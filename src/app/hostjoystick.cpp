#include "hostjoystick.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

namespace {

// A stick counts as pushed past a third of its travel.
constexpr int kDeadZone = 11000;

}  // namespace

HostJoystick::~HostJoystick()
{
    close();
    if (initialised_)
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

unsigned HostJoystick::directions(int x, int y)
{
    unsigned bits = 0;
    if (x < -kDeadZone)
        bits |= Left;
    if (x > kDeadZone)
        bits |= Right;
    if (y < -kDeadZone)
        bits |= Up;
    if (y > kDeadZone)
        bits |= Down;
    return bits;
}

void HostJoystick::close()
{
    if (controller_)
        SDL_GameControllerClose(static_cast<SDL_GameController*>(controller_));
    if (joystick_)
        SDL_JoystickClose(static_cast<SDL_Joystick*>(joystick_));
    controller_ = joystick_ = nullptr;
}

void HostJoystick::findDevice()
{
    for (int i = 0; i < SDL_NumJoysticks() && !connected(); ++i) {
        if (SDL_IsGameController(i))
            controller_ = SDL_GameControllerOpen(i);
        else
            joystick_ = SDL_JoystickOpen(i);
    }
}

unsigned HostJoystick::poll()
{
    if (failed_)
        return 0;
    if (!initialised_) {
        // There is no SDL window here: the controller must be read whatever
        // window has the focus.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            failed_ = true;
            return 0;
        }
        initialised_ = true;
    }
    SDL_JoystickUpdate();

    if (controller_ && !SDL_GameControllerGetAttached(static_cast<SDL_GameController*>(controller_)))
        close();
    if (joystick_ && !SDL_JoystickGetAttached(static_cast<SDL_Joystick*>(joystick_)))
        close();
    if (!connected()) {
        // Looking for devices is not free: about once a second is enough.
        if (--framesToRetry_ > 0)
            return 0;
        framesToRetry_ = 50;
        findDevice();
        if (!connected())
            return 0;
    }

    unsigned bits = 0;
    if (controller_) {
        auto* pad = static_cast<SDL_GameController*>(controller_);
        auto button = [pad](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
        bits |= directions(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX),
                           SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
        if (button(SDL_CONTROLLER_BUTTON_DPAD_UP))
            bits |= Up;
        if (button(SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            bits |= Down;
        if (button(SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            bits |= Left;
        if (button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            bits |= Right;
        if (button(SDL_CONTROLLER_BUTTON_A))
            bits |= Fire1;
        if (button(SDL_CONTROLLER_BUTTON_B))
            bits |= Fire2;
        if (button(SDL_CONTROLLER_BUTTON_X))
            bits |= Fire3;
    } else {
        auto* stick = static_cast<SDL_Joystick*>(joystick_);
        if (SDL_JoystickNumAxes(stick) >= 2)
            bits |= directions(SDL_JoystickGetAxis(stick, 0), SDL_JoystickGetAxis(stick, 1));
        if (SDL_JoystickNumHats(stick) > 0) {
            const Uint8 hat = SDL_JoystickGetHat(stick, 0);
            if (hat & SDL_HAT_UP)
                bits |= Up;
            if (hat & SDL_HAT_DOWN)
                bits |= Down;
            if (hat & SDL_HAT_LEFT)
                bits |= Left;
            if (hat & SDL_HAT_RIGHT)
                bits |= Right;
        }
        const int buttons = SDL_JoystickNumButtons(stick);
        if (buttons > 0 && SDL_JoystickGetButton(stick, 0))
            bits |= Fire1;
        if (buttons > 1 && SDL_JoystickGetButton(stick, 1))
            bits |= Fire2;
        if (buttons > 2 && SDL_JoystickGetButton(stick, 2))
            bits |= Fire3;
    }
    return bits;
}
