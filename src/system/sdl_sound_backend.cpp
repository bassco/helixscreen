// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_DISPLAY_SDL

#include "sdl_sound_backend.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

SDLSoundBackend::SDLSoundBackend() = default;

SDLSoundBackend::~SDLSoundBackend() {
    shutdown();
}

bool SDLSoundBackend::initialize() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        spdlog::error("[SDLSound] SDL_InitSubSystem(AUDIO) failed: {}", SDL_GetError());
        return false;
    }

    SDL_AudioSpec desired{};
    desired.freq = sample_rate_;
    desired.format = AUDIO_F32SYS;
    desired.channels = 1;
    desired.samples = 256; // Low latency buffer
    desired.callback = audio_callback;
    desired.userdata = this;

    SDL_AudioSpec obtained{};
    device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (device_id_ == 0) {
        spdlog::error("[SDLSound] SDL_OpenAudioDevice failed: {}", SDL_GetError());
        return false;
    }

    sample_rate_ = obtained.freq;
    SDL_PauseAudioDevice(device_id_, 0); // Start playback
    voice_buf_.resize(obtained.samples);
    mix_buf_.resize(obtained.samples);
    initialized_ = true;

    spdlog::info("[SDLSound] Audio initialized: {} Hz, {} samples buffer", sample_rate_,
                 obtained.samples);
    return true;
}

void SDLSoundBackend::shutdown() {
    if (!initialized_)
        return;
    if (device_id_) {
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
    }
    initialized_ = false;
    spdlog::info("[SDLSound] Audio shutdown");
}

void SDLSoundBackend::set_tone(float freq_hz, float amplitude, float duty_cycle) {
    set_voice(0, freq_hz, amplitude, duty_cycle);
}

void SDLSoundBackend::silence() {
    for (int v = 0; v < MAX_VOICES; ++v)
        voices_[v].amplitude.store(0, std::memory_order_relaxed);
}

void SDLSoundBackend::set_waveform(Waveform w) {
    set_voice_waveform(0, w);
}

void SDLSoundBackend::set_voice(int slot, float freq_hz, float amplitude, float duty_cycle) {
    if (slot < 0 || slot >= MAX_VOICES) return;
    voices_[slot].freq.store(freq_hz, std::memory_order_relaxed);
    voices_[slot].amplitude.store(amplitude, std::memory_order_relaxed);
    voices_[slot].duty.store(duty_cycle, std::memory_order_relaxed);
}

void SDLSoundBackend::set_voice_waveform(int slot, Waveform w) {
    if (slot < 0 || slot >= MAX_VOICES) return;
    voices_[slot].wave.store(w, std::memory_order_relaxed);
}

void SDLSoundBackend::silence_voice(int slot) {
    if (slot < 0 || slot >= MAX_VOICES) return;
    voices_[slot].amplitude.store(0, std::memory_order_relaxed);
}

void SDLSoundBackend::set_render_source(std::function<void(float*, size_t, int)> fn) {
    std::lock_guard<std::mutex> lock(render_source_mutex_);
    render_source_ = std::move(fn);
}

void SDLSoundBackend::clear_render_source() {
    std::lock_guard<std::mutex> lock(render_source_mutex_);
    render_source_ = nullptr;
}

void SDLSoundBackend::set_filter(const std::string& type, float cutoff) {
    // Note: compute_biquad_coeffs writes to filter_ (plain struct) from the sequencer
    // thread while the render thread reads it. This is technically a data race, but
    // a torn read at worst causes a single-frame audio glitch — acceptable for a
    // synthesizer buzzer on a 3D printer touchscreen.
    if (type.empty()) {
        filter_type_.store(helix::audio::FilterType::NONE, std::memory_order_relaxed);
        return;
    }

    auto ft = helix::audio::filter_type_from_string(type);
    filter_cutoff_.store(cutoff, std::memory_order_relaxed);
    helix::audio::compute_biquad_coeffs(filter_, ft, cutoff, static_cast<float>(sample_rate_));
    filter_.z1 = 0;
    filter_.z2 = 0;
    filter_type_.store(ft, std::memory_order_release);
}

void SDLSoundBackend::audio_callback(void* userdata, uint8_t* stream, int len) {
    auto* self = static_cast<SDLSoundBackend*>(userdata);
    auto* out = reinterpret_cast<float*>(stream);
    int num_samples = len / static_cast<int>(sizeof(float));

    // Start with silence in mix buffer
    auto* mix = self->mix_buf_.data();
    auto* vbuf = self->voice_buf_.data();
    std::memset(mix, 0, num_samples * sizeof(float));
    bool has_audio = false;

    // Render tracker PCM if active
    {
        std::function<void(float*, size_t, int)> source;
        {
            std::lock_guard<std::mutex> lock(self->render_source_mutex_);
            source = self->render_source_;
        }
        if (source) {
            source(mix, static_cast<size_t>(num_samples), self->sample_rate_);
            has_audio = true;
        }
    }

    std::atomic_thread_fence(std::memory_order_acquire);

    // Mix synth voices on top (SFX layered over tracker)
    for (int v = 0; v < MAX_VOICES; ++v) {
        float amp = self->voices_[v].amplitude.load(std::memory_order_relaxed);
        if (amp <= 0.001f) {
            self->voices_[v].phase = 0;
            continue;
        }
        float freq = self->voices_[v].freq.load(std::memory_order_relaxed);
        if (freq <= 0.0f) continue;

        helix::audio::generate_samples(
            vbuf, num_samples, self->sample_rate_,
            self->voices_[v].wave.load(std::memory_order_relaxed),
            freq, amp,
            self->voices_[v].duty.load(std::memory_order_relaxed),
            self->voices_[v].phase);

        for (int i = 0; i < num_samples; ++i)
            mix[i] += vbuf[i];
        has_audio = true;
    }

    if (!has_audio) {
        std::memset(stream, 0, static_cast<size_t>(len));
        return;
    }

    // Clamp
    for (int i = 0; i < num_samples; ++i)
        mix[i] = std::clamp(mix[i], -1.0f, 1.0f);

    // Apply shared filter
    if (self->filter_type_.load(std::memory_order_acquire) != helix::audio::FilterType::NONE) {
        helix::audio::apply_filter(self->filter_, mix, num_samples);
    }

    std::memcpy(out, mix, num_samples * sizeof(float));
}

#endif // HELIX_DISPLAY_SDL
