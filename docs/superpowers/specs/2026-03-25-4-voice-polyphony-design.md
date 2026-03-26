# 4-Voice Polyphony

**Date:** 2026-03-25
**Status:** Design

## Summary

Add 4-voice polyphonic synthesis to the sound system. Backends gain a voice slot interface, the sequencer assigns notes to voices, and the JSON theme format gains a `"notes"` array for chords. PWM/M300 backends gracefully degrade to root note only.

## Design

### Voice State

Each polyphonic backend (SDL, ALSA) maintains 4 independent voice slots:

```cpp
static constexpr int MAX_VOICES = 4;

struct VoiceState {
    std::atomic<float> freq{0};
    std::atomic<float> amplitude{0};
    std::atomic<float> duty{0.5f};
    std::atomic<Waveform> wave{Waveform::SQUARE};
    float phase = 0;  // render-thread only
};
```

**Phase reset:** When the render loop detects a voice's amplitude has dropped to zero, it resets that voice's phase to 0. This gives a clean attack when the voice is re-triggered, without disturbing other sustained voices.

### Backend Interface Changes

New virtual methods on `SoundBackend`:

```cpp
/// Set a specific voice slot (0-3). Default: maps slot 0 to set_tone().
virtual void set_voice(int slot, float freq_hz, float amplitude, float duty_cycle);
virtual void set_voice_waveform(int slot, Waveform w);
virtual void silence_voice(int slot);

/// How many voices this backend supports. Default: 1.
virtual int voice_count() const { return 1; }
```

Default implementations in `SoundBackend`:
- `set_voice(0, ...)` calls `set_tone(...)`, other slots are no-ops
- `silence_voice(0)` calls `silence()`, other slots are no-ops
- `voice_count()` returns 1

This preserves full backward compatibility — PWM and M300 backends need zero changes.

### SDL/ALSA Backend Changes

Both backends replace their single-voice atomics with a `VoiceState voices_[MAX_VOICES]` array.

**Buffer allocation:**
- **ALSA**: Add a `mix_buf_` vector alongside existing `mono_buf_`, both allocated once in `initialize()`
- **SDL**: Add a `voice_buf_` and `mix_buf_` as `std::vector<float>` members, resized once in `initialize()`. The SDL callback's `out` pointer is the final output — don't reuse it as scratch.

**Render loop** (same pattern for both):
```cpp
// Acquire fence: ensure we see all voice writes from the sequencer as a group
std::atomic_thread_fence(std::memory_order_acquire);

// Check if any voice is active (early-return optimization)
bool any_active = false;
for (int v = 0; v < MAX_VOICES; ++v) {
    if (voices_[v].amplitude.load(relaxed) > 0.001f) { any_active = true; break; }
}
if (!any_active) {
    std::memset(output, 0, frame_count * sizeof(float));
    return;
}

// Generate each active voice, sum into mix buffer
std::memset(mix_buf, 0, frame_count * sizeof(float));
for (int v = 0; v < MAX_VOICES; ++v) {
    float amp = voices_[v].amplitude.load(relaxed);
    if (amp <= 0.001f) {
        voices_[v].phase = 0;  // reset phase for clean re-trigger
        continue;
    }
    float freq = voices_[v].freq.load(relaxed);
    if (freq <= 0.0f) continue;

    generate_samples(voice_buf, frame_count, sample_rate_,
                     voices_[v].wave.load(relaxed), freq, amp,
                     voices_[v].duty.load(relaxed), voices_[v].phase);
    for (size_t i = 0; i < frame_count; ++i)
        mix_buf[i] += voice_buf[i];
}
// Clamp mixed output to [-1, 1]
for (size_t i = 0; i < frame_count; ++i)
    mix_buf[i] = std::clamp(mix_buf[i], -1.0f, 1.0f);
// Apply shared filter to mixed output
if (filter_type != FilterType::NONE)
    apply_filter(filter_, mix_buf, frame_count);
```

One shared filter on the mixed output (not per-voice).

**`set_tone()`** maps to `set_voice(0, ...)` for backward compatibility.
**`silence()`** silences all 4 voices (loops over `voices_[]` zeroing amplitudes).

**Memory ordering:** The sequencer writes all voice slots then does a `std::atomic_thread_fence(release)`. The render loop does an `acquire` fence before reading voices. This guarantees all voice parameters are visible as a group, preventing stale-chord glitches where the render thread sees a mix of old and new step parameters.

### JSON Theme Format

Backward-compatible. Existing `"note"`/`"freq"` still works (voice 0 only). New `"notes"` array for chords:

```json
{ "note": "C4", "dur": "4n" }
{ "notes": ["C4", "E4", "G4"], "dur": "4n" }
{ "notes": ["C4", "E4", "G4", "C5"], "dur": "4n" }
```

- `"notes"` takes priority over `"note"` if both present
- Each note inherits the step's `wave`, `vel`, `env`, `duty`, `filter`, `lfo`
- If `notes` has more than `MAX_VOICES` entries, extras are silently dropped during parsing
- Parsing in `SoundThemeParser`: new `parse_notes()` that converts note names to frequencies

### SoundStep Changes

```cpp
struct SoundStep {
    // Existing fields unchanged...
    float freq_hz = 0;  // single voice (backward compat)

    // New: multi-voice frequencies (empty = use freq_hz on voice 0)
    // Fixed size avoids heap allocation per step (MAX_VOICES = 4)
    std::array<float, 4> chord_freqs{};
    uint8_t chord_count = 0;  // 0 = monophonic (use freq_hz)
};
```

### Sequencer Changes

In `begin_playback()` and step advance:

```cpp
void SoundSequencer::apply_step(const SoundStep& step) {
    if (step.chord_count > 0) {
        // Polyphonic: assign each note to a voice
        int voices = backend_->voice_count();
        for (int v = 0; v < voices; ++v) {
            if (v < step.chord_count) {
                backend_->set_voice(v, step.chord_freqs[v], amplitude, duty);
                backend_->set_voice_waveform(v, step.wave);
            } else {
                backend_->silence_voice(v);
            }
        }
    } else {
        // Monophonic: voice 0 only, silence others
        backend_->set_tone(step.freq_hz, amplitude, duty);
        for (int v = 1; v < backend_->voice_count(); ++v)
            backend_->silence_voice(v);
    }
    // Release fence: make all voice writes visible to render thread
    std::atomic_thread_fence(std::memory_order_release);
}
```

Where `amplitude` and `duty` come from the envelope/modulation computation (existing code), not from `SoundStep` directly (the current sequencer hardcodes `duty = 0.5f` at the tick level).

**Frequency sweeps on chords:** Sweeps apply as a **ratio** to each voice's base frequency, preserving intervals. If a chord is [C4, E4, G4] and the sweep goes to 2x, the result is [C5, E5, G5] — not [C4+261Hz, E4+261Hz, G4+261Hz].

### Graceful Degradation

| Backend | `voice_count()` | Behavior |
|---------|-----------------|----------|
| SDL | 4 | Full polyphony |
| ALSA | 4 | Full polyphony |
| PWM | 1 (default) | Root note only (chord_freqs[0]) |
| M300 | 1 (default) | Root note only (chord_freqs[0]) |

### Known Issues

**`set_filter()` data race:** Both SDL and ALSA backends write non-atomic biquad coefficients from the sequencer thread while the render thread reads them. This is a pre-existing issue (not introduced by polyphony) but the longer render loop (4 voice generations before filter) widens the race window slightly. Tracked for a separate fix (double-buffer the `BiquadFilter` with atomic index swap).

### File Summary

| File | Action | Est. Lines |
|------|--------|------------|
| `include/sound_backend.h` | Modify — add virtual voice methods with defaults | ~15 |
| `include/sdl_sound_backend.h` | Modify — VoiceState array, voice_buf/mix_buf, overrides | ~25 |
| `src/system/sdl_sound_backend.cpp` | Modify — multi-voice render loop, set_voice impls | ~50 |
| `include/alsa_sound_backend.h` | Modify — VoiceState array, mix_buf, overrides | ~25 |
| `src/system/alsa_sound_backend.cpp` | Modify — multi-voice render loop, set_voice impls | ~50 |
| `include/sound_theme.h` | Modify — add chord_freqs/chord_count to SoundStep | ~5 |
| `src/system/sound_theme.cpp` | Modify — parse `"notes"` array | ~20 |
| `include/sound_sequencer.h` | Modify — apply_step helper | ~5 |
| `src/system/sound_sequencer.cpp` | Modify — voice assignment in tick/advance | ~35 |
| `tests/unit/test_sound_polyphony.cpp` | Create — polyphony tests | ~150 |

**Total: ~380 lines**

### Testing

- **Voice mixing**: Generate 2 voices (440Hz + 880Hz), verify mixed output contains both frequencies
- **Chord parsing**: Verify `"notes": ["C4", "E4", "G4"]` produces `chord_count=3` with correct frequencies
- **Backward compatibility**: Verify single `"note"` steps still work (chord_count=0, voice 0 only)
- **Voice count capping**: Verify 5-note chord is capped to 4 during parsing
- **Silence on advance**: Verify unused voices are silenced between steps
- **Mono backend degradation**: Verify `voice_count()=1` backend plays root note only, no crash
- **Phase reset**: Verify silenced voice gets phase=0, re-triggered voice starts clean

### Non-Goals

- Per-voice filtering (shared filter on mix is sufficient)
- Voice stealing (4 slots, chords limited to 4 notes)
- Independent per-voice envelopes (all voices share the step's envelope)
- Velocity per note within a chord (all notes same velocity)
- Per-voice duty cycle (all voices share step duty)
