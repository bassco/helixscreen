# ALSA Sound Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add real waveform audio to Raspberry Pi and other Linux SBCs via ALSA, replacing the M300 beeper as primary sound output on devices with audio hardware.

**Architecture:** Extract shared synthesis code from SDL backend into `sound_synthesis.h`, create `ALSASoundBackend` that uses ALSA PCM for output, add to auto-detection chain between SDL and PWM. Also add startup sounds to all themes and create two Jan Hammer tribute themes.

**Tech Stack:** ALSA (libasound2), C++17, Catch2 (tests), Make build system, Docker cross-compilation

**Spec:** `docs/superpowers/specs/2026-03-25-alsa-sound-backend-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `include/sound_synthesis.h` | Create | Shared waveform generation, BiquadFilter, FilterType enum |
| `src/system/sound_synthesis.cpp` | Create | Implementations of synthesis functions |
| `include/sdl_sound_backend.h` | Modify | Remove extracted statics, delegate to sound_synthesis |
| `src/system/sdl_sound_backend.cpp` | Modify | Delegate to sound_synthesis, use FilterType enum |
| `include/alsa_sound_backend.h` | Create | ALSA backend header (`#ifdef HELIX_HAS_ALSA`) |
| `src/system/alsa_sound_backend.cpp` | Create | ALSA backend implementation |
| `src/system/sound_manager.cpp` | Modify | Add ALSA to auto-detection chain |
| `Makefile` | Modify | ALSA conditional compilation flags |
| `docker/Dockerfile.pi` | Modify | Add `libasound2-dev:arm64` |
| `docker/Dockerfile.pi32` | Modify | Add `libasound2-dev:armhf` |
| ~~`docker/Dockerfile.snapmaker-u1`~~ | Skip | No `PLATFORM_TARGET=snapmaker-u1` in Makefile yet |
| `tests/unit/test_sound_synthesis.cpp` | Create | Synthesis unit tests |
| `tests/unit/test_alsa_sound_backend.cpp` | Create | ALSA backend unit tests |
| `config/sounds/default.json` | Already done | Startup sound added |
| `config/sounds/retro.json` | Already done | Startup sound added |
| `config/sounds/minimal.json` | Already done | Startup sound added |
| `config/sounds/crocketts_theme.json` | Create | Jan Hammer tribute theme |
| `config/sounds/miami_vice.json` | Create | Jan Hammer tribute theme |
| `src/application/application.cpp` | Modify | Trigger startup sound |

---

### Task 1: Extract `sound_synthesis.h` from SDL backend

This is a pure refactor — move the platform-independent synthesis math out of `SDLSoundBackend` into shared code. No behavior change. Everything must compile and all existing tests must pass after this step.

**Files:**
- Create: `include/sound_synthesis.h`
- Create: `src/system/sound_synthesis.cpp`
- Modify: `include/sdl_sound_backend.h` (remove extracted statics)
- Modify: `src/system/sdl_sound_backend.cpp` (delegate to sound_synthesis)

**Key context:**
- `SDLSoundBackend::generate_samples()` is declared `static` and public in `include/sdl_sound_backend.h:49-50`
- `SDLSoundBackend::BiquadFilter` struct is at `include/sdl_sound_backend.h:53-60`
- `SDLSoundBackend::compute_biquad_coeffs()` at `include/sdl_sound_backend.h:63-64`
- `SDLSoundBackend::apply_filter()` at `include/sdl_sound_backend.h:67`
- Implementations are in `src/system/sdl_sound_backend.cpp:99-185`
- All of this code is inside `#ifdef HELIX_DISPLAY_SDL` — we need it available without SDL
- The existing `filter_type_` is `std::string` (data race) — fix to `FilterType` enum during extraction

- [ ] **Step 1: Create `include/sound_synthesis.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sound_theme.h" // Waveform enum

#include <atomic>
#include <string>

namespace helix::audio {

/// Filter type enum — replaces std::string to eliminate data race between
/// sequencer thread (writer) and audio render thread (reader).
enum class FilterType : int { NONE = 0, LOWPASS = 1, HIGHPASS = 2 };

/// Convert string to FilterType (for theme JSON parsing)
FilterType filter_type_from_string(const std::string& type);

/// Biquad filter state (Direct Form II Transposed)
struct BiquadFilter {
    float b0 = 1, b1 = 0, b2 = 0;
    float a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    bool active = false;
    FilterType current_type = FilterType::NONE;
    float current_cutoff = 0;
    float current_sample_rate = 0;
};

/// Generate waveform samples into buffer.
/// @param phase Modified in-place for continuity across calls.
void generate_samples(float* buffer, int num_samples, int sample_rate,
                      Waveform wave, float freq, float amplitude,
                      float duty_cycle, float& phase);

/// Compute biquad coefficients for lowpass or highpass.
void compute_biquad_coeffs(BiquadFilter& f, FilterType type, float cutoff,
                           float sample_rate);

/// Apply filter to buffer in-place.
void apply_filter(BiquadFilter& f, float* buffer, int num_samples);

/// Recompute filter coefficients only if parameters changed (avoids per-frame recompute).
void update_filter_if_needed(BiquadFilter& f, FilterType type, float cutoff,
                             float sample_rate);

} // namespace helix::audio
```

- [ ] **Step 2: Create `src/system/sound_synthesis.cpp`**

Move the implementations from `src/system/sdl_sound_backend.cpp` lines 99-185 into this file. The functions are identical — just change the namespace from `SDLSoundBackend::` to `helix::audio::`. Add `filter_type_from_string()` and `update_filter_if_needed()`. The `compute_biquad_coeffs` now takes `FilterType` enum instead of `const std::string&`.

Key differences from SDL version:
- `compute_biquad_coeffs` takes `FilterType` enum, not string
- `update_filter_if_needed` checks if params changed before recomputing (optimization for ALSA render loop)
- `filter_type_from_string` converts `"lowpass"`/`"highpass"` to enum
- `#include <spdlog/spdlog.h>` for the unknown-filter-type warning

- [ ] **Step 3: Update `include/sdl_sound_backend.h`**

Remove the extracted statics and struct. Keep the SDL-specific members. The class should delegate to `helix::audio::` functions.

Remove from `SDLSoundBackend`:
- `struct BiquadFilter` (lines 53-60) — now in `sound_synthesis.h`
- `static void generate_samples(...)` declaration (lines 49-50)
- `static void compute_biquad_coeffs(...)` declaration (lines 63-64)
- `static void apply_filter(...)` declaration (lines 67)

Replace member `BiquadFilter filter_` with `helix::audio::BiquadFilter filter_`
Replace member `std::string filter_type_` with `std::atomic<helix::audio::FilterType> filter_type_{helix::audio::FilterType::NONE}`

Add `#include "sound_synthesis.h"` at top.

- [ ] **Step 4: Update `src/system/sdl_sound_backend.cpp`**

- Remove the implementations of `generate_samples`, `compute_biquad_coeffs`, `apply_filter` (lines 99-185)
- Update `set_filter()` to convert string → FilterType enum and store atomically
- Update `audio_callback()` to call `helix::audio::generate_samples()` and `helix::audio::apply_filter()`
- The `#ifndef M_PI` define can be removed (move to sound_synthesis.cpp)

Key changes in `set_filter()`:
```cpp
void SDLSoundBackend::set_filter(const std::string& type, float cutoff) {
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
```

Key changes in `audio_callback()`:
```cpp
helix::audio::generate_samples(out, num_samples, self->sample_rate_, wave, freq, amp, duty, self->phase_);
if (self->filter_type_.load(std::memory_order_acquire) != helix::audio::FilterType::NONE) {
    helix::audio::apply_filter(self->filter_, out, num_samples);
}
```

- [ ] **Step 5: Update existing SDL sound test**

The test file `tests/unit/test_sdl_sound_backend.cpp` calls `SDLSoundBackend::generate_samples()` and related statics directly. Update these calls to use `helix::audio::generate_samples()` etc. The test file is gated behind `#ifdef HELIX_DISPLAY_SDL` — leave that gate in place for now (the SDL-specific tests need SDL).

Search for all `SDLSoundBackend::generate_samples`, `SDLSoundBackend::BiquadFilter`, `SDLSoundBackend::compute_biquad_coeffs`, `SDLSoundBackend::apply_filter` in the test file and replace with `helix::audio::` equivalents. Add `#include "sound_synthesis.h"`.

- [ ] **Step 6: Build and run tests**

Run: `make -j && make test-run`
Expected: All tests pass, no compilation errors. The extraction is a pure refactor — behavior is identical.

- [ ] **Step 7: Commit**

```
git add include/sound_synthesis.h src/system/sound_synthesis.cpp \
    include/sdl_sound_backend.h src/system/sdl_sound_backend.cpp \
    tests/unit/test_sdl_sound_backend.cpp
git commit -m "refactor(sound): extract synthesis code from SDL backend into sound_synthesis.h"
```

---

### Task 2: Write synthesis unit tests (no SDL/ALSA dependency)

Now that synthesis is in its own file, write tests that run on ALL platforms — no `#ifdef` gates needed.

**Files:**
- Create: `tests/unit/test_sound_synthesis.cpp`

- [ ] **Step 1: Create `tests/unit/test_sound_synthesis.cpp`**

Test the extracted functions. These tests must NOT be gated behind any `#ifdef`. Key test cases:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_synthesis.h"
#include <cmath>
#include <vector>
#include "../catch_amalgamated.hpp"

using namespace helix::audio;
using Catch::Approx;

static constexpr int SAMPLE_RATE = 44100;
static constexpr int SAMPLES_10MS = 441;

// Helper: compute RMS of buffer
static float compute_rms(const float* buf, int n) {
    float sum = 0;
    for (int i = 0; i < n; i++) sum += buf[i] * buf[i];
    return std::sqrt(sum / n);
}
```

Test cases to include:
- **Square wave frequency**: Generate 1kHz square at 0.5 duty, count zero crossings, verify frequency within 2%
- **Square wave duty cycle**: Generate at duty=0.25, measure time-above-zero vs time-below-zero ratio
- **Sine wave RMS**: Generate sine at amplitude 1.0, verify RMS ≈ 0.707 (1/√2)
- **Triangle wave symmetry**: Generate triangle, verify peak positive ≈ amplitude, peak negative ≈ -amplitude
- **Saw wave range**: Generate saw at amplitude 0.8, verify min/max within [-0.8, 0.8]
- **Phase continuity**: Generate 100 samples, then 100 more with same phase ref — verify no discontinuity at boundary
- **Silence at zero amplitude**: Generate with amplitude=0, verify all zeros
- **FilterType from string**: Verify `"lowpass"` → LOWPASS, `"highpass"` → HIGHPASS, `""` → NONE, `"unknown"` → LOWPASS (default)
- **Lowpass filter**: Generate white noise (random samples), apply lowpass at 1kHz, verify high-frequency energy reduced
- **Highpass filter**: Generate white noise, apply highpass at 1kHz, verify low-frequency energy reduced
- **Filter coefficient stability**: Verify `update_filter_if_needed` doesn't recompute when called with same params (check that `current_cutoff` matches)
- **S16 clamping helper** (if exposed, otherwise test in ALSA test): Verify float 1.5 clamps to 32767, float -1.5 clamps to -32767

- [ ] **Step 2: Build and run tests**

Run: `make test-run`
Expected: All new synthesis tests pass alongside existing tests.

- [ ] **Step 3: Commit**

```
git add tests/unit/test_sound_synthesis.cpp
git commit -m "test(sound): add platform-independent synthesis unit tests"
```

---

### Task 3: Build system — ALSA conditional compilation

**Files:**
- Modify: `Makefile` (add ALSA detection + flags)
- Modify: `docker/Dockerfile.pi` (add libasound2-dev:arm64)
- Modify: `docker/Dockerfile.pi32` (add libasound2-dev:armhf)

Note: `Dockerfile.snapmaker-u1` skipped — no `PLATFORM_TARGET=snapmaker-u1` exists in the Makefile yet. Add ALSA when that platform target is implemented.

Note: New `.cpp` files (`sound_synthesis.cpp`, `alsa_sound_backend.cpp`) are auto-discovered by the Makefile's `$(wildcard $(SRC_DIR)/*/*.cpp)` glob at line ~303. No explicit Makefile source list changes needed.

- [ ] **Step 1: Add ALSA detection to Makefile**

Insert after line 644 (`CXXFLAGS += $(SYSTEMD_CXXFLAGS)`):

```makefile
# ALSA audio backend — real waveform synthesis on Linux targets with libasound2
# Enabled for: Pi (all variants), x86 SBCs
# Not available on: macOS (no ALSA), AD5M (no sound card), K1/K2/MIPS (musl, no ALSA)
ALSA_LIBS :=
ALSA_CXXFLAGS :=
ifneq (,$(filter pi pi-fbdev pi-both pi32 pi32-fbdev pi32-both x86 x86-fbdev x86-both,$(PLATFORM_TARGET)))
    ALSA_CXXFLAGS := -DHELIX_HAS_ALSA
    ALSA_LIBS := -lasound
else ifeq ($(PLATFORM_TARGET),native)
    ifeq ($(UNAME_S),Linux)
        ALSA_PKG := $(shell pkg-config --exists alsa 2>/dev/null && echo "yes")
        ifeq ($(ALSA_PKG),yes)
            ALSA_CXXFLAGS := -DHELIX_HAS_ALSA $(shell pkg-config --cflags alsa 2>/dev/null)
            ALSA_LIBS := $(shell pkg-config --libs alsa 2>/dev/null)
        endif
    endif
endif
CXXFLAGS += $(ALSA_CXXFLAGS)
LDFLAGS += $(ALSA_LIBS)
```

The `LDFLAGS +=` append works because it runs after all the platform-specific `LDFLAGS :=` assignment blocks. When `ALSA_LIBS` is empty (macOS, AD5M, K1, K2), the append is a no-op.

- [ ] **Step 2: Add libasound2-dev to Docker toolchains**

**`docker/Dockerfile.pi`** line 72: Add `libasound2-dev:arm64 \` to the `apt-get install` block, after `libbluetooth-dev:arm64`:
```dockerfile
    libbluetooth-dev:arm64 \
    libasound2-dev:arm64 \
```

**`docker/Dockerfile.pi32`**: Add `libasound2-dev:armhf \` to the equivalent block after `libbluetooth-dev:armhf`.

- [ ] **Step 3: Verify native build compiles with ALSA flag**

Run: `make -j`
Expected: Compiles successfully. On Linux desktop with libasound2-dev installed, you should see `-DHELIX_HAS_ALSA` in compile output and `-lasound` in link. On macOS, neither flag appears.

Verify: `make -j 2>&1 | grep -c HELIX_HAS_ALSA` — should be >0 on Linux, 0 on macOS.

- [ ] **Step 4: Commit**

```
git add Makefile docker/Dockerfile.pi docker/Dockerfile.pi32
git commit -m "build(sound): add ALSA conditional compilation for Pi/x86 targets"
```

---

### Task 4: Implement `ALSASoundBackend`

**Files:**
- Create: `include/alsa_sound_backend.h`
- Create: `src/system/alsa_sound_backend.cpp`

**Key reference:** The spec's pseudocode at `docs/superpowers/specs/2026-03-25-alsa-sound-backend-design.md` — sections "ALSA Setup", "Shutdown", and "Audio Render Loop". Follow the spec closely but adapt to actual code patterns.

**Pattern reference:** Study `include/sdl_sound_backend.h` and `src/system/sdl_sound_backend.cpp` for the member variable pattern (atomics for tone params, phase accumulator, filter state).

- [ ] **Step 1: Create `include/alsa_sound_backend.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef HELIX_HAS_ALSA

#include "sound_backend.h"
#include "sound_synthesis.h"

#include <alsa/asoundlib.h>
#include <atomic>
#include <string>
#include <thread>

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

  private:
    void render_loop();
    snd_pcm_sframes_t recover_xrun(snd_pcm_sframes_t err);

    snd_pcm_t* pcm_ = nullptr;
    std::thread render_thread_;
    std::atomic<bool> running_{false};

    // Tone parameters (written by sequencer thread, read by render thread)
    std::atomic<float> current_freq_{0};
    std::atomic<float> current_amplitude_{0};
    std::atomic<float> current_duty_{0.5f};
    std::atomic<Waveform> current_wave_{Waveform::SQUARE};

    // Filter parameters
    std::atomic<helix::audio::FilterType> filter_type_{helix::audio::FilterType::NONE};
    std::atomic<float> filter_cutoff_{20000.0f};

    // Render thread state (only accessed from render thread)
    float phase_ = 0;
    helix::audio::BiquadFilter filter_;

    // Audio format negotiated during initialize()
    unsigned int sample_rate_ = 44100;
    snd_pcm_uframes_t period_size_ = 256;
    unsigned int channels_ = 1;
    bool use_s16_ = false;
};

#endif // HELIX_HAS_ALSA
```

- [ ] **Step 2: Create `src/system/alsa_sound_backend.cpp`**

Implement following the spec pseudocode. Key sections:

**Constructor/Destructor:** Constructor default, destructor calls `shutdown()`.

**`initialize()`:** Follow spec — open with `SND_PCM_NONBLOCK`, switch to blocking, negotiate format (float→s16 fallback), channels (mono→stereo fallback), rate, period size, buffer size (2x period). Start render thread.

**`shutdown()`:** Set `running_=false`, join thread, `snd_pcm_drop()`, `snd_pcm_close()`. Order matters — join before close.

**`set_tone()` / `silence()` / `set_waveform()`:** Atomic stores (same pattern as SDL).

**`set_filter()`:** Convert string → `FilterType` enum via `helix::audio::filter_type_from_string()`, store atomically.

**`render_loop()`:** Follow spec — allocate buffers once, loop on `running_`, generate mono samples via `helix::audio::generate_samples()`, apply filter via `helix::audio::update_filter_if_needed()` + `helix::audio::apply_filter()`, duplicate to stereo if needed, convert to S16 with clamping if needed, write via `snd_pcm_writei()` with XRUN recovery. Sleep 10ms on persistent errors to avoid busy-spin.

**`recover_xrun()`:** Handle `-EPIPE` (underrun → prepare) and `-ESTRPIPE` (suspend → resume+prepare). Follow spec.

**Static helpers for testability:** Extract these as `static` public methods on `ALSASoundBackend` so Task 6 tests can call them directly without needing ALSA hardware:

```cpp
/// Duplicate mono buffer to interleaved stereo (L=R).
static void mono_to_stereo(const float* mono, float* stereo, size_t frame_count);

/// Convert float [-1,1] samples to int16 with clamping. Handles both mono and stereo.
static void float_to_s16(const float* src, int16_t* dst, size_t sample_count);
```

Log tags: `[ALSASound]` matching existing convention.

- [ ] **Step 3: Build**

Run: `make -j`
Expected: Compiles. On Linux with ALSA headers, the new backend is compiled. On macOS, it's excluded.

- [ ] **Step 4: Commit**

```
git add include/alsa_sound_backend.h src/system/alsa_sound_backend.cpp
git commit -m "feat(sound): add ALSA PCM backend for Pi/Linux audio output"
```

---

### Task 5: Wire ALSA into SoundManager auto-detection

**Files:**
- Modify: `src/system/sound_manager.cpp`

- [ ] **Step 1: Add ALSA to the detection chain**

In `src/system/sound_manager.cpp`, add the include at the top (conditionally):

```cpp
#ifdef HELIX_HAS_ALSA
#include "alsa_sound_backend.h"
#endif
```

In `SoundManager::create_backend()`, insert ALSA detection between the SDL block (line 180) and the PWM block (line 182). Follow the existing pattern:

```cpp
#ifdef HELIX_HAS_ALSA
    auto alsa_backend = std::make_shared<ALSASoundBackend>();
    if (alsa_backend->initialize()) {
        spdlog::info("[SoundManager] Using ALSA PCM backend");
        return alsa_backend;
    }
    spdlog::debug("[SoundManager] ALSA not available, falling back");
#endif
```

Insert this right after the `#endif` for `HELIX_DISPLAY_SDL` (after line 180) and before the PWM backend creation (line 182).

- [ ] **Step 2: Build and test**

Run: `make -j && make test-run`
Expected: All tests pass. On desktop Linux with ALSA, the sound manager now detects and prefers ALSA. On macOS/desktop without ALSA, falls through to SDL as before.

- [ ] **Step 3: Commit**

```
git add src/system/sound_manager.cpp
git commit -m "feat(sound): add ALSA backend to SoundManager auto-detection chain"
```

---

### Task 6: ALSA backend unit tests

**Files:**
- Create: `tests/unit/test_alsa_sound_backend.cpp`

- [ ] **Step 1: Create test file**

Gate the entire file behind `#ifdef HELIX_HAS_ALSA`. Test what we can without actual ALSA hardware:

Key test cases (all use the static helpers added in Task 4):
- **Stereo duplication**: Call `ALSASoundBackend::mono_to_stereo()` with a known mono buffer, verify stereo output has L=R for each frame
- **S16 conversion — normal range**: Call `ALSASoundBackend::float_to_s16()` with values in [-1, 1], verify correct scaling
- **S16 conversion — clamping**: Verify float 1.5 → 32767, float -1.5 → -32767, float 0.0 → 0
- **S16 conversion — boundary**: Verify float 1.0 → 32767, float -1.0 → -32767
- **set_tone / silence atomic behavior**: Create backend (don't call initialize), call `set_tone()`, verify state via `is_playing`-like checks. Call `silence()`, verify amplitude → 0.
- **set_filter type conversion**: Call `set_filter("lowpass", 1000)`, call `set_filter("", 0)`.
- **set_waveform**: Verify atomic store round-trip for each waveform type

Note: Testing actual ALSA PCM playback requires hardware — that's for integration testing on the Pi (Task 9). The unit tests cover the logic paths that are testable without hardware.

- [ ] **Step 2: Build and run tests**

Run: `make test-run`
Expected: ALSA tests pass on Linux, skipped on macOS (entire file excluded by `#ifdef`).

- [ ] **Step 3: Commit**

```
git add tests/unit/test_alsa_sound_backend.cpp
git commit -m "test(sound): add ALSA backend unit tests"
```

---

### Task 7: Startup sound trigger

**Files:**
- Modify: `src/application/application.cpp`

**Context:** The startup sounds are already added to all three theme JSON files (default.json, retro.json, minimal.json) from earlier in this session. This task wires up the trigger.

- [ ] **Step 1: Add startup sound call**

In `src/application/application.cpp`, the `if` block and `initialize()` call already exist at lines 477-479:
```cpp
    if (Config::get_instance()->is_beta_features_enabled()) {
        SoundManager::instance().initialize();
    }
```

Add the `play()` call after `initialize()`, inside the existing `if` block (line 479, before the closing `}`):

```cpp
        SoundManager::instance().initialize();
        // Play startup chime (non-blocking, sequencer handles playback)
        SoundManager::instance().play("startup", SoundPriority::EVENT);
```

The `play()` call is non-blocking — it queues the sound and returns immediately. Using `EVENT` priority so it's not gated by `ui_sounds_enabled` toggle (startup is an event, not a UI interaction).

`SoundPriority` is already available — it's defined in `sound_sequencer.h` which is included by `sound_manager.h`, and `application.cpp` already includes `sound_manager.h`.

- [ ] **Step 2: Build**

Run: `make -j`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```
git add src/application/application.cpp
git commit -m "feat(sound): play startup chime after SoundManager initialization"
```

---

### Task 8: Jan Hammer tribute themes

**Files:**
- Create: `config/sounds/crocketts_theme.json`
- Create: `config/sounds/miami_vice.json`

These are fun easter egg themes with all standard UI sounds styled in an 80s synth aesthetic, plus recognizable melody approximations as the startup sound.

- [ ] **Step 1: Create `config/sounds/crocketts_theme.json`**

Full theme with all standard sounds. Style: warm saw waves, slow attacks, filter sweeps — Jan Hammer's signature smooth synth tone. The startup sound should be a recognizable approximation of the Crockett's Theme melody (the iconic descending synth line). Key of Bb minor, ~100 BPM.

All sounds needed: `button_tap`, `toggle_on`, `toggle_off`, `nav_forward`, `nav_back`, `dropdown_open`, `print_complete`, `print_cancelled`, `error_alert`, `error_tone`, `alarm_urgent`, `test_beep`, `startup`.

Use `"wave": "saw"` as the default waveform with filter sweeps for the 80s analog synth feel. Slower envelopes than retro theme (longer sustain, longer release).

The startup melody (Crockett's Theme main hook, simplified monophonic):
- Bb4 (dotted quarter) → Ab4 (eighth) → Gb4 (quarter) → F4 (half, with vibrato)
- Then ascending: Gb4 → Ab4 → Bb4 (quarter) → Db5 (half, with filter sweep)
- Final note: F5 (whole, slow fade with LFO vibrato)

- [ ] **Step 2: Create `config/sounds/miami_vice.json`**

Style: punchy, electronic, more aggressive than Crockett's. Shorter envelopes, staccato hits, saw + square mix. The startup should approximate the Miami Vice main theme synth lead.

The startup melody (Miami Vice theme, simplified monophonic):
The recognizable opening staccato riff in E minor:
- E4 staccato hits (16th notes, 3x with pauses) → then G4 → A4 → B4 ascending
- Lead melody: B4 → D5 → E5 → D5 → B4 → G4 (longer notes with filter bloom)

- [ ] **Step 3: Verify themes load**

Run the app in test mode and check logs for theme loading:
```bash
./build/bin/helix-screen --test -vv 2>&1 | grep -i "theme\|sound"
```
Themes should appear in `get_available_themes()` output.

- [ ] **Step 4: Commit**

```
git add config/sounds/crocketts_theme.json config/sounds/miami_vice.json
git commit -m "feat(sound): add Crockett's Theme and Miami Vice tribute sound themes"
```

---

### Task 9: Integration test on Pi (manual)

This task requires SSH access to a Pi with HDMI audio output.

- [ ] **Step 1: Rebuild Docker Pi image**

```bash
docker build -t helixscreen/toolchain-pi -f docker/Dockerfile.pi docker/
```

Verify `libasound2-dev:arm64` installed successfully in the image.

- [ ] **Step 2: Cross-compile for Pi**

```bash
make pi-docker
```

Expected: Binary compiles with `-DHELIX_HAS_ALSA` and `-lasound`.

- [ ] **Step 3: Deploy and test on Pi**

```bash
PI_HOST=192.168.1.113 make deploy-pi
```

SSH to Pi and run:
```bash
# First verify ALSA works at OS level
speaker-test -t sine -f 440 -l 1 -D hw:0,0

# Run HelixScreen with sound enabled
./helixscreen --test -vv 2>&1 | grep -i "alsa\|sound\|backend"
```

Expected log output should show:
```
[SoundManager] Using ALSA PCM backend
[SoundManager] Initialized with theme 'default', backend ready
[SoundManager] play('startup', priority=1)
```

If HDMI has speakers: you should hear the startup chime. If no speakers: logs confirm the backend initialized (graceful — no crash, no error).

- [ ] **Step 4: Test on CB1 (192.168.1.112)**

Same procedure. The CB1 has the H616 analog codec (card 2) — sound may actually play through the onboard audio output if a speaker is connected.

---

### Task 10: Final build verification

- [ ] **Step 1: Full native build + test**

```bash
make -j && make test-run
```

Expected: All tests pass, including new synthesis and ALSA tests.

- [ ] **Step 2: Verify no regressions in existing sound tests**

```bash
./build/bin/helix-tests "[sound]" -v
```

(Or whatever tag groups the sound tests — check existing test tags.)

- [ ] **Step 3: Final commit (if any cleanup needed)**

Only if previous tasks left unstaged changes. Otherwise this is a verification-only step.
