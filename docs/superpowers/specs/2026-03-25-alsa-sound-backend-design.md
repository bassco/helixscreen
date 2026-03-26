# ALSA Sound Backend

**Date:** 2026-03-25
**Status:** Design

## Summary

Add an `ALSASoundBackend` that uses Linux ALSA for real waveform audio on embedded targets (Raspberry Pi, BTT CB1, SonicPad, x86 SBCs). This replaces the M300 beeper as the primary sound output on devices with audio hardware, enabling full synthesized audio (waveforms, envelopes, filters) through HDMI, analog, or USB audio outputs.

## Motivation

Currently, embedded Linux targets fall through the backend detection chain to either PWM (AD5M buzzer) or M300 (Klipper G-code beeper). Both are single-frequency, no-amplitude-control outputs. The Pi and CB1 both have HDMI audio and the CB1 has an onboard analog codec — real audio hardware that supports full waveform synthesis, but we can't use it because the SDL audio backend is gated behind `HELIX_DISPLAY_SDL` (desktop-only).

### Verified Hardware

| Device | Audio Cards | libasound2 |
|--------|-------------|------------|
| Pi 5 (192.168.1.113) | 2x HDMI (vc4-hdmi-0, vc4-hdmi-1) | 1.2.8 installed |
| BTT CB1 (192.168.1.112) | Audio hub (3 streams), HDMI, H616 Analog Codec | 1.2.4 installed |

## Design

### Prerequisite Refactor: Extract `sound_synthesis.h`

The static helpers (`generate_samples`, `compute_biquad_coeffs`, `apply_filter`, `BiquadFilter`) are currently inside `SDLSoundBackend` and gated behind `HELIX_DISPLAY_SDL`. They're pure math with zero SDL dependency. Extract them first:

- `include/sound_synthesis.h` — free functions + `BiquadFilter` struct in `namespace helix::audio`
- `src/system/sound_synthesis.cpp` — implementations (moved from `sdl_sound_backend.cpp`)
- No `#ifdef` guards — always compiled, pure math, no platform dependencies
- `SDLSoundBackend` delegates to these; `ALSASoundBackend` uses them directly

**Fix during extraction:** The existing `filter_type_` in SDL is a `std::string` written from the sequencer thread and read from the audio callback — a data race (UB, not just a glitch). During extraction, change the filter type to an enum (`FilterType::LOWPASS`, `FilterType::HIGHPASS`, `FilterType::NONE`) stored as `std::atomic<FilterType>`. Both SDL and ALSA backends benefit.

### New Class: `ALSASoundBackend`

Implements the existing `SoundBackend` interface. Uses the extracted `sound_synthesis.h` for waveform generation. Writes generated samples to an ALSA PCM device.

**Files:**
- `include/alsa_sound_backend.h` — header, guarded by `#ifdef HELIX_HAS_ALSA`
- `src/system/alsa_sound_backend.cpp` — implementation

Note: `initialize()` is a non-virtual method on the concrete class (same pattern as SDL and PWM backends — not part of the `SoundBackend` interface).

### Backend Interface (unchanged)

The existing `SoundBackend` interface is sufficient:

```cpp
void set_tone(float freq_hz, float amplitude, float duty_cycle);
void silence();
void set_waveform(Waveform w);
void set_filter(const std::string& type, float cutoff);
bool supports_waveforms() const;   // returns true
bool supports_amplitude() const;   // returns true
bool supports_filter() const;      // returns true
float min_tick_ms() const;         // returns 1.0f
```

### Architecture

```
SoundSequencer (tick thread, ~1ms)
    │
    ├─ set_tone(freq, amp, duty)     ──► ALSASoundBackend
    ├─ set_waveform(wave)                    │
    └─ set_filter(type, cutoff)              │
                                             ▼
                                    Audio render thread
                                    (fills ALSA buffer)
                                             │
                                    ┌────────┴────────┐
                                    │ generate_samples │  (from sound_synthesis.h)
                                    │ apply_filter     │  (from sound_synthesis.h)
                                    └────────┬────────┘
                                             │
                                             ▼
                                    snd_pcm_writei()
                                    → HDMI / Analog / USB
```

**Threading model:** The sequencer thread writes atomic parameters (`freq`, `amplitude`, `waveform`, filter type enum). The render thread reads them and generates samples via blocking `snd_pcm_writei()`. No mutex needed — same relaxed-atomic pattern as SDL, but with the filter type data race fixed (enum instead of string).

**Member types:**
- `std::atomic<float>` for `current_freq_`, `current_amplitude_`, `current_duty_`
- `std::atomic<Waveform>` for `current_wave_`
- `std::atomic<FilterType>` for `current_filter_type_` (fixed from SDL's string)
- `std::atomic<float>` for `filter_cutoff_`
- `std::atomic<bool>` for `running_` (controls render thread lifetime)

### ALSA Setup

```cpp
bool ALSASoundBackend::initialize() {
    // Open with SND_PCM_NONBLOCK to avoid hanging if device is busy,
    // then switch to blocking mode for writei
    int rc = snd_pcm_open(&pcm_, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (rc < 0) {
        spdlog::debug("[ALSASound] snd_pcm_open failed: {}", snd_strerror(rc));
        return false;
    }
    snd_pcm_nonblock(pcm_, 0);  // Switch to blocking for writei

    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_any(pcm_, params) < 0) {
        snd_pcm_close(pcm_);
        return false;
    }
    snd_pcm_hw_params_set_access(pcm_, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    // Try float32 first (matches SDL), fall back to S16_LE
    if (snd_pcm_hw_params_set_format(pcm_, params, SND_PCM_FORMAT_FLOAT_LE) < 0) {
        if (snd_pcm_hw_params_set_format(pcm_, params, SND_PCM_FORMAT_S16_LE) < 0) {
            snd_pcm_close(pcm_);
            return false;
        }
        use_s16_ = true;
    }

    // Try mono first; HDMI often requires stereo minimum — fall back to 2ch
    channels_ = 1;
    if (snd_pcm_hw_params_set_channels(pcm_, params, 1) < 0) {
        if (snd_pcm_hw_params_set_channels(pcm_, params, 2) < 0) {
            snd_pcm_close(pcm_);
            return false;
        }
        channels_ = 2;
    }

    unsigned int rate = 44100;
    if (snd_pcm_hw_params_set_rate_near(pcm_, params, &rate, nullptr) < 0) {
        snd_pcm_close(pcm_);
        return false;
    }
    sample_rate_ = rate;

    snd_pcm_uframes_t period = 256;
    snd_pcm_hw_params_set_period_size_near(pcm_, params, &period, nullptr);
    period_size_ = period;

    // Set buffer to 2x period for predictable latency (~11.6ms at 44100)
    snd_pcm_uframes_t buffer_size = period * 2;
    snd_pcm_hw_params_set_buffer_size_near(pcm_, params, &buffer_size);

    if (snd_pcm_hw_params(pcm_, params) < 0) {
        snd_pcm_close(pcm_);
        return false;
    }

    running_.store(true);
    render_thread_ = std::thread(&ALSASoundBackend::render_loop, this);
    return true;
}
```

### Shutdown

```cpp
void ALSASoundBackend::shutdown() {
    if (!running_.load()) return;

    // 1. Signal thread to stop
    running_.store(false);

    // 2. Join render thread (it will exit after current writei completes)
    if (render_thread_.joinable())
        render_thread_.join();

    // 3. Drain remaining audio, then close device
    if (pcm_) {
        snd_pcm_drop(pcm_);   // Immediate stop (drain would block)
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}
```

Destructor calls `shutdown()` (same as SDL pattern). Order matters: thread must be joined before `snd_pcm_close`, otherwise `writei` segfaults.

### Audio Render Loop

```cpp
void ALSASoundBackend::render_loop() {
    // Allocate buffers once
    const size_t frame_count = period_size_;
    std::vector<float> mono_buf(frame_count);
    std::vector<float> stereo_buf;        // only if channels_ == 2
    std::vector<int16_t> s16_buf;         // only if use_s16_
    if (channels_ == 2) stereo_buf.resize(frame_count * 2);
    if (use_s16_) s16_buf.resize(frame_count * channels_);

    while (running_.load(std::memory_order_relaxed)) {
        float freq = current_freq_.load(std::memory_order_relaxed);
        float amp = current_amplitude_.load(std::memory_order_relaxed);
        float duty = current_duty_.load(std::memory_order_relaxed);
        Waveform wave = current_wave_.load(std::memory_order_relaxed);

        if (amp <= 0.001f || freq <= 0.0f) {
            std::memset(mono_buf.data(), 0, mono_buf.size() * sizeof(float));
        } else {
            helix::audio::generate_samples(mono_buf.data(), frame_count,
                                           sample_rate_, wave, freq, amp, duty, phase_);
            auto filter_type = current_filter_type_.load(std::memory_order_relaxed);
            if (filter_type != FilterType::NONE) {
                float cutoff = filter_cutoff_.load(std::memory_order_relaxed);
                helix::audio::recompute_filter_if_needed(filter_, filter_type, cutoff, sample_rate_);
                helix::audio::apply_filter(filter_, mono_buf.data(), frame_count);
            }
        }

        // Prepare write buffer (handle stereo duplication + S16 conversion)
        const void* write_ptr;
        if (channels_ == 2) {
            // Duplicate mono to stereo (L=R)
            for (size_t i = 0; i < frame_count; ++i) {
                stereo_buf[i * 2] = mono_buf[i];
                stereo_buf[i * 2 + 1] = mono_buf[i];
            }
        }
        const float* src = (channels_ == 2) ? stereo_buf.data() : mono_buf.data();

        if (use_s16_) {
            size_t total_samples = frame_count * channels_;
            for (size_t i = 0; i < total_samples; ++i) {
                // Clamp to [-1, 1] then scale to full int16 range
                float clamped = std::clamp(src[i], -1.0f, 1.0f);
                s16_buf[i] = static_cast<int16_t>(clamped * 32767.0f);
            }
            write_ptr = s16_buf.data();
        } else {
            write_ptr = src;
        }

        // Write with XRUN/suspend recovery
        snd_pcm_sframes_t frames = snd_pcm_writei(pcm_, write_ptr, frame_count);
        if (frames < 0) {
            frames = recover_xrun(frames);
            if (frames < 0) {
                spdlog::warn("[ALSASound] Write failed: {}", snd_strerror(frames));
                // Brief sleep to avoid busy-spin on persistent errors
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
}

snd_pcm_sframes_t ALSASoundBackend::recover_xrun(snd_pcm_sframes_t err) {
    if (err == -EPIPE) {
        // Buffer underrun
        spdlog::debug("[ALSASound] XRUN (underrun), recovering");
        int rc = snd_pcm_prepare(pcm_);
        return (rc < 0) ? rc : 0;
    } else if (err == -ESTRPIPE) {
        // Device suspended (power management)
        spdlog::debug("[ALSASound] Suspended, resuming");
        int rc;
        while ((rc = snd_pcm_resume(pcm_)) == -EAGAIN)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (rc < 0)
            rc = snd_pcm_prepare(pcm_);
        return rc;
    }
    return err;
}
```

### Auto-Detection Order

In `SoundManager::create_backend()`:

```
1. SDL          (desktop, HELIX_DISPLAY_SDL)
2. ALSA         (Linux with audio hardware, HELIX_HAS_ALSA)    ← NEW
3. PWM sysfs    (AD5M buzzer)
4. M300/Klipper (G-code beeper)
```

ALSA `initialize()` opens with `SND_PCM_NONBLOCK` to avoid hanging if the device is busy, then falls through to PWM/M300 on failure. On platforms without ALSA (AD5M, K1), the code isn't even compiled.

### Build System Changes

**Makefile** — new `HELIX_HAS_ALSA` define and link flag:

```makefile
# ALSA audio — available on Linux targets with libasound2
# Not available on: macOS (no ALSA), AD5M (no sound card / no libasound2),
# K1/MIPS (musl, no ALSA), K2 (musl, no ALSA)
ifneq (,$(filter pi pi-fbdev pi-both pi32 pi32-fbdev pi32-both x86 x86-fbdev x86-both snapmaker-u1,$(PLATFORM_TARGET)))
    ALSA_AVAILABLE := yes
endif
# Native Linux builds: check for libasound2-dev via pkg-config
ifeq ($(PLATFORM_TARGET),native)
    ifeq ($(UNAME_S),Linux)
        ALSA_PKG := $(shell pkg-config --exists alsa 2>/dev/null && echo "yes")
        ifeq ($(ALSA_PKG),yes)
            ALSA_AVAILABLE := yes
            ALSA_CFLAGS := $(shell pkg-config --cflags alsa)
            ALSA_LIBS := $(shell pkg-config --libs alsa)
        endif
    endif
endif
ifeq ($(ALSA_AVAILABLE),yes)
    CXXFLAGS += -DHELIX_HAS_ALSA $(ALSA_CFLAGS)
    LDFLAGS += $(if $(ALSA_LIBS),$(ALSA_LIBS),-lasound)
endif
```

**Dockerfile.pi** — add `libasound2-dev:arm64`:

```dockerfile
RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y --no-install-recommends \
    ...existing packages... \
    libasound2-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*
```

Same for `Dockerfile.pi32` (`libasound2-dev:armhf`) and `Dockerfile.snapmaker-u1`.

**Not added to:** `Dockerfile.ad5m` (no sound card), `Dockerfile.k1`/`Dockerfile.k2` (musl libc, no ALSA).

### Graceful Degradation

- **No audio device:** `snd_pcm_open` fails → falls through to PWM/M300
- **Device busy:** `SND_PCM_NONBLOCK` open returns `-EBUSY` → falls through
- **Buffer underrun (XRUN):** `snd_pcm_prepare()` recovery, log at debug level
- **Device suspended:** `snd_pcm_resume()` then `snd_pcm_prepare()`, handles power management
- **Device disconnected mid-playback:** `snd_pcm_writei` error → brief sleep to avoid spin, log warning
- **Persistent write errors:** 10ms sleep prevents busy-spin, logs warning

### Settings Integration

No settings changes needed. The ALSA backend is transparent to the user — it's just another backend that `SoundManager` auto-detects. The existing `sounds_enabled` and `ui_sounds_enabled` toggles control everything.

### Capabilities

| Feature | SDL | ALSA | PWM | M300 |
|---------|-----|------|-----|------|
| Waveforms | Yes | Yes | Approx | No |
| Amplitude | Yes | Yes | No | No |
| Filters | Yes | Yes | No | No |
| Polyphony-ready | Yes | Yes | No | No |
| Min tick | 1ms | 1ms | 1ms | ~50ms |

### Testing

**Unit tests** (`tests/unit/test_sound_synthesis.cpp`):
- Test the extracted `sound_synthesis.h` functions — runs everywhere, no ALSA/SDL needed
- Waveform generation: verify frequency, amplitude, phase continuity
- Biquad filter: verify coefficient computation, lowpass/highpass behavior
- S16 conversion: verify clamping at boundaries (±1.0, beyond ±1.0)

**ALSA backend tests** (`tests/unit/test_alsa_sound_backend.cpp`, `#ifdef HELIX_HAS_ALSA`):
- Mock `snd_pcm_*` calls for error recovery paths
- Test stereo duplication (mono→stereo frame expansion)
- Test XRUN recovery flow
- Test shutdown ordering (thread join before pcm_close)

**Integration test on Pi:**
- `ssh pi "speaker-test -t sine -f 440 -l 1"` verifies ALSA works at OS level
- Run `helix-screen --test -vv`, enable sounds in settings, press test beep button

### File Summary

| File | Action | Lines (est) |
|------|--------|-------------|
| `include/sound_synthesis.h` | New — extracted waveform/filter code + FilterType enum | ~70 |
| `src/system/sound_synthesis.cpp` | New — implementations | ~110 |
| `include/alsa_sound_backend.h` | New — ALSA backend header | ~70 |
| `src/system/alsa_sound_backend.cpp` | New — ALSA backend impl | ~200 |
| `src/system/sdl_sound_backend.cpp` | Modify — delegate to sound_synthesis | ~-80 |
| `include/sdl_sound_backend.h` | Modify — remove extracted statics, use FilterType enum | ~-20 |
| `src/system/sound_manager.cpp` | Modify — add ALSA to detection chain | ~15 |
| `Makefile` | Modify — ALSA conditional compilation | ~15 |
| `docker/Dockerfile.pi` | Modify — add libasound2-dev | ~1 |
| `docker/Dockerfile.pi32` | Modify — add libasound2-dev | ~1 |
| `docker/Dockerfile.snapmaker-u1` | Modify — add libasound2-dev | ~1 |
| `tests/unit/test_sound_synthesis.cpp` | New — synthesis unit tests | ~150 |
| `tests/unit/test_alsa_sound_backend.cpp` | New — ALSA backend tests | ~120 |

**Total: ~4 new files, ~7 modified files, ~750 lines**

## Non-Goals

- **Polyphony** — The ALSA backend is polyphony-ready (it generates sample buffers), but multi-voice support is a separate feature. This spec keeps the existing monophonic sequencer.
- **Volume control** — ALSA mixer volume is left at system default. The synth controls amplitude in software.
- **Device selection UI** — Always uses `"default"` ALSA device. Users can configure their default device via system tools (`alsamixer`, `asound.conf`).
- **PulseAudio/PipeWire** — ALSA direct access is sufficient and avoids dependency bloat.

## Also Included (In-Flight Work)

These items were already in progress before this design and will be included in the same branch but as separate commits:

1. **Startup sound** — `"startup"` sound definition added to all three themes (default, retro, minimal). Triggered after `SoundManager::initialize()` in `application.cpp`.

2. **Sound gap audit** — Identified missing sound triggers for: modals, keyboard/numpad, sliders, connection events, overlays, `dropdown_open` (defined but unused), `alarm_urgent` (defined but unused). These will be wired up incrementally as follow-up work.

3. **Jan Hammer tribute themes** — `crocketts_theme.json` and `miami_vice.json` theme files with all standard sounds plus iconic melody approximations as startup sounds.
