#include "audiooutput.h"

#include <vector>

#define SDL_MAIN_HANDLED
#include <SDL.h>

namespace {

constexpr int kChannels = 2;
constexpr size_t kBytesPerFrame = kChannels * sizeof(int16_t);

}  // namespace

AudioOutput::~AudioOutput()
{
    close();
}

bool AudioOutput::open(int sampleRate)
{
    close();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;

    SDL_AudioSpec want{};
    want.freq = sampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = kChannels;
    want.samples = 512;
    SDL_AudioSpec got{};
    // No changes allowed: SDL converts if the device wants another format.
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (device_ == 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    sampleRate_ = sampleRate;
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void AudioOutput::close()
{
    if (device_ == 0)
        return;
    SDL_CloseAudioDevice(device_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    device_ = 0;
    sampleRate_ = 0;
}

double AudioOutput::queue(const int16_t* samples, size_t count)
{
    if (device_ == 0)
        return 0;
    // Starting from empty, lead with silence so the queue has its cushion
    // at once instead of stuttering while it builds up.
    if (SDL_GetQueuedAudioSize(device_) == 0) {
        const std::vector<int16_t> silence(
            static_cast<size_t>(sampleRate_ * targetLatency_) * kChannels, 0);
        SDL_QueueAudio(device_, silence.data(), static_cast<Uint32>(silence.size() * sizeof(int16_t)));
    }
    if (count > 0)
        SDL_QueueAudio(device_, samples, static_cast<Uint32>(count * sizeof(int16_t)));
    return static_cast<double>(SDL_GetQueuedAudioSize(device_)) / kBytesPerFrame / sampleRate_;
}

void AudioOutput::clear()
{
    if (device_ != 0)
        SDL_ClearQueuedAudio(device_);
}
