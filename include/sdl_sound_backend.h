// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_DISPLAY_SDL

#include "sound_backend.h"
#include "sound_synthesis.h"
#include "sound_theme.h"

#include <SDL.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

/// SDL2 audio backend -- generates real waveform audio for desktop simulator
class SDLSoundBackend : public SoundBackend {
  public:
    SDLSoundBackend();
    ~SDLSoundBackend() override;

    // SoundBackend interface
    void set_tone(float freq_hz, float amplitude, float duty_cycle) override;
    void silence() override;
    void set_waveform(Waveform w) override;
    void set_filter(const std::string& type, float cutoff) override;
    bool supports_waveforms() const override {
        return true;
    }
    bool supports_amplitude() const override {
        return true;
    }
    bool supports_filter() const override {
        return true;
    }
    float min_tick_ms() const override {
        return 1.0f;
    }

    /// Initialize SDL audio device. Returns false on failure.
    bool initialize();

    /// Shutdown SDL audio device
    void shutdown();

    // Polyphonic voice interface
    void set_voice(int slot, float freq_hz, float amplitude, float duty_cycle) override;
    void set_voice_waveform(int slot, Waveform w) override;
    void silence_voice(int slot) override;
    int voice_count() const override { return MAX_VOICES; }

    // Render source for direct audio generation (tracker PCM playback)
    bool supports_render_source() const override { return true; }
    void set_render_source(std::function<void(float*, size_t, int)> fn) override;
    void clear_render_source() override;

  private:
    static constexpr int MAX_VOICES = 4;

    struct VoiceState {
        std::atomic<float> freq{0};
        std::atomic<float> amplitude{0};
        std::atomic<float> duty{0.5f};
        std::atomic<Waveform> wave{Waveform::SQUARE};
        float phase = 0;  // audio callback thread only
    };

    static void audio_callback(void* userdata, uint8_t* stream, int len);

    VoiceState voices_[MAX_VOICES];

    // Filter parameters (shared across all voices)
    std::atomic<float> filter_cutoff_{20000.0f};
    std::atomic<helix::audio::FilterType> filter_type_{helix::audio::FilterType::NONE};

    // Filter state (only accessed from audio callback thread)
    helix::audio::BiquadFilter filter_;

    // Scratch buffers for multi-voice mixing (sized in initialize())
    std::vector<float> voice_buf_;
    std::vector<float> mix_buf_;

    // External render source (tracker PCM playback)
    std::function<void(float*, size_t, int)> render_source_;
    std::mutex render_source_mutex_;

    SDL_AudioDeviceID device_id_ = 0;
    int sample_rate_ = 44100;
    bool initialized_ = false;
};

#endif // HELIX_DISPLAY_SDL
