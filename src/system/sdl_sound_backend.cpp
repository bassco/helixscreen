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
    desired.samples = 64; // Very low latency buffer — keeps callback period ~1.5ms
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

void SDLSoundBackend::set_voice_envelope(int slot, const ADSREnvelope& env,
                                          float velocity, float duration_ms) {
    if (slot < 0 || slot >= MAX_VOICES) return;
    auto& e = envelopes_[slot];
    e.attack_ms.store(env.attack_ms, std::memory_order_relaxed);
    e.decay_ms.store(env.decay_ms, std::memory_order_relaxed);
    e.sustain_level.store(env.sustain_level, std::memory_order_relaxed);
    e.release_ms.store(env.release_ms, std::memory_order_relaxed);
    e.velocity.store(velocity, std::memory_order_relaxed);
    e.duration_ms.store(duration_ms, std::memory_order_relaxed);
    // Bump generation last with release fence — callback sees consistent params
    e.generation.fetch_add(1, std::memory_order_release);
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
    if (type.empty()) {
        filter_type_.store(helix::audio::FilterType::NONE, std::memory_order_relaxed);
        return;
    }

    auto ft = helix::audio::filter_type_from_string(type);
    bool type_changed = (ft != filter_type_.load(std::memory_order_relaxed));
    filter_cutoff_.store(cutoff, std::memory_order_relaxed);
    helix::audio::compute_biquad_coeffs(filter_, ft, cutoff, static_cast<float>(sample_rate_));
    // Only reset filter state when the filter type changes or was previously off.
    // Resetting z1/z2 on every coefficient update (e.g. during sweeps) restarts the
    // filter cold each tick, causing clicks and excessive attenuation.
    if (type_changed) {
        filter_.z1 = 0;
        filter_.z2 = 0;
    }
    filter_type_.store(ft, std::memory_order_release);
}

// ============================================================================
// Per-sample envelope computation (runs in audio callback thread)
// ============================================================================

float SDLSoundBackend::advance_envelope(VoiceEnvelope& env, float sample_rate) {
    // Check for new note (generation changed)
    uint32_t gen = env.generation.load(std::memory_order_acquire);
    if (gen != env.cb_generation) {
        env.cb_generation = gen;
        env.elapsed_samples = 0;
        env.current_amplitude = 0;
    }

    float vel = env.velocity.load(std::memory_order_relaxed);
    if (vel <= 0.001f) {
        env.current_amplitude = 0;
        return 0;
    }

    float a_ms = env.attack_ms.load(std::memory_order_relaxed);
    float d_ms = env.decay_ms.load(std::memory_order_relaxed);
    float s = env.sustain_level.load(std::memory_order_relaxed);
    float r_ms = env.release_ms.load(std::memory_order_relaxed);
    float dur_ms = env.duration_ms.load(std::memory_order_relaxed);

    // Convert elapsed samples to milliseconds
    float elapsed_ms = env.elapsed_samples * 1000.0f / sample_rate;
    env.elapsed_samples++;

    // Total step duration (at least enough for the full ADSR)
    float total_ms = std::max(dur_ms, a_ms + d_ms + r_ms);

    // If all ADSR times are 0, return full velocity
    if (a_ms <= 0 && d_ms <= 0 && r_ms <= 0) {
        env.current_amplitude = (elapsed_ms < total_ms) ? vel : 0;
        return env.current_amplitude;
    }

    // Past the end of the note
    if (elapsed_ms >= total_ms) {
        env.current_amplitude = 0;
        return 0;
    }

    float release_start = total_ms - r_ms;
    float amp;

    if (elapsed_ms < a_ms) {
        // Attack: ramp 0 → 1
        amp = (a_ms > 0) ? (elapsed_ms / a_ms) : 1.0f;
    } else if (elapsed_ms < a_ms + d_ms) {
        // Decay: ramp 1 → sustain
        float decay_progress = (d_ms > 0) ? ((elapsed_ms - a_ms) / d_ms) : 1.0f;
        amp = 1.0f - (1.0f - s) * decay_progress;
    } else if (elapsed_ms < release_start) {
        // Sustain
        amp = s;
    } else {
        // Release: ramp sustain → 0
        float release_elapsed = elapsed_ms - release_start;
        float release_progress = (r_ms > 0) ? std::clamp(release_elapsed / r_ms, 0.0f, 1.0f) : 1.0f;
        amp = s * (1.0f - release_progress);
    }

    env.current_amplitude = amp * vel;
    return env.current_amplitude;
}

// ============================================================================
// Audio callback (runs in SDL audio thread)
// ============================================================================

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

    // Mix synth voices with per-sample envelope
    for (int v = 0; v < MAX_VOICES; ++v) {
        float gate = self->voices_[v].amplitude.load(std::memory_order_relaxed);
        auto& env = self->envelopes_[v];

        // Skip voice if gate is off and envelope is already silent
        if (gate <= 0.001f && env.current_amplitude <= 0.001f) {
            continue;
        }
        float freq = self->voices_[v].freq.load(std::memory_order_relaxed);
        if (freq <= 0.0f) continue;

        // Generate waveform at unit amplitude
        helix::audio::generate_samples(
            vbuf, num_samples, self->sample_rate_,
            self->voices_[v].wave.load(std::memory_order_relaxed),
            freq, 1.0f,
            self->voices_[v].duty.load(std::memory_order_relaxed),
            self->voices_[v].phase);

        // Apply per-sample envelope
        for (int i = 0; i < num_samples; ++i) {
            float amp = advance_envelope(env, static_cast<float>(self->sample_rate_));
            mix[i] += vbuf[i] * amp;
        }
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
