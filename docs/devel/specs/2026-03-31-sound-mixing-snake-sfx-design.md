# Sound Mixing & Snake Game SFX — Design Spec

**Date:** 2026-03-31
**Status:** Approved
**Scope:** Enable concurrent tracker music + sound effects, add sound to the snake game easter egg.

## Overview

The sound system currently uses strict either/or: tracker music or sound effects, never both. This spec adds layered mixing so the snake game can play `elysium.mod` as background music with arcade SFX on top.

## Part 1: Sound System — Concurrent Mixing

### Sequencer Tick Loop

Current behavior in `sound_sequencer.cpp` tick loop:

```cpp
if (ext_tick) {
    ext_tick(dt_ms);      // tracker ONLY
} else if (playing_) {
    tick(dt_ms);           // sfx ONLY
}
```

New behavior:

```cpp
if (ext_tick) {
    ext_tick(dt_ms);       // tracker (always if present)
}
if (playing_) {
    tick(dt_ms);           // sfx overlay (runs alongside tracker)
}
```

For PCM-capable backends (SDL, ALSA), both outputs mix naturally since they both write to the audio buffer. For frequency-only backends (PWM, M300), SFX is skipped while tracker is active — single tone hardware can't mix.

### SoundManager Preemption Changes

Current: any sound with priority >= tracker priority kills the tracker.

New:
- If backend supports mixing (`has_render_source()` returns true): queue SFX normally alongside tracker. Tracker keeps playing.
- If backend is frequency-only: skip SFX while tracker is playing (current drop behavior).
- ALARM priority still kills the tracker unconditionally (safety alerts > easter egg music).

New public methods on SoundManager:
- `bool can_mix() const` — returns true if backend supports concurrent tracker + SFX
- `void fade_out_tracker(uint32_t duration_ms)` — ramps tracker volume to zero over duration, then stops

### Tracker Fade-Out

`fade_out_tracker(uint32_t duration_ms)` uses the existing `TrackerPlayer::set_volume_override(int vol)` to ramp volume from current level to 0 over the specified duration. Implementation: set a fade target in the sequencer, decrement volume each tick proportionally. When volume reaches 0, call `stop_tracker()`.

### Files Touched

| File | Change |
|------|--------|
| `src/system/sound_sequencer.cpp` | Remove either/or in tick loop, allow concurrent execution |
| `src/system/sound_manager.cpp` | Conditional preemption based on backend capability, add fade_out_tracker() |
| `include/sound_manager.h` | Add `can_mix()`, `fade_out_tracker(uint32_t ms)` |

## Part 2: Snake Game Sound Integration

### Sound Tiers

**Tier 1 — Tracker + SFX (`HELIX_HAS_TRACKER`):**
- Game start: `play_file("assets/sounds/elysium.mod")` — loops as background music
- All SFX play layered on top of tracker
- Death: `fade_out_tracker(500)` — music fades over 500ms during death animation
- Restart (tap to play): `play_file(...)` again — music starts fresh from beginning
- Game close (ESC/X): `stop_tracker()` — immediate stop

**Tier 2 — SFX only (`HELIX_HAS_SOUND`, no tracker):**
- All SFX play standalone (no background music)
- Same events, same sounds, just no tracker underneath

**Tier 3 — No sound:**
- Silent. Game works as before.

### Sound Effects (Hardcoded Waveforms)

All SFX are hardcoded simple waveforms — no JSON theme entries needed. Easter egg doesn't need theme customization.

| Event | Waveform | Duration | Pitch | Character |
|-------|----------|----------|-------|-----------|
| **Eat food** | Square wave | ~80ms | Sweep 440→880Hz | Short ascending chirp |
| **Die** | Sawtooth | ~200ms | Sweep 220→55Hz | Low descending buzz |
| **Speed tier up** | Square wave | ~150ms | C5→E5→G5 arpeggio | Quick three-note rise |
| **Game start** | Square wave | ~120ms | C4→G4 interval | Two-note rising fanfare |

SFX play at `SoundPriority::UI` level — they never preempt real application sounds.

### Death Sequence Timing

1. **0ms** — Player dies. `fade_out_tracker(500)` called. White flash. SFX "die" sound plays.
2. **0-200ms** — Snake turns red, segments shrink. Tracker fading.
3. **300ms** — Score card fades in. Tracker still fading.
4. **500ms** — Tracker reaches zero volume, stops.
5. **600ms** — Silence. Input accepted. Tap to restart.
6. **On restart** — Game start SFX plays, tracker restarts from beginning.

### Files Touched

| File | Change |
|------|--------|
| `src/ui/ui_snake_game.cpp` | Add sound calls at game events, `#include "sound_manager.h"`, `#ifdef HELIX_HAS_SOUND` / `#ifdef HELIX_HAS_TRACKER` guards |

## Out of Scope

- Theme-configurable SFX (hardcoded is fine for easter egg)
- Tracker pause/resume (always restart from beginning)
- Mixing on frequency-only backends (PWM, M300) — physically impossible
- Volume ducking (tracker volume stays constant while SFX plays)
