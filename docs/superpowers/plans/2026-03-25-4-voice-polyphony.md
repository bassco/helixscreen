# 4-Voice Polyphony Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 4-voice polyphonic chord support to the sound system, enabling themes to play chords via a `"notes"` array in step definitions.

**Architecture:** Backends gain a voice slot interface (default 1 voice, SDL/ALSA override to 4). The sequencer assigns chord notes to voice slots. JSON theme format adds backward-compatible `"notes"` array. PWM/M300 gracefully degrade to root note.

**Tech Stack:** C++17, Catch2 (tests), existing sound_synthesis.h for per-voice sample generation

**Spec:** `docs/superpowers/specs/2026-03-25-4-voice-polyphony-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `include/sound_backend.h` | Modify | Add virtual voice methods with defaults |
| `include/sound_theme.h` | Modify | Add `chord_freqs`/`chord_count` to SoundStep |
| `src/system/sound_theme.cpp` | Modify | Parse `"notes"` array in steps |
| `include/sdl_sound_backend.h` | Modify | VoiceState array, voice_buf/mix_buf, overrides |
| `src/system/sdl_sound_backend.cpp` | Modify | Multi-voice render loop |
| `include/alsa_sound_backend.h` | Modify | VoiceState array, mix_buf, overrides |
| `src/system/alsa_sound_backend.cpp` | Modify | Multi-voice render loop |
| `src/system/sound_sequencer.cpp` | Modify | Voice assignment in tick(), apply_step helper |
| `include/sound_sequencer.h` | Modify | Add apply_step declaration |
| `tests/unit/test_sound_polyphony.cpp` | Create | Polyphony tests |

---

### Task 1: Add voice methods to SoundBackend interface

Small, safe change — adds virtual methods with backward-compatible defaults. All existing backends continue working unchanged.

**Files:**
- Modify: `include/sound_backend.h`

- [ ] **Step 1: Add voice methods to SoundBackend**

After `set_filter()` (line 54) and before `min_tick_ms()` (line 57), add:

```cpp
    /// Set a specific voice slot (0-based). Default: slot 0 maps to set_tone().
    /// @param slot Voice slot index (0 to voice_count()-1)
    virtual void set_voice(int slot, float freq_hz, float amplitude, float duty_cycle) {
        if (slot == 0) set_tone(freq_hz, amplitude, duty_cycle);
    }

    /// Set waveform for a specific voice slot. Default: slot 0 maps to set_waveform().
    virtual void set_voice_waveform(int slot, Waveform w) {
        if (slot == 0) set_waveform(w);
    }

    /// Silence a specific voice slot. Default: slot 0 maps to silence().
    virtual void silence_voice(int slot) {
        if (slot == 0) silence();
    }

    /// Number of independent voice slots. Default: 1 (monophonic).
    virtual int voice_count() const { return 1; }
```

- [ ] **Step 2: Build and test**

```bash
make -j && make test-run
```
Expected: All tests pass — default implementations mean no behavioral change.

- [ ] **Step 3: Commit**

```bash
git add include/sound_backend.h
git commit -m "feat(sound): add polyphonic voice slot interface to SoundBackend"
```

---

### Task 2: Add chord fields to SoundStep + theme parser

Add `chord_freqs`/`chord_count` to the step struct and parse `"notes"` arrays from JSON.

**Files:**
- Modify: `include/sound_theme.h`
- Modify: `src/system/sound_theme.cpp`

- [ ] **Step 1: Add chord fields to SoundStep**

In `include/sound_theme.h`, add `#include <array>` at the top (after `#include <optional>`), then add after `bool is_pause = false;` (line 52):

```cpp
    // Polyphonic chord (empty = monophonic, use freq_hz on voice 0)
    std::array<float, 4> chord_freqs{};
    uint8_t chord_count = 0;  // 0 = monophonic
```

- [ ] **Step 2: Parse "notes" array in theme parser**

In `src/system/sound_theme.cpp`, in the `parse_step()` function, after the frequency parsing block (after line 144 `step.freq_hz = clamp_freq(...)`), add:

```cpp
    // Polyphonic: "notes" array takes priority over single "note"/"freq"
    if (j.contains("notes") && j["notes"].is_array()) {
        auto& notes_arr = j["notes"];
        step.chord_count = static_cast<uint8_t>(std::min(notes_arr.size(), size_t(4)));
        for (uint8_t i = 0; i < step.chord_count; ++i) {
            if (notes_arr[i].is_string()) {
                step.chord_freqs[i] = SoundThemeParser::note_to_freq(notes_arr[i].get<std::string>());
            } else if (notes_arr[i].is_number()) {
                step.chord_freqs[i] = clamp_freq(notes_arr[i].get<float>());
            }
        }
        // Also set freq_hz to root note for mono backends
        if (step.chord_count > 0) {
            step.freq_hz = step.chord_freqs[0];
        }
    }
```

- [ ] **Step 3: Build and test**

```bash
make -j && make test-run
```
Expected: All tests pass. No behavioral change yet (sequencer doesn't use chord_freqs).

- [ ] **Step 4: Commit**

```bash
git add include/sound_theme.h src/system/sound_theme.cpp
git commit -m "feat(sound): add chord notation parsing to SoundStep and theme parser"
```

---

### Task 3: SDL backend — 4-voice polyphony

Replace SDL's single-voice atomics with a VoiceState array and multi-voice render loop.

**Files:**
- Modify: `include/sdl_sound_backend.h`
- Modify: `src/system/sdl_sound_backend.cpp`

- [ ] **Step 1: Update SDL header with VoiceState and voice methods**

In `include/sdl_sound_backend.h`:

Add `#include <vector>` to the includes.

Add the VoiceState struct and voice method overrides. Replace the single-voice atomics (lines 49-53: `current_freq_`, `current_amplitude_`, `current_duty_`, `current_wave_`) with a VoiceState array. Keep `filter_type_` and `filter_cutoff_` as-is (shared filter).

Replace the private section (lines 46-67) with:

```cpp
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

    // Scratch buffers for multi-voice mixing (allocated in initialize())
    std::vector<float> voice_buf_;
    std::vector<float> mix_buf_;

    SDL_AudioDeviceID device_id_ = 0;
    int sample_rate_ = 44100;
    bool initialized_ = false;
```

Add public overrides after `shutdown()`:

```cpp
    // Polyphonic voice interface
    void set_voice(int slot, float freq_hz, float amplitude, float duty_cycle) override;
    void set_voice_waveform(int slot, Waveform w) override;
    void silence_voice(int slot) override;
    int voice_count() const override { return MAX_VOICES; }
```

- [ ] **Step 2: Update SDL implementation**

In `src/system/sdl_sound_backend.cpp`:

**`initialize()`**: After `SDL_PauseAudioDevice(device_id_, 0)`, add buffer allocation:
```cpp
    voice_buf_.resize(obtained.samples);
    mix_buf_.resize(obtained.samples);
```

**`set_tone()`**: Change to delegate to voice 0:
```cpp
void SDLSoundBackend::set_tone(float freq_hz, float amplitude, float duty_cycle) {
    set_voice(0, freq_hz, amplitude, duty_cycle);
}
```

**`silence()`**: Silence all voices:
```cpp
void SDLSoundBackend::silence() {
    for (int v = 0; v < MAX_VOICES; ++v)
        voices_[v].amplitude.store(0, std::memory_order_relaxed);
}
```

**`set_waveform()`**: Delegate to voice 0:
```cpp
void SDLSoundBackend::set_waveform(Waveform w) {
    set_voice_waveform(0, w);
}
```

**New voice methods:**
```cpp
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
```

**`audio_callback()`**: Replace the single-voice render with multi-voice mixing:
```cpp
void SDLSoundBackend::audio_callback(void* userdata, uint8_t* stream, int len) {
    auto* self = static_cast<SDLSoundBackend*>(userdata);
    auto* out = reinterpret_cast<float*>(stream);
    int num_samples = len / static_cast<int>(sizeof(float));

    std::atomic_thread_fence(std::memory_order_acquire);

    // Check if any voice is active
    bool any_active = false;
    for (int v = 0; v < MAX_VOICES; ++v) {
        if (self->voices_[v].amplitude.load(std::memory_order_relaxed) > 0.001f) {
            any_active = true;
            break;
        }
    }
    if (!any_active) {
        std::memset(stream, 0, static_cast<size_t>(len));
        return;
    }

    // Mix all active voices
    std::memset(self->mix_buf_.data(), 0, num_samples * sizeof(float));
    for (int v = 0; v < MAX_VOICES; ++v) {
        float amp = self->voices_[v].amplitude.load(std::memory_order_relaxed);
        if (amp <= 0.001f) {
            self->voices_[v].phase = 0;
            continue;
        }
        float freq = self->voices_[v].freq.load(std::memory_order_relaxed);
        if (freq <= 0.0f) continue;

        helix::audio::generate_samples(
            self->voice_buf_.data(), num_samples, self->sample_rate_,
            self->voices_[v].wave.load(std::memory_order_relaxed),
            freq, amp,
            self->voices_[v].duty.load(std::memory_order_relaxed),
            self->voices_[v].phase);

        for (int i = 0; i < num_samples; ++i)
            self->mix_buf_[i] += self->voice_buf_[i];
    }

    // Clamp and apply shared filter
    for (int i = 0; i < num_samples; ++i)
        self->mix_buf_[i] = std::clamp(self->mix_buf_[i], -1.0f, 1.0f);

    if (self->filter_type_.load(std::memory_order_acquire) != helix::audio::FilterType::NONE) {
        helix::audio::apply_filter(self->filter_, self->mix_buf_.data(), num_samples);
    }

    std::memcpy(out, self->mix_buf_.data(), num_samples * sizeof(float));
}
```

- [ ] **Step 3: Update SDL test**

In `tests/unit/test_sdl_sound_backend.cpp`, any tests that directly reference `SDLSoundBackend` member names (`current_freq_`, etc.) may need updates since those are now inside `voices_[0]`. The tests use the public API (`generate_samples`, `apply_filter`), so most should be unaffected. Verify by building.

- [ ] **Step 4: Build and test**

```bash
make -j && make test-run
```

- [ ] **Step 5: Commit**

```bash
git add include/sdl_sound_backend.h src/system/sdl_sound_backend.cpp
git commit -m "feat(sound): add 4-voice polyphony to SDL audio backend"
```

---

### Task 4: ALSA backend — 4-voice polyphony

Same pattern as SDL but for the ALSA blocking-write render loop.

**Files:**
- Modify: `include/alsa_sound_backend.h`
- Modify: `src/system/alsa_sound_backend.cpp`

- [ ] **Step 1: Update ALSA header**

Same structural changes as SDL: replace single-voice atomics with `VoiceState voices_[MAX_VOICES]` array, add `mix_buf_` vector, add voice method overrides. Keep the existing `mono_to_stereo()` and `float_to_s16()` static helpers.

The `VoiceState` struct is identical to SDL's. Add `voice_buf_` vector alongside existing buffer vectors.

- [ ] **Step 2: Update ALSA implementation**

**`initialize()`**: After buffer allocation, also allocate `voice_buf_` and `mix_buf_`:
```cpp
voice_buf_.resize(period_size_);
mix_buf_.resize(period_size_);
```

**`set_tone()`**, **`silence()`**, **`set_waveform()`**: Same delegation pattern as SDL.

**New voice methods**: Same bounds-checked atomic stores as SDL.

**`render_loop()`**: Replace the single-voice generation with multi-voice mixing loop (same pattern as SDL's `audio_callback` but in the blocking-write style). The `mix_buf_` replaces what was `mono_buf_`. After mixing: stereo dup if needed, S16 conversion if needed, `snd_pcm_writei()`.

- [ ] **Step 3: Build and test**

```bash
make -j && make test-run
```

- [ ] **Step 4: Commit**

```bash
git add include/alsa_sound_backend.h src/system/alsa_sound_backend.cpp
git commit -m "feat(sound): add 4-voice polyphony to ALSA audio backend"
```

---

### Task 5: Sequencer — voice assignment + release fence

Wire the sequencer to use the voice interface for chord steps.

**Files:**
- Modify: `include/sound_sequencer.h`
- Modify: `src/system/sound_sequencer.cpp`

- [ ] **Step 1: Add apply_step to sequencer header**

In `include/sound_sequencer.h`, add in the private section (after `end_playback()`, line 85):

```cpp
    /// Apply a step's note(s) to backend voices
    void apply_step_voices(const SoundStep& step, float amplitude, float duty);
```

- [ ] **Step 2: Implement apply_step_voices**

In `src/system/sound_sequencer.cpp`, add the method:

```cpp
void SoundSequencer::apply_step_voices(const SoundStep& step, float amplitude, float duty) {
    if (step.chord_count > 0) {
        // Polyphonic: assign each chord note to a voice slot
        int voices = backend_->voice_count();
        for (int v = 0; v < voices; ++v) {
            if (v < step.chord_count) {
                backend_->set_voice(v, step.chord_freqs[v], amplitude, duty);
                if (backend_->supports_waveforms()) {
                    backend_->set_voice_waveform(v, step.wave);
                }
            } else {
                backend_->silence_voice(v);
            }
        }
    } else {
        // Monophonic: voice 0 only via existing set_tone path
        if (backend_->supports_waveforms()) {
            backend_->set_waveform(step.wave);
        }
        backend_->set_tone(step.freq_hz, amplitude, duty);
        // Silence other voices (no-op on mono backends via default impl)
        for (int v = 1; v < backend_->voice_count(); ++v)
            backend_->silence_voice(v);
    }
    // Release fence: make all voice writes visible to render thread
    std::atomic_thread_fence(std::memory_order_release);
}
```

- [ ] **Step 3: Update tick() to use apply_step_voices**

In `src/system/sound_sequencer.cpp`, in the `tick()` function, replace the block at lines 212-226 (from `// Set waveform if backend supports it` through `backend_->set_tone(freq, amplitude, duty);`) with:

```cpp
    // Set filter if backend supports it and step has a filter configured
    if (backend_->supports_filter() && !step.filter.type.empty()) {
        float cutoff = step.filter.cutoff;
        if (step.filter.sweep_to > 0) {
            cutoff = compute_sweep(step.filter.cutoff, step.filter.sweep_to, progress);
        }
        backend_->set_filter(step.filter.type, cutoff);
    }

    // Apply frequency sweep as ratio for chords (preserves intervals)
    if (step.chord_count > 0 && !step.sweep.target.empty() && step.sweep.target == "freq") {
        // Compute sweep ratio from the root note
        float sweep_freq = compute_sweep(step.freq_hz, step.sweep.end_value, progress);
        float ratio = (step.freq_hz > 0) ? (sweep_freq / step.freq_hz) : 1.0f;
        // Apply ratio to all chord voices
        SoundStep swept_step = step;
        for (uint8_t i = 0; i < swept_step.chord_count; ++i)
            swept_step.chord_freqs[i] *= ratio;
        apply_step_voices(swept_step, amplitude, duty);
    } else {
        // For monophonic steps, freq is already swept at line 188
        SoundStep mod_step = step;
        mod_step.freq_hz = freq;  // use the swept/LFO-modulated frequency
        apply_step_voices(mod_step, amplitude, duty);
    }
```

Note: This replaces the old `backend_->set_waveform()` + `backend_->set_filter()` + `backend_->set_tone()` block. The filter handling stays at the top (filter is shared, applied to mix). The waveform and tone handling is now delegated to `apply_step_voices()`.

- [ ] **Step 4: Build and test**

```bash
make -j && make test-run
```

- [ ] **Step 5: Commit**

```bash
git add include/sound_sequencer.h src/system/sound_sequencer.cpp
git commit -m "feat(sound): add polyphonic voice assignment to sequencer"
```

---

### Task 6: Polyphony unit tests

**Files:**
- Create: `tests/unit/test_sound_polyphony.cpp`

- [ ] **Step 1: Create polyphony test file**

Tests to implement:

1. **Chord parsing — 3 notes**: Parse a step JSON with `"notes": ["C4", "E4", "G4"]`, verify `chord_count == 3` and `chord_freqs` contain correct Hz values (C4≈261.63, E4≈329.63, G4≈392.00)

2. **Chord parsing — single note fallback**: Parse with just `"note": "A4"`, verify `chord_count == 0` and `freq_hz == 440.0`

3. **Chord parsing — capped to 4**: Parse with `"notes": ["C4","D4","E4","F4","G4"]` (5 notes), verify `chord_count == 4` (5th dropped)

4. **Chord parsing — notes overrides note**: Parse with both `"note": "A4"` and `"notes": ["C4","E4"]`, verify `chord_count == 2` and `freq_hz` is set to C4 (root note for mono fallback)

5. **Backend voice_count default**: Create a mock backend (or use base class), verify `voice_count() == 1`

6. **Backend set_voice default — slot 0 delegates**: Verify `set_voice(0, 440, 0.8, 0.5)` calls `set_tone(440, 0.8, 0.5)` on base class

7. **Backend set_voice default — slot 1+ is no-op**: Verify `set_voice(1, ...)` does not crash and does not call `set_tone()`

Structure:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_theme.h"
#include "sound_backend.h"

#include <cmath>
#include "../catch_amalgamated.hpp"

using Catch::Approx;
```

For chord parsing tests, either parse from JSON directly or construct `SoundStep` and verify fields. The simplest approach: use `SoundThemeParser::load_from_string()` if available, or manually construct JSON and call the parser. Check if `SoundThemeParser` has a public method for this — if not, test at the theme-loading level (create a minimal theme JSON string with a chord step).

For backend mock tests, create a simple `MockBackend` subclass that tracks calls:
```cpp
class MockBackend : public SoundBackend {
  public:
    float last_freq = 0, last_amp = 0, last_duty = 0;
    bool silenced = false;
    void set_tone(float f, float a, float d) override { last_freq = f; last_amp = a; last_duty = d; }
    void silence() override { silenced = true; }
};
```

- [ ] **Step 2: Build and run**

```bash
make test-run
```

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_sound_polyphony.cpp
git commit -m "test(sound): add polyphony unit tests for chord parsing and voice interface"
```

---

### Task 7: Build verification + update Jan Hammer themes with chords

**Files:**
- Modify: `config/sounds/crocketts_theme.json` (add chord steps)
- Modify: `config/sounds/miami_vice.json` (add chord steps)

- [ ] **Step 1: Full build and test**

```bash
make -j && make test-run
```

- [ ] **Step 2: Add chord steps to Crockett's Theme**

In `crocketts_theme.json`, update the `print_complete` sound to use chords for the triumphant finale. Update the `startup` sound to add chord stabs at climactic moments. Example:

Replace single-note climax steps with chord versions:
```json
{ "notes": ["C5", "E5", "G5"], "dur": "4n", ... }
```

Keep most notes monophonic — chords are most effective as accents.

- [ ] **Step 3: Add chord steps to Miami Vice**

In `miami_vice.json`, add chord stabs to `print_complete` and `startup` sounds. Miami Vice style uses punchy staccato chord hits.

- [ ] **Step 4: Commit**

```bash
git add config/sounds/crocketts_theme.json config/sounds/miami_vice.json
git commit -m "feat(sound): add chord support to Jan Hammer tribute themes"
```
