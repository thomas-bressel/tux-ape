#pragma once

// The joystick or game pad plugged into the host, read through SDL2, for
// WinAPE's "Enable Joystick". The first controller found is used; one that
// is plugged in later is picked up, one that is pulled out is let go.
class HostJoystick {
public:
    // What the stick and buttons are doing, as bits.
    enum : unsigned { Up = 1, Down = 2, Left = 4, Right = 8, Fire1 = 16, Fire2 = 32, Fire3 = 64 };

    HostJoystick() = default;
    ~HostJoystick();
    HostJoystick(const HostJoystick&) = delete;
    HostJoystick& operator=(const HostJoystick&) = delete;

    // Meant to be called once per emulated frame. Returns 0 when there is no
    // controller.
    unsigned poll();
    void close();
    bool connected() const { return controller_ != nullptr || joystick_ != nullptr; }

    // The directions a stick position stands for; x and y from -32768 to
    // 32767, y growing downwards.
    static unsigned directions(int x, int y);

private:
    bool initialised_ = false;
    bool failed_ = false;
    void* controller_ = nullptr;  // SDL_GameController
    void* joystick_ = nullptr;    // SDL_Joystick, for devices SDL has no mapping for
    int framesToRetry_ = 0;

    void findDevice();
};
