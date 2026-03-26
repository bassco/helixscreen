// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_HAS_ALSA

#include "alsa_sound_backend.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

ALSASoundBackend::ALSASoundBackend() = default;

ALSASoundBackend::~ALSASoundBackend() {
    shutdown();
}

bool ALSASoundBackend::initialize() {
    // Open with NONBLOCK to avoid hanging if device is busy
    int err = snd_pcm_open(&pcm_, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
        spdlog::error("[ALSASound] Cannot open PCM device 'default': {}", snd_strerror(err));
        return false;
    }

    // Switch to blocking mode for write loop
    snd_pcm_nonblock(pcm_, 0);

    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_alloca(&hw_params);

    err = snd_pcm_hw_params_any(pcm_, hw_params);
    if (err < 0) {
        spdlog::error("[ALSASound] Cannot get hardware params: {}", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    err = snd_pcm_hw_params_set_access(pcm_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        spdlog::error("[ALSASound] Cannot set access type: {}", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Try float format first, fall back to S16
    use_s16_ = false;
    err = snd_pcm_hw_params_set_format(pcm_, hw_params, SND_PCM_FORMAT_FLOAT_LE);
    if (err < 0) {
        spdlog::debug("[ALSASound] Float format not supported, trying S16_LE");
        err = snd_pcm_hw_params_set_format(pcm_, hw_params, SND_PCM_FORMAT_S16_LE);
        if (err < 0) {
            spdlog::error("[ALSASound] Cannot set audio format: {}", snd_strerror(err));
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }
        use_s16_ = true;
    }

    // Try mono first, fall back to stereo (HDMI often requires stereo)
    channels_ = 1;
    err = snd_pcm_hw_params_set_channels(pcm_, hw_params, 1);
    if (err < 0) {
        spdlog::debug("[ALSASound] Mono not supported, trying stereo");
        err = snd_pcm_hw_params_set_channels(pcm_, hw_params, 2);
        if (err < 0) {
            spdlog::error("[ALSASound] Cannot set channel count: {}", snd_strerror(err));
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }
        channels_ = 2;
    }

    // Sample rate
    sample_rate_ = 44100;
    err = snd_pcm_hw_params_set_rate_near(pcm_, hw_params, &sample_rate_, nullptr);
    if (err < 0) {
        spdlog::error("[ALSASound] Cannot set sample rate: {}", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Period size
    period_size_ = 256;
    err = snd_pcm_hw_params_set_period_size_near(pcm_, hw_params, &period_size_, nullptr);
    if (err < 0) {
        spdlog::error("[ALSASound] Cannot set period size: {}", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Buffer size: 2x period for low latency
    snd_pcm_uframes_t buffer_size = period_size_ * 2;
    err = snd_pcm_hw_params_set_buffer_size_near(pcm_, hw_params, &buffer_size);
    if (err < 0) {
        spdlog::error("[ALSASound] Cannot set buffer size: {}", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Apply hardware params
    err = snd_pcm_hw_params(pcm_, hw_params);
    if (err < 0) {
        spdlog::error("[ALSASound] Cannot apply hw params: {}", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    spdlog::info("[ALSASound] Audio initialized: {} Hz, {} ch, period {}, format {}", sample_rate_,
                 channels_, period_size_, use_s16_ ? "S16_LE" : "FLOAT_LE");

    // Allocate scratch buffers for multi-voice mixing
    voice_buf_.resize(period_size_);
    mix_buf_.resize(period_size_);

    // Start render thread
    running_.store(true, std::memory_order_relaxed);
    render_thread_ = std::thread(&ALSASoundBackend::render_loop, this);

    return true;
}

void ALSASoundBackend::shutdown() {
    if (!running_.load(std::memory_order_relaxed) && pcm_ == nullptr)
        return;

    running_.store(false, std::memory_order_relaxed);

    // Join render thread before closing device
    if (render_thread_.joinable()) {
        render_thread_.join();
    }

    if (pcm_) {
        snd_pcm_drop(pcm_);
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }

    spdlog::info("[ALSASound] Audio shutdown");
}

void ALSASoundBackend::set_tone(float freq_hz, float amplitude, float duty_cycle) {
    set_voice(0, freq_hz, amplitude, duty_cycle);
}

void ALSASoundBackend::silence() {
    for (int v = 0; v < MAX_VOICES; ++v)
        voices_[v].amplitude.store(0, std::memory_order_relaxed);
}

void ALSASoundBackend::set_waveform(Waveform w) {
    set_voice_waveform(0, w);
}

void ALSASoundBackend::set_voice(int slot, float freq_hz, float amplitude, float duty_cycle) {
    if (slot < 0 || slot >= MAX_VOICES) return;
    voices_[slot].freq.store(freq_hz, std::memory_order_relaxed);
    voices_[slot].amplitude.store(amplitude, std::memory_order_relaxed);
    voices_[slot].duty.store(duty_cycle, std::memory_order_relaxed);
}

void ALSASoundBackend::set_voice_waveform(int slot, Waveform w) {
    if (slot < 0 || slot >= MAX_VOICES) return;
    voices_[slot].wave.store(w, std::memory_order_relaxed);
}

void ALSASoundBackend::silence_voice(int slot) {
    if (slot < 0 || slot >= MAX_VOICES) return;
    voices_[slot].amplitude.store(0, std::memory_order_relaxed);
}

void ALSASoundBackend::set_render_source(std::function<void(float*, size_t, int)> fn) {
    std::lock_guard<std::mutex> lock(render_source_mutex_);
    render_source_ = std::move(fn);
}

void ALSASoundBackend::clear_render_source() {
    std::lock_guard<std::mutex> lock(render_source_mutex_);
    render_source_ = nullptr;
}

void ALSASoundBackend::set_filter(const std::string& type, float cutoff) {
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

void ALSASoundBackend::render_loop() {
    const size_t frames = period_size_;

    // Allocate output buffers once
    std::vector<float> stereo;
    std::vector<int16_t> s16_buf;

    if (channels_ == 2) {
        stereo.resize(frames * 2);
    }
    if (use_s16_) {
        s16_buf.resize(frames * channels_);
    }

    while (running_.load(std::memory_order_relaxed)) {
        // Check for external render source (tracker PCM playback)
        bool rendered_externally = false;
        {
            std::function<void(float*, size_t, int)> source;
            {
                std::lock_guard<std::mutex> lock(render_source_mutex_);
                source = render_source_;
            }
            if (source) {
                source(mix_buf_.data(), frames, static_cast<int>(sample_rate_));
                // Apply filter if active
                auto ft = filter_type_.load(std::memory_order_acquire);
                if (ft != helix::audio::FilterType::NONE) {
                    float cutoff = filter_cutoff_.load(std::memory_order_relaxed);
                    helix::audio::update_filter_if_needed(filter_, ft, cutoff,
                                                          static_cast<float>(sample_rate_));
                    helix::audio::apply_filter(filter_, mix_buf_.data(),
                                               static_cast<int>(frames));
                }
                rendered_externally = true;
            }
        }

        if (!rendered_externally) {
        std::atomic_thread_fence(std::memory_order_acquire);

        // Check if any voice is active
        bool any_active = false;
        for (int v = 0; v < MAX_VOICES; ++v) {
            if (voices_[v].amplitude.load(std::memory_order_relaxed) > 0.001f) {
                any_active = true;
                break;
            }
        }

        if (!any_active) {
            // Silence — write zeros and reset all phases
            std::memset(mix_buf_.data(), 0, frames * sizeof(float));
            for (int v = 0; v < MAX_VOICES; ++v)
                voices_[v].phase = 0;
        } else {
            // Mix all active voices
            std::memset(mix_buf_.data(), 0, frames * sizeof(float));
            for (int v = 0; v < MAX_VOICES; ++v) {
                float amp = voices_[v].amplitude.load(std::memory_order_relaxed);
                if (amp <= 0.001f) {
                    voices_[v].phase = 0;
                    continue;
                }
                float freq = voices_[v].freq.load(std::memory_order_relaxed);
                if (freq <= 0.0f) continue;

                helix::audio::generate_samples(
                    voice_buf_.data(), static_cast<int>(frames),
                    static_cast<int>(sample_rate_),
                    voices_[v].wave.load(std::memory_order_relaxed),
                    freq, amp,
                    voices_[v].duty.load(std::memory_order_relaxed),
                    voices_[v].phase);

                for (size_t i = 0; i < frames; ++i)
                    mix_buf_[i] += voice_buf_[i];
            }

            // Clamp
            for (size_t i = 0; i < frames; ++i)
                mix_buf_[i] = std::clamp(mix_buf_[i], -1.0f, 1.0f);

            // Apply shared filter
            auto ft = filter_type_.load(std::memory_order_acquire);
            if (ft != helix::audio::FilterType::NONE) {
                float cutoff = filter_cutoff_.load(std::memory_order_relaxed);
                helix::audio::update_filter_if_needed(filter_, ft, cutoff,
                                                      static_cast<float>(sample_rate_));
                helix::audio::apply_filter(filter_, mix_buf_.data(), static_cast<int>(frames));
            }
        }
        } // !rendered_externally

        // Determine what to write
        const void* write_buf = nullptr;

        if (channels_ == 2 && use_s16_) {
            mono_to_stereo(mix_buf_.data(), stereo.data(), frames);
            float_to_s16(stereo.data(), s16_buf.data(), frames * 2);
            write_buf = s16_buf.data();
        } else if (channels_ == 2) {
            mono_to_stereo(mix_buf_.data(), stereo.data(), frames);
            write_buf = stereo.data();
        } else if (use_s16_) {
            float_to_s16(mix_buf_.data(), s16_buf.data(), frames);
            write_buf = s16_buf.data();
        } else {
            write_buf = mix_buf_.data();
        }

        snd_pcm_sframes_t written =
            snd_pcm_writei(pcm_, write_buf, static_cast<snd_pcm_uframes_t>(frames));
        if (written < 0) {
            written = recover_xrun(written);
            if (written < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
}

snd_pcm_sframes_t ALSASoundBackend::recover_xrun(snd_pcm_sframes_t err) {
    if (err == -EPIPE) {
        // Underrun
        spdlog::debug("[ALSASound] Buffer underrun, recovering");
        int ret = snd_pcm_prepare(pcm_);
        if (ret < 0) {
            spdlog::error("[ALSASound] Cannot recover from underrun: {}", snd_strerror(ret));
            return static_cast<snd_pcm_sframes_t>(ret);
        }
        return 0;
    }

    if (err == -ESTRPIPE) {
        // Suspended
        spdlog::debug("[ALSASound] Device suspended, resuming");
        int ret;
        while ((ret = snd_pcm_resume(pcm_)) == -EAGAIN) {
            if (!running_.load(std::memory_order_relaxed)) return err;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (ret < 0) {
            ret = snd_pcm_prepare(pcm_);
            if (ret < 0) {
                spdlog::error("[ALSASound] Cannot recover from suspend: {}", snd_strerror(ret));
                return static_cast<snd_pcm_sframes_t>(ret);
            }
        }
        return 0;
    }

    return err;
}

void ALSASoundBackend::mono_to_stereo(const float* mono, float* stereo, size_t frame_count) {
    for (size_t i = 0; i < frame_count; ++i) {
        stereo[i * 2] = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }
}

void ALSASoundBackend::float_to_s16(const float* src, int16_t* dst, size_t sample_count) {
    for (size_t i = 0; i < sample_count; ++i) {
        dst[i] = static_cast<int16_t>(std::clamp(src[i], -1.0f, 1.0f) * 32767.0f);
    }
}

#endif // HELIX_HAS_ALSA
