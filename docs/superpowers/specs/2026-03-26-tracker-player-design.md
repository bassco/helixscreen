# MOD/MED Tracker Player

**Date:** 2026-03-26
**Status:** Design

## Summary

Add a standalone tracker playback engine that parses ProTracker MOD and OctaMED MMD0-3 files and plays them through the existing 4-voice polyphonic backend. Integrated into SoundManager via `play_file()`. Gated by two compile-time flags: `HELIX_HAS_SOUND` (entire sound system) and `HELIX_HAS_TRACKER` (tracker parser/player).

## Design Decisions

- **Standalone player, not a converter.** Tracker playback (pattern-based, per-tick effects, channel state) is fundamentally different from the existing sequencer's linear step model. The TrackerPlayer drives the backend directly, bypassing the sequencer.
- **Synthesis, not sampling.** MOD/MED instruments are PCM samples; we map them to synth waveforms instead. Keeps code small, matches the chiptune aesthetic, avoids sample mixing/interpolation complexity.
- **~15-20 tracker effects** for good fidelity on most files. Unrecognized effects silently ignored.
- **Two compile-time flags** for granular exclusion. `HELIX_HAS_TRACKER` requires `HELIX_HAS_SOUND`.
- **Routes through SoundManager** — priority system applies, one playback source at a time.

## Data Model: TrackerModule

```cpp
struct TrackerNote {
    uint8_t note;        // 0=none, 1-120 = C-0..B-9
    uint8_t instrument;  // 0=none
    uint8_t effect;
    uint8_t effect_data;
};

struct TrackerInstrument {
    Waveform waveform = Waveform::SQUARE;
    float volume = 1.0f;    // 0.0-1.0
    float finetune = 0;     // semitone offset
};

struct TrackerModule {
    std::vector<TrackerInstrument> instruments;
    // Flat storage: patterns[pat_idx][row * 4 + channel]
    std::vector<std::vector<TrackerNote>> patterns;
    std::vector<uint8_t> order;       // pattern playback sequence
    uint8_t num_orders = 0;
    uint8_t speed = 6;                // ticks per row
    uint8_t tempo = 125;              // BPM
    uint16_t rows_per_pattern = 64;   // 64 for MOD, up to 999 for MED

    static std::optional<TrackerModule> load(const std::string& path);
    static std::optional<TrackerModule> load_from_memory(const uint8_t* data, size_t size);
};
```

**Memory:** `TrackerNote` is 4 bytes. A 64-row, 4-channel pattern is 1KB. The Crockett's Theme MED (9 patterns) fits in ~9KB.

**Format detection:** `load()` reads magic bytes. MMD files start with `"MMD0"`-`"MMD3"` at offset 0. MOD files have a 4-char signature at offset 1080 (`"M.K."`, `"4CHN"`, `"FLT4"`, etc.). Unrecognized formats return `nullopt`. Paths are relative to the working directory (same convention as other asset loading — `SoundThemeParser::load_from_file()`, image assets, etc.).

**Instrument mapping:** Each instrument maps to a `Waveform` enum value. Default is SQUARE. The MED file's tracks map naturally: square for lead, saw for bass, triangle for pads, sine for harmony. Volume and finetune are carried from the file's instrument table.

## Playback Engine: TrackerPlayer

```cpp
class TrackerPlayer {
public:
    explicit TrackerPlayer(std::shared_ptr<SoundBackend> backend);

    void load(TrackerModule module);  // takes ownership via move
    void play();
    void stop();
    bool is_playing() const;
    void tick(float dt_ms);           // called from sequencer thread

private:
    struct ChannelState {
        float freq = 0;
        float target_freq = 0;     // tone portamento target
        float volume = 1.0f;
        float duty = 0.5f;         // duty cycle (square wave width)
        float vibrato_phase = 0;
        uint8_t vibrato_speed = 0;
        uint8_t vibrato_depth = 0;
        uint8_t arp_tick = 0;      // 0/1/2 cycle
        uint8_t arp_x = 0, arp_y = 0;
        uint8_t instrument = 0;
        uint8_t effect = 0;
        uint8_t effect_data = 0;
        Waveform waveform = Waveform::SQUARE;
        bool active = false;
    };

    void advance_row();
    void process_effects_on_tick();
    void apply_to_backend();

    std::shared_ptr<SoundBackend> backend_;
    TrackerModule module_;
    std::array<ChannelState, 4> channels_{};

    int order_idx_ = 0;
    int row_ = 0;
    int tick_ = 0;           // current tick within row
    int speed_ = 6;          // ticks per row
    int tempo_ = 125;        // BPM
    float tick_accum_ = 0;   // ms accumulator
    std::atomic<bool> playing_{false};
};
```

### Tick Timing

MOD/MED tempo: `ms_per_tick = 2500.0f / tempo`. At default tempo 125, that's 20ms per tick. With speed 6, each row lasts 120ms.

`tick(float dt_ms)` accumulates dt. When accumulator exceeds `ms_per_tick`, fires one tracker tick and decrements. Multiple tracker ticks can fire per call if dt is large (catches up after scheduling jitter).

### Row Processing

1. **Tick 0 of each row:** Read `TrackerNote` for all 4 channels from current pattern/row. New notes set frequency and trigger (reset volume from instrument). Capture effect command and data into `ChannelState`.
2. **Every tick (including 0):** Process per-tick effects (slides, vibrato, arpeggio cycling, volume slides).
3. **After processing:** `apply_to_backend()` scales each channel's volume by master volume from `AudioSettingsManager::instance().get_volume() / 100.0f`, then writes all 4 channels to voice slots 0-3 via `set_voice(slot, freq, scaled_volume, duty)` / `set_voice_waveform(slot, waveform)` / `silence_voice(slot)`, followed by a `std::atomic_thread_fence(std::memory_order_release)`.

### Supported Effects (~15-20)

| Effect | Cmd | Behavior |
|--------|-----|----------|
| Arpeggio | 0xy | Cycle freq: note, note+x semitones, note+y semitones (per tick) |
| Portamento up | 1xx | freq += xx × period_step per tick |
| Portamento down | 2xx | freq -= xx × period_step per tick |
| Tone portamento | 3xx | Slide freq toward target note at rate xx per tick |
| Vibrato | 4xy | Oscillate freq ± depth(y) at speed(x), sine LUT |
| Tone porta + vol slide | 5xy | Continue tone porta + volume slide (Axy logic) |
| Vibrato + vol slide | 6xy | Continue vibrato + volume slide (Axy logic) |
| Tremolo | 7xy | Oscillate volume ± depth(y) at speed(x) |
| Set volume | Cxx | Immediate: channel volume = xx/64 |
| Volume slide | Axy | volume += x/64 or -= y/64 per tick |
| Position jump | Bxx | Jump to order position xx, row 0 |
| Pattern break | Dxx | Next order, start at row xx |
| Set speed/tempo | Fxx | xx < 32 → speed = xx; xx ≥ 32 → tempo = xx |
| Fine porta up | E1x | One-shot fine slide up on tick 0 only |
| Fine porta down | E2x | One-shot fine slide down on tick 0 only |
| Note cut | ECx | Silence channel at tick x |
| Note retrigger | E9x | Re-trigger note every x ticks |
| Pattern loop | E6x | x=0: set loop start; x>0: loop x times |
| Fine volume up | EAx | volume += x/64 on tick 0 only |
| Fine volume down | EBx | volume -= x/64 on tick 0 only |

Unrecognized effects are silently ignored — no crash, no log spam.

### End of Module

When the last order is reached and the last row played, playback stops (`playing_ = false`). No looping by default. SoundManager detects `!is_playing()` on next tick and cleans up.

## SoundManager Integration

### New API

```cpp
#ifdef HELIX_HAS_TRACKER
void play_file(const std::string& path, SoundPriority priority = SoundPriority::EVENT);
void stop_tracker();
bool is_tracker_playing() const;
#endif
```

### Behavior

- `play_file()` calls `TrackerModule::load(path)`. On success, creates a `TrackerPlayer` with the current backend and starts playback.
- Priority preemption: tracker playback participates in the same priority system as sequencer sounds. A higher-priority `play()` call stops the tracker; a `play_file()` at higher priority stops the sequencer.
- Only one `TrackerPlayer` at a time. Calling `play_file()` again replaces the current one.
- `stop_tracker()` for explicit stop (e.g., navigating away from About screen).

### Tick Routing

`SoundSequencer` gains a `set_external_tick(std::function<void(float)>)` method. When set, the sequencer loop calls this callback instead of its own step advancement logic. When cleared (set to `nullptr`), normal sequencer behavior resumes.

```cpp
// SoundSequencer new method:
void set_external_tick(std::function<void(float dt_ms)> fn);

// sequencer_loop() pseudocode:
while (running_) {
    auto dt = measure_elapsed();
    if (external_tick_) {
        external_tick_(dt);        // drives TrackerPlayer
    } else if (playing_) {
        tick(dt);                  // normal sequencer step logic
    } else {
        wait_on_condition_variable();
    }
    sleep(min_tick_ms);
}
```

One thread, no new threads created. The external tick callback is set/cleared by SoundManager during `play_file()` / `stop_tracker()`.

### Handoff Sequence

When `play_file()` preempts the sequencer:
1. `SoundManager` calls `sequencer_->stop()` — this calls `backend_->silence()` and clears sequencer state
2. `SoundManager` sets `sequencer_->set_external_tick(...)` to route ticks to the tracker
3. TrackerPlayer begins playback on next tick

When `stop_tracker()` is called or tracker finishes:
1. TrackerPlayer calls `backend_->silence()` (all voices)
2. `SoundManager` clears `sequencer_->set_external_tick(nullptr)`
3. Sequencer resumes normal behavior on next tick

When a higher-priority sequencer sound preempts the tracker:
1. `SoundManager` calls `tracker_->stop()` (silences all voices)
2. `SoundManager` clears `set_external_tick(nullptr)`
3. Normal `sequencer_->play()` proceeds

All transitions happen on the SoundManager's calling thread (UI thread). The sequencer thread only reads `external_tick_` — no concurrent write. `external_tick_` is a `std::function` guarded by the sequencer's existing `queue_mutex_`.

## About Screen Integration

```cpp
void AboutSettingsOverlay::on_activate() {
#ifdef HELIX_HAS_TRACKER
    SoundManager::instance().play_file(
        "assets/sounds/crocketts_theme.med", SoundPriority::UI);
#endif
}

void AboutSettingsOverlay::on_deactivate() {
#ifdef HELIX_HAS_TRACKER
    SoundManager::instance().stop_tracker();
#endif
}
```

## Compile-Time Gating

### Flags

| Flag | Gates | Default ON |
|------|-------|------------|
| `HELIX_HAS_SOUND` | SoundManager, SoundSequencer, all backends, SoundTheme, SoundThemeParser, AudioSettingsManager, sound settings overlay | Pi, Pi32, x86, native with SDL or ALSA |
| `HELIX_HAS_TRACKER` | TrackerModule, TrackerPlayer, MOD/MED parsers, `play_file()` API | Same platforms as `HELIX_HAS_SOUND` |

`HELIX_HAS_TRACKER` requires `HELIX_HAS_SOUND`. Setting `HELIX_HAS_TRACKER` without `HELIX_HAS_SOUND` is a compile error (static_assert).

### Makefile

```makefile
# Sound system
SOUND_CXXFLAGS :=
TRACKER_CXXFLAGS :=
ifneq (,$(filter pi pi-fbdev pi-both pi32 pi32-fbdev pi32-both x86 x86-fbdev x86-both,$(PLATFORM_TARGET)))
    SOUND_CXXFLAGS := -DHELIX_HAS_SOUND
    TRACKER_CXXFLAGS := -DHELIX_HAS_TRACKER
else ifeq ($(PLATFORM_TARGET),native)
    # Sound enabled if SDL or ALSA available
    ifneq (,$(or $(HELIX_DISPLAY_SDL),$(ALSA_CXXFLAGS)))
        SOUND_CXXFLAGS := -DHELIX_HAS_SOUND
        TRACKER_CXXFLAGS := -DHELIX_HAS_TRACKER
    endif
endif
CXXFLAGS += $(SOUND_CXXFLAGS) $(TRACKER_CXXFLAGS)
```

### Source Gating

New files (`tracker_module.h`, `tracker_player.h`, their `.cpp` files) are entirely wrapped in `#ifdef HELIX_HAS_TRACKER`. Conditional compilation in Makefile excludes them from the build entirely when the flag is off — no empty translation units.

Existing sound files get `#ifdef HELIX_HAS_SOUND` wrappers. This is a mechanical change applied via Makefile conditional object lists (not `#ifdef` in every file). Sound `.o` files are only compiled when `HELIX_HAS_SOUND` is set. Callers guard their `SoundManager::instance()` calls with `#ifdef HELIX_HAS_SOUND`:

**Files requiring `#ifdef HELIX_HAS_SOUND` call-site guards:**
- `src/application/application.cpp` — `SoundManager::instance().play("startup")`, init/shutdown
- `src/ui/ui_settings_sound.cpp` — entire file (sound settings overlay)
- `src/ui/ui_settings_about.cpp` — tracker playback hook (also gated by `HELIX_HAS_TRACKER`)
- Any panel/overlay calling `SoundManager::instance().play()` for UI sounds

## File Summary

| File | Action |
|------|--------|
| `include/tracker_module.h` | Create — data model, `load()` |
| `src/system/tracker_module_mod.cpp` | Create — ProTracker MOD parser |
| `src/system/tracker_module_med.cpp` | Create — OctaMED MMD0-3 parser |
| `include/tracker_player.h` | Create — TrackerPlayer class |
| `src/system/tracker_player.cpp` | Create — playback engine, effects |
| `include/sound_backend.h` | Modify — move `SoundPriority` enum here (from `sound_sequencer.h`) |
| `include/sound_manager.h` | Modify — `play_file()`, TrackerPlayer member |
| `src/system/sound_manager.cpp` | Modify — file playback, tick routing, handoff sequence |
| `include/sound_sequencer.h` | Modify — add `set_external_tick()`, remove `SoundPriority` (moved) |
| `src/system/sound_sequencer.cpp` | Modify — external tick callback in sequencer loop |
| `src/ui/ui_settings_about.cpp` | Modify — play/stop on activate/deactivate |
| `Makefile` | Modify — `HELIX_HAS_SOUND` + `HELIX_HAS_TRACKER` flags |
| `assets/sounds/crocketts_theme.med` | Copy — MED file from ~/Downloads |
| `tests/unit/test_tracker_module.cpp` | Create — parse verification |
| `tests/unit/test_tracker_player.cpp` | Create — tick/effect/voice tests |

## Testing

- **MOD parsing:** Load a known MOD file, verify pattern count, note values, instrument count, order list
- **MED parsing:** Load crocketts_theme.med, verify 9 patterns, 4 channels, correct note extraction from blocks 2-3
- **Format detection:** MOD magic at 1080, MMD magic at 0, garbage returns nullopt
- **Tick advancement:** Verify row advances after `speed` ticks, order advances after all rows
- **Effect processing:** Arpeggio cycles frequencies, portamento slides, volume slide clamps 0-1, pattern break jumps correctly
- **Backend voice output:** After tick, verify `set_voice()` called with expected frequencies for each channel
- **End of module:** Playback stops after last order, `is_playing()` returns false
- **Priority preemption:** Tracker stopped when higher-priority sequencer sound plays
- **Compile gating:** Build with `HELIX_HAS_TRACKER=0` succeeds, no tracker symbols in binary

## Non-Goals

- PCM sample playback (we synthesize, not sample)
- More than 4 channels (MOD is 4, our backend is 4)
- 8-channel OctaMED support (MMD files with >4 tracks play first 4 only)
- SID/VGM formats (fundamentally different architectures, deferred)
- Looping playback (play once and stop; caller can re-trigger)
- Per-instrument waveform auto-detection from PCM sample analysis
- Streaming from disk (entire module loaded into memory — they're tiny)
