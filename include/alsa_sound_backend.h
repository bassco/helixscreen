// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef HELIX_HAS_ALSA

#include "sound_backend.h"
#include "sound_synthesis.h"

#include <alsa/asoundlib.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// Per-voice ADSR envelope state, advanced per-sample in the render loop.
/// Will be replaced by VoiceSlot in Task 4 (ALSA backend migration).
struct VoiceEnvelope {
    std::atomic<float> attack_ms{5};
    std::atomic<float> decay_ms{40};
    std::atomic<float> sustain_level{0.6f};
    std::atomic<float> release_ms{80};
    std::atomic<float> velocity{0};
    std::atomic<float> duration_ms{0};
    std::atomic<uint32_t> generation{0};

    uint32_t cb_generation = 0;
    float elapsed_samples = 0;
    float current_amplitude = 0;
};

/// ALSA PCM audio backend — real waveform synthesis for Linux SBCs
/// Uses the shared sound_synthesis.h for sample generation.
/// Threading: sequencer thread writes atomic params, render thread reads and generates.
class ALSASoundBackend : public SoundBackend {
  public:
    ALSASoundBackend();
    ~ALSASoundBackend() override;

    // SoundBackend interface
    void set_tone(float freq_hz, float amplitude, float duty_cycle) override;
    void silence() override;
    void set_waveform(Waveform w) override;
    void set_filter(const std::string& type, float cutoff) override;
    bool supports_waveforms() const override { return true; }
    bool supports_amplitude() const override { return true; }
    bool supports_filter() const override { return true; }
    float min_tick_ms() const override { return 1.0f; }

    /// Initialize ALSA PCM device. Returns false if no audio device available.
    bool initialize();

    /// Stop render thread and close ALSA device.
    void shutdown();

    /// Duplicate mono buffer to interleaved stereo (L=R). Public for testability.
    static void mono_to_stereo(const float* mono, float* stereo, size_t frame_count);

    /// Convert float [-1,1] samples to int16 with clamping. Public for testability.
    static void float_to_s16(const float* src, int16_t* dst, size_t sample_count);

    // Polyphonic voice interface
    void set_voice(int slot, float freq_hz, float amplitude, float duty_cycle) override;
    void set_voice_waveform(int slot, Waveform w) override;
    void silence_voice(int slot) override;
    int voice_count() const override { return MAX_VOICES; }

    /// Set envelope parameters for a voice (called by sequencer at step start)
    void set_voice_envelope(int slot, const ADSREnvelope& env, float velocity,
                            float duration_ms) override;

    // Render source for direct audio generation (tracker PCM playback)
    bool supports_render_source() const override { return true; }
    void set_render_source(std::function<void(float*, size_t, int)> fn) override;
    void clear_render_source() override;

  private:
    void render_loop();
    snd_pcm_sframes_t recover_xrun(snd_pcm_sframes_t err);

    /// Compute per-sample envelope value in the render thread
    static float advance_envelope(VoiceEnvelope& env, float sample_rate);

    snd_pcm_t* pcm_ = nullptr;
    std::thread render_thread_;
    std::atomic<bool> running_{false};

    static constexpr int MAX_VOICES = 4;

    struct VoiceState {
        std::atomic<float> freq{0};
        std::atomic<float> amplitude{0};
        std::atomic<float> duty{0.5f};
        std::atomic<Waveform> wave{Waveform::SQUARE};
        float phase = 0; // render thread only
    };

    VoiceState voices_[MAX_VOICES];
    VoiceEnvelope envelopes_[MAX_VOICES];

    // Filter parameters
    std::atomic<helix::audio::FilterType> filter_type_{helix::audio::FilterType::NONE};
    std::atomic<float> filter_cutoff_{20000.0f};

    // Render thread state (only accessed from render thread)
    helix::audio::BiquadFilter filter_;

    // Scratch buffers for multi-voice mixing
    std::vector<float> voice_buf_;
    std::vector<float> mix_buf_;

    // External render source (tracker PCM playback)
    std::function<void(float*, size_t, int)> render_source_;
    std::mutex render_source_mutex_;

    // Audio format negotiated during initialize()
    unsigned int sample_rate_ = 44100;
    snd_pcm_uframes_t period_size_ = 256;
    unsigned int channels_ = 1;
    bool use_s16_ = false;
};

#endif // HELIX_HAS_ALSA
