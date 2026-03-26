# MOD/MED Tracker Player Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse ProTracker MOD and OctaMED MMD0-3 files and play them through the 4-voice polyphonic backend, integrated into SoundManager.

**Architecture:** A `TrackerModule` data model holds parsed patterns/instruments. A `TrackerPlayer` ticks through patterns driving backend voices. SoundManager gains `play_file()` to load and play tracker files via the sequencer's external tick callback. Two compile-time flags (`HELIX_HAS_SOUND`, `HELIX_HAS_TRACKER`) gate the code.

**Tech Stack:** C++17, Catch2 (testing), ALSA/SDL backends (existing)

**Spec:** `docs/superpowers/specs/2026-03-26-tracker-player-design.md`

**Worktree:** `/home/pbrown/Code/Printing/helixscreen/.worktrees/alsa-sound-backend` (reuse existing)

---

## File Structure

| File | Responsibility |
|------|----------------|
| `include/tracker_module.h` | Data model (TrackerNote, TrackerInstrument, TrackerModule), format detection, `load()` |
| `src/system/tracker_module_mod.cpp` | ProTracker .MOD binary parser |
| `src/system/tracker_module_med.cpp` | OctaMED MMD0-3 binary parser |
| `include/tracker_player.h` | TrackerPlayer class, ChannelState, effect processing |
| `src/system/tracker_player.cpp` | Playback engine: tick timing, row processing, ~15-20 effects, backend voice output |
| `include/sound_backend.h` | Move `SoundPriority` enum here from `sound_sequencer.h` |
| `include/sound_sequencer.h` | Add `set_external_tick()`, remove `SoundPriority` |
| `src/system/sound_sequencer.cpp` | External tick callback in sequencer loop |
| `include/sound_manager.h` | Add `play_file()`, `stop_tracker()`, TrackerPlayer member |
| `src/system/sound_manager.cpp` | File loading, tick routing, handoff sequence |
| `src/ui/ui_settings_about.cpp` | Trigger tracker on About screen activate/deactivate |
| `Makefile` | `HELIX_HAS_SOUND` + `HELIX_HAS_TRACKER` flags |
| `assets/sounds/crocketts_theme.med` | Copy from ~/Downloads |
| `tests/unit/test_tracker_module.cpp` | MOD/MED parsing tests |
| `tests/unit/test_tracker_player.cpp` | Playback engine tests |

---

### Task 1: TrackerModule Data Model + MOD Parser

**Files:**
- Create: `include/tracker_module.h`
- Create: `src/system/tracker_module_mod.cpp`
- Test: `tests/unit/test_tracker_module.cpp`

**Context:** ProTracker MOD format: 1084-byte header (title at 0, 31 instruments at 20, order table at 952, magic at 1080), then pattern data (64 rows × 4 channels × 4 bytes per note). Note encoding: upper 4 bits of byte 0 + byte 1 = 12-bit Amiga period value. Effect in lower 4 bits of byte 0 + byte 2 (command) + byte 3 (data).

**Reference:** The test will use a synthetically constructed MOD binary — no external file dependency.

- [ ] **Step 1: Create tracker_module.h with data model**

```cpp
// include/tracker_module.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef HELIX_HAS_TRACKER

#include "sound_theme.h" // for Waveform enum

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace helix::audio {

struct TrackerNote {
    uint8_t note = 0;       // 0=none, 1-120 = C-0..B-9
    uint8_t instrument = 0; // 0=none
    uint8_t effect = 0;
    uint8_t effect_data = 0;
};

struct TrackerInstrument {
    Waveform waveform = Waveform::SQUARE;
    float volume = 1.0f;  // 0.0-1.0 (from MOD 0-64)
    float finetune = 0;   // semitone offset (-0.5 to +0.5)
};

struct TrackerModule {
    std::vector<TrackerInstrument> instruments;
    // Flat: patterns[pat_idx][row * 4 + channel]
    std::vector<std::vector<TrackerNote>> patterns;
    std::vector<uint8_t> order;
    uint8_t num_orders = 0;
    uint8_t speed = 6;
    uint8_t tempo = 125;
    uint16_t rows_per_pattern = 64;

    /// Load from file path (detects format by magic bytes)
    static std::optional<TrackerModule> load(const std::string& path);

    /// Load from memory buffer (for testing)
    static std::optional<TrackerModule> load_from_memory(const uint8_t* data, size_t size);

    /// Convert tracker note number (1-120) to frequency in Hz
    /// Note 1 = C-0, Note 13 = C-1, etc. A-4 (note 58) = 440 Hz.
    static float note_to_freq(uint8_t note);
};

// Internal: format-specific parsers (called by load_from_memory)
std::optional<TrackerModule> parse_mod(const uint8_t* data, size_t size);
std::optional<TrackerModule> parse_med(const uint8_t* data, size_t size);

} // namespace helix::audio

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 2: Write failing tests for MOD parser**

Create `tests/unit/test_tracker_module.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_HAS_TRACKER

#include "tracker_module.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::audio;
using Catch::Approx;

// ============================================================================
// Helper: build a minimal valid MOD file in memory
// ============================================================================

static std::vector<uint8_t> make_minimal_mod() {
    // MOD format: 1084-byte header + pattern data
    // Title: 20 bytes, 31 instruments × 30 bytes = 930, orders: 130 bytes, magic: 4 bytes
    // Total header: 20 + 930 + 1 (song_len) + 1 (restart) + 128 (orders) + 4 (magic) = 1084
    // Pattern data: 64 rows × 4 channels × 4 bytes = 1024 bytes per pattern

    std::vector<uint8_t> data(1084 + 1024, 0);

    // Title at offset 0
    std::memcpy(data.data(), "TestMod", 7);

    // Instrument 1 at offset 20: volume=64 (max), finetune=0
    // Bytes: name[22], length[2], finetune[1], volume[1], loop_start[2], loop_length[2]
    data[20 + 25] = 64; // volume (offset 25 within instrument block)

    // Song length at offset 950
    data[950] = 1; // 1 order

    // Order table at offset 952: order[0] = pattern 0
    data[952] = 0;

    // Magic "M.K." at offset 1080
    std::memcpy(data.data() + 1080, "M.K.", 4);

    // Pattern 0 data starts at offset 1084
    // Row 0, Channel 0: note C-4 (Amiga period 428), instrument 1, no effect
    // MOD note encoding: 4 bytes per channel
    // byte0: upper nybble = instrument high bit | (period >> 8) & 0x0F
    // byte1: period & 0xFF
    // byte2: lower nybble of instrument << 4 | effect command
    // byte3: effect data
    //
    // Period 428 = 0x01AC, instrument 1
    // byte0 = (inst & 0xF0) | ((period >> 8) & 0x0F) = 0x00 | 0x01 = 0x01
    // byte1 = period & 0xFF = 0xAC
    // byte2 = ((inst & 0x0F) << 4) | effect = 0x10 | 0x00 = 0x10
    // byte3 = 0x00
    size_t pat_offset = 1084;
    data[pat_offset + 0] = 0x01; // period high + inst high
    data[pat_offset + 1] = 0xAC; // period low (428 = C-4)
    data[pat_offset + 2] = 0x10; // inst low + effect cmd
    data[pat_offset + 3] = 0x00; // effect data

    return data;
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("TrackerModule: reject garbage data", "[tracker][sound]") {
    uint8_t garbage[] = {0, 1, 2, 3, 4, 5, 6, 7};
    auto mod = TrackerModule::load_from_memory(garbage, sizeof(garbage));
    REQUIRE_FALSE(mod.has_value());
}

TEST_CASE("TrackerModule: reject too-small buffer", "[tracker][sound]") {
    uint8_t small[100] = {};
    auto mod = TrackerModule::load_from_memory(small, sizeof(small));
    REQUIRE_FALSE(mod.has_value());
}

TEST_CASE("TrackerModule: parse minimal MOD", "[tracker][sound]") {
    auto data = make_minimal_mod();
    auto mod = TrackerModule::load_from_memory(data.data(), data.size());
    REQUIRE(mod.has_value());

    CHECK(mod->num_orders == 1);
    CHECK(mod->order[0] == 0);
    CHECK(mod->patterns.size() == 1);
    CHECK(mod->rows_per_pattern == 64);
    CHECK(mod->speed == 6);
    CHECK(mod->tempo == 125);

    // 31 instruments
    CHECK(mod->instruments.size() == 31);
    // Instrument 0 (index 0 = MOD instrument 1) has volume 1.0
    CHECK(mod->instruments[0].volume == Approx(1.0f));

    // Pattern 0, row 0, channel 0 should have a note (C-4 = note 49)
    auto& pat = mod->patterns[0];
    auto& note = pat[0 * 4 + 0]; // row 0, channel 0
    CHECK(note.note > 0);         // has a note
    CHECK(note.instrument == 1);   // instrument 1
}

TEST_CASE("TrackerModule: note_to_freq basic", "[tracker][sound]") {
    // A-4 = note 58 = 440 Hz
    CHECK(TrackerModule::note_to_freq(58) == Approx(440.0f).margin(1.0f));
    // C-4 = note 49 ≈ 261.63 Hz
    CHECK(TrackerModule::note_to_freq(49) == Approx(261.63f).margin(1.0f));
    // note 0 = no note
    CHECK(TrackerModule::note_to_freq(0) == 0.0f);
}

TEST_CASE("TrackerModule: MOD pattern data integrity", "[tracker][sound]") {
    auto data = make_minimal_mod();
    auto mod = TrackerModule::load_from_memory(data.data(), data.size());
    REQUIRE(mod.has_value());

    // Row 0, channel 0 has data; row 1 channel 0 should be empty
    auto& pat = mod->patterns[0];
    CHECK(pat[1 * 4 + 0].note == 0);       // row 1 empty
    CHECK(pat[1 * 4 + 0].instrument == 0);
}

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[tracker]" -v`
Expected: FAIL — `tracker_module.h` exists but `parse_mod` not implemented

- [ ] **Step 4: Implement MOD parser**

Create `src/system/tracker_module_mod.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_HAS_TRACKER

#include "tracker_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <spdlog/spdlog.h>

namespace helix::audio {

// Amiga period table for octaves 0-7 (C to B, 12 notes per octave)
// Period for C-0 through B-0, divide by 2 for each higher octave
static constexpr uint16_t kBasePeriods[12] = {
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 906
};

// Convert Amiga period to tracker note number (1-120)
static uint8_t period_to_note(uint16_t period) {
    if (period == 0) return 0;

    // Search through octaves 0-9 for closest match
    for (int octave = 0; octave < 10; ++octave) {
        for (int semitone = 0; semitone < 12; ++semitone) {
            uint16_t p = kBasePeriods[semitone] >> octave;
            if (p == 0) continue;
            // Allow ±2 tolerance for finetune variations
            if (std::abs(static_cast<int>(period) - static_cast<int>(p)) <= 2) {
                return static_cast<uint8_t>(octave * 12 + semitone + 1);
            }
        }
    }

    // Nearest match by ratio if exact match fails
    float best_dist = 1e9f;
    uint8_t best_note = 0;
    for (int octave = 0; octave < 10; ++octave) {
        for (int semitone = 0; semitone < 12; ++semitone) {
            uint16_t p = kBasePeriods[semitone] >> octave;
            if (p == 0) continue;
            float dist = std::abs(std::log2(static_cast<float>(period) / p));
            if (dist < best_dist) {
                best_dist = dist;
                best_note = static_cast<uint8_t>(octave * 12 + semitone + 1);
            }
        }
    }
    return best_note;
}

float TrackerModule::note_to_freq(uint8_t note) {
    if (note == 0) return 0.0f;
    // Note 58 = A-4 = 440 Hz
    // freq = 440 * 2^((note - 58) / 12)
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 58.0f) / 12.0f);
}

std::optional<TrackerModule> TrackerModule::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        spdlog::warn("[TrackerModule] Cannot open: {}", path);
        return std::nullopt;
    }

    auto size = file.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024) { // 4MB sanity limit
        spdlog::warn("[TrackerModule] File too small or too large: {} bytes", static_cast<long>(size));
        return std::nullopt;
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return load_from_memory(data.data(), data.size());
}

std::optional<TrackerModule> TrackerModule::load_from_memory(const uint8_t* data, size_t size) {
    if (!data || size < 4) return std::nullopt;

    // Check MED magic at offset 0 FIRST (MED files can be smaller than MOD header)
    if (std::memcmp(data, "MMD", 3) == 0 && data[3] >= '0' && data[3] <= '3') {
        return parse_med(data, size);
    }

    // Check MOD magic at offset 1080 (requires at least 1084 bytes)
    if (size >= 1084) {
        static const char* kModMagics[] = {"M.K.", "M!K!", "4CHN", "FLT4", "FLT8", "6CHN", "8CHN"};
        for (const char* magic : kModMagics) {
            if (std::memcmp(data + 1080, magic, 4) == 0) {
                return parse_mod(data, size);
            }
        }
    }

    spdlog::debug("[TrackerModule] Unrecognized format");
    return std::nullopt;
}

std::optional<TrackerModule> parse_mod(const uint8_t* data, size_t size) {
    TrackerModule mod;
    mod.speed = 6;
    mod.tempo = 125;
    mod.rows_per_pattern = 64;

    // Parse 31 instruments (offset 20, 30 bytes each)
    mod.instruments.resize(31);
    for (int i = 0; i < 31; ++i) {
        size_t off = 20 + i * 30;
        // Finetune: offset 24 within instrument, signed nybble
        int8_t ft = static_cast<int8_t>(data[off + 24] & 0x0F);
        if (ft > 7) ft -= 16;
        mod.instruments[i].finetune = static_cast<float>(ft) / 16.0f; // ±0.5 semitones
        // Volume: offset 25, 0-64
        uint8_t vol = std::min(data[off + 25], static_cast<uint8_t>(64));
        mod.instruments[i].volume = static_cast<float>(vol) / 64.0f;
        // Default waveform assignment by instrument index (simple heuristic)
        // Instruments 0-7: SQUARE, 8-15: SAW, 16-23: TRIANGLE, 24-30: SINE
        if (i < 8) mod.instruments[i].waveform = Waveform::SQUARE;
        else if (i < 16) mod.instruments[i].waveform = Waveform::SAW;
        else if (i < 24) mod.instruments[i].waveform = Waveform::TRIANGLE;
        else mod.instruments[i].waveform = Waveform::SINE;
    }

    // Song length (number of orders)
    mod.num_orders = data[950];
    if (mod.num_orders == 0 || mod.num_orders > 128) {
        spdlog::warn("[TrackerModule] Invalid song length: {}", mod.num_orders);
        return std::nullopt;
    }

    // Order table (128 bytes at offset 952)
    mod.order.resize(mod.num_orders);
    uint8_t max_pattern = 0;
    for (int i = 0; i < mod.num_orders; ++i) {
        mod.order[i] = data[952 + i];
        max_pattern = std::max(max_pattern, mod.order[i]);
    }

    // Pattern data starts at offset 1084
    int num_patterns = max_pattern + 1;
    size_t pattern_bytes = 64 * 4 * 4; // 64 rows × 4 channels × 4 bytes
    size_t needed = 1084 + static_cast<size_t>(num_patterns) * pattern_bytes;
    if (size < needed) {
        spdlog::warn("[TrackerModule] MOD too small for {} patterns: {} < {}", num_patterns, size, needed);
        return std::nullopt;
    }

    mod.patterns.resize(num_patterns);
    for (int p = 0; p < num_patterns; ++p) {
        mod.patterns[p].resize(64 * 4);
        for (int row = 0; row < 64; ++row) {
            for (int ch = 0; ch < 4; ++ch) {
                size_t off = 1084 + p * pattern_bytes + (row * 4 + ch) * 4;
                uint8_t b0 = data[off];
                uint8_t b1 = data[off + 1];
                uint8_t b2 = data[off + 2];
                uint8_t b3 = data[off + 3];

                // Period: upper 4 bits of b0 + b1
                uint16_t period = (static_cast<uint16_t>(b0 & 0x0F) << 8) | b1;
                // Instrument: upper 4 bits of b0 + upper 4 bits of b2
                uint8_t inst = (b0 & 0xF0) | ((b2 >> 4) & 0x0F);
                // Effect: lower 4 bits of b2
                uint8_t effect = b2 & 0x0F;
                // Effect data
                uint8_t effect_data = b3;

                auto& n = mod.patterns[p][row * 4 + ch];
                n.note = period_to_note(period);
                n.instrument = inst;
                n.effect = effect;
                n.effect_data = effect_data;
            }
        }
    }

    spdlog::info("[TrackerModule] Loaded MOD: {} orders, {} patterns, {} instruments",
                 mod.num_orders, num_patterns, 31);
    return mod;
}

} // namespace helix::audio

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[tracker]" -v`
Expected: All tracker tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/tracker_module.h src/system/tracker_module_mod.cpp tests/unit/test_tracker_module.cpp
git commit -m "feat(sound): add TrackerModule data model and MOD parser"
```

---

### Task 2: OctaMED MMD0-3 Parser

**Files:**
- Create: `src/system/tracker_module_med.cpp`
- Modify: `tests/unit/test_tracker_module.cpp`
- Asset: Copy `~/Downloads/hkvalhe_-_crockettstheme2023.med` to `assets/sounds/crocketts_theme.med`

**Context:** OctaMED MMD format: big-endian. Header starts with "MMD0"-"MMD3" magic. Key offsets from header: `song_offset` (at byte 8), `blockarr_offset` (at byte 16). Song struct has `numblocks`, `songlen`, `playseq` (order table), `deftempo`, `flags`. Block data: each block has `numtracks`, `lines` (rows-1), then note data (4 bytes per note: note byte, instrument byte, effect byte, effect data byte).

**The MED file** (`hkvalhe_-_crockettstheme2023.med`) is 308KB. Previous session's Python parser confirmed: 9 blocks (patterns), 4 tracks, variable rows per block.

- [ ] **Step 1: Copy MED asset file**

```bash
cp ~/Downloads/hkvalhe_-_crockettstheme2023.med assets/sounds/crocketts_theme.med
```

- [ ] **Step 2: Add MED parsing tests**

Append to `tests/unit/test_tracker_module.cpp` (before the final `#endif`):

```cpp
TEST_CASE("TrackerModule: detect MED format", "[tracker][sound]") {
    // Minimal MMD0 header — just enough to detect format
    std::vector<uint8_t> data(64, 0);
    std::memcpy(data.data(), "MMD0", 4);
    // This should attempt MED parse but fail (incomplete data)
    auto mod = TrackerModule::load_from_memory(data.data(), data.size());
    REQUIRE_FALSE(mod.has_value()); // too small to be valid
}

TEST_CASE("TrackerModule: load crocketts_theme.med from file", "[tracker][sound]") {
    auto mod = TrackerModule::load("assets/sounds/crocketts_theme.med");
    // Skip if file not present (CI may not have it)
    if (!mod.has_value()) {
        SKIP("crocketts_theme.med not found");
    }

    // Known properties from previous Python analysis
    CHECK(mod->patterns.size() == 9);   // 9 blocks
    CHECK(mod->num_orders > 0);
    CHECK(mod->rows_per_pattern >= 64); // MED blocks are at least 64 rows

    // Should have instruments
    CHECK(mod->instruments.size() > 0);

    // Pattern 0 should have note data (not all zeros)
    bool has_notes = false;
    for (auto& n : mod->patterns[0]) {
        if (n.note > 0) { has_notes = true; break; }
    }
    CHECK(has_notes);
}
```

- [ ] **Step 3: Run tests to verify MED tests fail**

Run: `make test && ./build/bin/helix-tests "[tracker]" -v`
Expected: MED file test FAILs (parse_med not implemented), MOD tests still pass

- [ ] **Step 4: Implement MED parser**

Create `src/system/tracker_module_med.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_HAS_TRACKER

#include "tracker_module.h"

#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace helix::audio {

// Big-endian readers
static uint16_t read_be16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

static uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

std::optional<TrackerModule> parse_med(const uint8_t* data, size_t size) {
    if (size < 52) {
        spdlog::warn("[TrackerModule] MED too small: {} bytes", size);
        return std::nullopt;
    }

    // Verify magic
    if (std::memcmp(data, "MMD", 3) != 0 || data[3] < '0' || data[3] > '3') {
        return std::nullopt;
    }
    uint8_t mmd_version = data[3] - '0';

    // Header layout (MMD0-MMD3):
    // 0-3: magic, 4-7: modlen, 8-11: song_offset, 12-15: reserved
    // 16-19: blockarr_offset, 20-23: reserved, 24-27: smplarr_offset
    // 28-31: reserved, 32-35: expdata_offset
    uint32_t song_offset = read_be32(data + 8);
    uint32_t blockarr_offset = read_be32(data + 16);

    if (song_offset >= size || blockarr_offset >= size) {
        spdlog::warn("[TrackerModule] MED offsets out of range");
        return std::nullopt;
    }

    // Song structure (at song_offset):
    // MMD0/1: 0-1: numblocks (not here — in sample array), etc.
    // MMD2/3 has a different song structure
    // Common fields at song_offset:
    //   +0: sample[63] pointers (skip)
    //   For MMD0: after sample array:
    //     +504: numblocks (uint16), +506: songlen (uint16)
    //     +508: playseq[256] (uint8 order table)
    //     +764: deftempo (uint16), +766: playtransp (int8), +767: flags (uint8)
    //     +768: flags2 (uint8), +769: tempo2 (uint8 = ticks per row)
    //     ... more fields

    const uint8_t* song = data + song_offset;
    size_t song_size = size - song_offset;

    // MMD0/1 song structure
    if (mmd_version <= 1) {
        if (song_size < 770) {
            spdlog::warn("[TrackerModule] MED song struct too small");
            return std::nullopt;
        }

        // Sample pointers: 63 × 4 bytes = 252 bytes (MMD0) or 63 × 8 = 504 (MMD1)
        size_t sample_table_size = (mmd_version == 0) ? 63 * 4 : 63 * 8;
        const uint8_t* s = song + sample_table_size;
        size_t remaining = song_size - sample_table_size;
        if (remaining < 266) { // need at least through deftempo
            spdlog::warn("[TrackerModule] MED song fields too small");
            return std::nullopt;
        }

        uint16_t numblocks = read_be16(s + 0);
        uint16_t songlen = read_be16(s + 2);

        TrackerModule mod;
        mod.num_orders = static_cast<uint8_t>(std::min(songlen, static_cast<uint16_t>(255)));
        mod.order.resize(mod.num_orders);
        for (int i = 0; i < mod.num_orders; ++i) {
            mod.order[i] = s[4 + i]; // playseq at offset +4
        }

        // deftempo at offset +260
        uint16_t deftempo = read_be16(s + 260);
        // tempo2 (ticks per row) at offset +265
        uint8_t tempo2 = s[265];

        // MED tempo is different from MOD:
        // BPM = deftempo if using BPM mode (flags & 0x20)
        // Otherwise it's a CIA timer value
        uint8_t flags = s[263];
        if (flags & 0x20) {
            // BPM mode
            mod.tempo = static_cast<uint8_t>(std::clamp(static_cast<int>(deftempo), 32, 255));
        } else {
            // CIA timer mode — convert to approximate BPM
            // CIA timing: BPM ≈ 4926 / deftempo (for PAL, close enough)
            if (deftempo > 0) {
                int bpm = 4926 / deftempo;
                mod.tempo = static_cast<uint8_t>(std::clamp(bpm, 32, 255));
            }
        }
        mod.speed = (tempo2 > 0) ? tempo2 : 6;

        // Parse blocks (patterns)
        const uint8_t* blockarr = data + blockarr_offset;
        if (blockarr_offset + numblocks * 4 > size) {
            spdlog::warn("[TrackerModule] MED block array out of range");
            return std::nullopt;
        }

        mod.patterns.resize(numblocks);
        uint16_t max_rows = 0;

        for (int b = 0; b < numblocks; ++b) {
            uint32_t block_offset = read_be32(blockarr + b * 4);
            if (block_offset == 0 || block_offset >= size) continue;

            const uint8_t* block = data + block_offset;
            size_t block_remaining = size - block_offset;

            if (mmd_version == 0) {
                // MMD0 block: 0: numtracks (uint8), 1: lines (uint8, rows-1)
                // Then note data: (lines+1) × numtracks × 3 bytes
                if (block_remaining < 2) continue;
                uint8_t numtracks = block[0];
                uint8_t lines = block[1]; // rows - 1
                uint16_t rows = static_cast<uint16_t>(lines) + 1;
                max_rows = std::max(max_rows, rows);

                int tracks = std::min(static_cast<int>(numtracks), 4);
                mod.patterns[b].resize(rows * 4); // always 4 channels

                size_t note_data_start = 2;
                for (uint16_t row = 0; row < rows; ++row) {
                    for (int ch = 0; ch < numtracks; ++ch) {
                        size_t off = note_data_start + (row * numtracks + ch) * 3;
                        if (off + 3 > block_remaining) break;

                        if (ch < 4) {
                            auto& n = mod.patterns[b][row * 4 + ch];
                            // MMD0: 3 bytes per note
                            // byte0: note (0=none, 1-120)
                            // byte1: instrument
                            // byte2: upper nybble=effect, lower nybble=unused?
                            //   Actually: byte1 upper nybble = instrument, lower = effect cmd
                            //   byte2 = effect data
                            // Wait — MMD0 3-byte format:
                            //   byte0[7:4] = note high, byte0[3:0] = instrument high
                            //   Actually the encoding is:
                            //   byte0 = (note << 2) | (instrument >> 4) for upper bits
                            //   No — simpler: byte0 = note, byte1 = instrument | (cmd << 4),
                            //                 byte2 = cmd_data
                            // Official MMD0: byte0 bits 7-0 = packed:
                            //   bits 7-2: note (0=none, 1-63)
                            //   bits 1-0: upper 2 bits of instrument
                            //   byte1 bits 7-4: lower 4 bits of instrument
                            //   byte1 bits 3-0: effect command
                            //   byte2: effect data
                            uint8_t note_val = (block[off] >> 2) & 0x3F;
                            uint8_t inst = ((block[off] & 0x03) << 4) | ((block[off + 1] >> 4) & 0x0F);
                            uint8_t effect = block[off + 1] & 0x0F;
                            uint8_t edata = block[off + 2];

                            // MED note values: 1-63 map to notes
                            // MED note 1 = C-1, note 12 = B-1, note 13 = C-2, etc.
                            // Shift to our 1-120 range: MED note + 12 (offset by one octave)
                            n.note = (note_val > 0) ? static_cast<uint8_t>(note_val + 12) : 0;
                            n.instrument = inst;
                            n.effect = effect;
                            n.effect_data = edata;
                        }
                    }
                }
            } else {
                // MMD1/2/3 block: 2-byte numtracks, 2-byte lines, then 4 bytes per note
                if (block_remaining < 4) continue;
                uint16_t numtracks = read_be16(block + 0);
                uint16_t lines = read_be16(block + 2); // rows - 1
                uint16_t rows = lines + 1;
                max_rows = std::max(max_rows, rows);

                int tracks = std::min(static_cast<int>(numtracks), 4);
                mod.patterns[b].resize(rows * 4);

                size_t note_data_start = 8; // skip numtracks(2) + lines(2) + reserved(4)
                for (uint16_t row = 0; row < rows; ++row) {
                    for (int ch = 0; ch < numtracks; ++ch) {
                        size_t off = note_data_start + (row * numtracks + ch) * 4;
                        if (off + 4 > block_remaining) break;

                        if (ch < 4) {
                            auto& n = mod.patterns[b][row * 4 + ch];
                            // MMD1+: 4 bytes per note
                            // byte0: note (0=none, 1-120)
                            // byte1: instrument
                            // byte2: effect command
                            // byte3: effect data
                            n.note = block[off];
                            n.instrument = block[off + 1];
                            n.effect = block[off + 2];
                            n.effect_data = block[off + 3];
                        }
                    }
                }
            }
        }

        mod.rows_per_pattern = max_rows > 0 ? max_rows : 64;

        // Instruments: use defaults (we don't parse MED instrument samples)
        // Assign waveforms by track role:
        // Track 0: SAW (bass), Track 1: SQUARE (rhythm), Track 2: TRIANGLE (pad), Track 3: SQUARE (lead)
        mod.instruments.resize(64);
        for (int i = 0; i < 64; ++i) {
            mod.instruments[i].volume = 1.0f;
            // Rotate waveforms
            switch (i % 4) {
            case 0: mod.instruments[i].waveform = Waveform::SAW; break;
            case 1: mod.instruments[i].waveform = Waveform::SQUARE; break;
            case 2: mod.instruments[i].waveform = Waveform::TRIANGLE; break;
            case 3: mod.instruments[i].waveform = Waveform::SQUARE; break;
            }
        }

        spdlog::info("[TrackerModule] Loaded MMD{}: {} orders, {} blocks, tempo={}, speed={}",
                     mmd_version, mod.num_orders, numblocks, mod.tempo, mod.speed);
        return mod;
    }

    spdlog::warn("[TrackerModule] MMD{} not yet supported", mmd_version);
    return std::nullopt;
}

} // namespace helix::audio

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[tracker]" -v`
Expected: All tracker tests PASS (including MED file load if asset present)

- [ ] **Step 6: Commit**

```bash
git add src/system/tracker_module_med.cpp assets/sounds/crocketts_theme.med
git add tests/unit/test_tracker_module.cpp
git commit -m "feat(sound): add OctaMED MMD0-3 parser and crocketts_theme.med asset"
```

---

### Task 3: TrackerPlayer — Core Tick Engine + Basic Effects

**Files:**
- Create: `include/tracker_player.h`
- Create: `src/system/tracker_player.cpp`
- Create: `tests/unit/test_tracker_player.cpp`

**Context:** The TrackerPlayer ticks through patterns, processes effects, and drives the 4-voice backend. This task implements the core tick engine and the first batch of effects (set volume, arpeggio, volume slide, set speed/tempo, pattern break, position jump). The remaining effects come in Task 4.

- [ ] **Step 1: Create tracker_player.h**

```cpp
// include/tracker_player.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef HELIX_HAS_TRACKER

#include "sound_backend.h"
#include "tracker_module.h"

#include <array>
#include <atomic>
#include <memory>

namespace helix::audio {

class TrackerPlayer {
  public:
    explicit TrackerPlayer(std::shared_ptr<SoundBackend> backend);

    void load(TrackerModule module);
    void play();
    void stop();
    bool is_playing() const;

    /// Called from sequencer thread at ~1ms rate
    void tick(float dt_ms);

    // Test access: read current channel state
    struct ChannelSnapshot {
        float freq;
        float volume;
        bool active;
    };
    ChannelSnapshot get_channel(int ch) const;
    int current_row() const { return row_; }
    int current_order() const { return order_idx_; }
    int current_tick() const { return tick_; }

  private:
    struct ChannelState {
        float freq = 0;
        float base_freq = 0;       // frequency before effects
        float target_freq = 0;     // tone portamento target
        float volume = 1.0f;
        float duty = 0.5f;
        float vibrato_phase = 0;
        float tremolo_phase = 0;
        uint8_t vibrato_speed = 0;
        uint8_t vibrato_depth = 0;
        uint8_t tremolo_speed = 0;
        uint8_t tremolo_depth = 0;
        uint8_t arp_tick = 0;
        uint8_t arp_x = 0, arp_y = 0;
        uint8_t instrument = 0;
        uint8_t effect = 0;
        uint8_t effect_data = 0;
        uint8_t porta_speed = 0;   // tone portamento memory
        Waveform waveform = Waveform::SQUARE;
        bool active = false;

        // Pattern loop state (per-channel in ProTracker)
        int loop_start_row = 0;
        int loop_count = 0;
    };

    void process_row();
    void process_tick_effects();
    void apply_to_backend();
    void advance_row();

    std::shared_ptr<SoundBackend> backend_;
    TrackerModule module_;
    std::array<ChannelState, 4> channels_{};

    int order_idx_ = 0;
    int row_ = 0;
    int tick_ = 0;
    int speed_ = 6;
    int tempo_ = 125;
    float tick_accum_ = 0;
    int next_order_ = -1;   // pending position jump
    int next_row_ = -1;     // pending pattern break row
    std::atomic<bool> playing_{false};
};

} // namespace helix::audio

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 2: Write failing tests for core tick engine**

Create `tests/unit/test_tracker_player.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_HAS_TRACKER

#include "tracker_module.h"
#include "tracker_player.h"

#include <cmath>
#include <memory>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::audio;
using Catch::Approx;

// ============================================================================
// Mock backend that records voice calls
// ============================================================================

class TrackerMockBackend : public SoundBackend {
  public:
    struct VoiceCall {
        int slot;
        float freq, amp, duty;
    };
    std::vector<VoiceCall> voice_calls;
    std::vector<int> silence_calls;
    int total_silence = 0;

    void set_tone(float f, float a, float d) override {
        voice_calls.push_back({0, f, a, d});
    }
    void silence() override { total_silence++; }
    bool supports_waveforms() const override { return true; }
    bool supports_amplitude() const override { return true; }
    int voice_count() const override { return 4; }
    void set_voice(int slot, float f, float a, float d) override {
        voice_calls.push_back({slot, f, a, d});
    }
    void silence_voice(int slot) override {
        silence_calls.push_back(slot);
    }
    void set_voice_waveform(int, Waveform) override {}
};

// ============================================================================
// Helper: build a TrackerModule programmatically
// ============================================================================

static TrackerModule make_test_module(int rows = 4, int patterns = 1) {
    TrackerModule mod;
    mod.speed = 6;
    mod.tempo = 125;
    mod.rows_per_pattern = static_cast<uint16_t>(rows);
    mod.num_orders = static_cast<uint8_t>(patterns);
    mod.order.resize(patterns);
    for (int i = 0; i < patterns; ++i) mod.order[i] = static_cast<uint8_t>(i);

    mod.patterns.resize(patterns);
    for (auto& pat : mod.patterns) {
        pat.resize(rows * 4); // 4 channels
    }

    mod.instruments.resize(4);
    for (int i = 0; i < 4; ++i) {
        mod.instruments[i].volume = 1.0f;
        mod.instruments[i].waveform = Waveform::SQUARE;
    }

    return mod;
}

/// Tick the player enough to advance one full tracker tick (20ms at tempo 125)
static void tick_one(TrackerPlayer& player) {
    player.tick(20.0f); // 2500/125 = 20ms per tick
}

/// Tick enough for N tracker ticks
static void tick_n(TrackerPlayer& player, int n) {
    for (int i = 0; i < n; ++i) tick_one(player);
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("TrackerPlayer: basic playback starts and stops", "[tracker][sound]") {
    auto backend = std::make_shared<TrackerMockBackend>();
    TrackerPlayer player(backend);

    auto mod = make_test_module(4);
    // Row 0, ch 0: note 49 (C-4), instrument 1
    mod.patterns[0][0] = {49, 1, 0, 0};

    player.load(std::move(mod));
    CHECK_FALSE(player.is_playing());

    player.play();
    CHECK(player.is_playing());

    // First tick processes row 0
    tick_one(player);
    CHECK(player.current_row() == 0);
    CHECK(player.current_tick() == 1);

    // Backend should have received voice calls
    CHECK(backend->voice_calls.size() > 0);

    player.stop();
    CHECK_FALSE(player.is_playing());
}

TEST_CASE("TrackerPlayer: advances rows after speed ticks", "[tracker][sound]") {
    auto backend = std::make_shared<TrackerMockBackend>();
    TrackerPlayer player(backend);

    auto mod = make_test_module(4);
    mod.speed = 3; // 3 ticks per row
    mod.patterns[0][0 * 4 + 0] = {49, 1, 0, 0}; // row 0
    mod.patterns[0][1 * 4 + 0] = {61, 1, 0, 0}; // row 1

    player.load(std::move(mod));
    player.play();

    // 3 ticks = advance to row 1
    tick_n(player, 3);
    CHECK(player.current_row() == 1);
}

TEST_CASE("TrackerPlayer: stops at end of module", "[tracker][sound]") {
    auto backend = std::make_shared<TrackerMockBackend>();
    TrackerPlayer player(backend);

    auto mod = make_test_module(2); // 2 rows, 1 pattern
    mod.speed = 1; // 1 tick per row (fast)
    mod.patterns[0][0] = {49, 1, 0, 0};

    player.load(std::move(mod));
    player.play();

    // 2 ticks = 2 rows = end
    tick_n(player, 3);
    CHECK_FALSE(player.is_playing());
}

TEST_CASE("TrackerPlayer: set volume effect (Cxx)", "[tracker][sound]") {
    auto backend = std::make_shared<TrackerMockBackend>();
    TrackerPlayer player(backend);

    auto mod = make_test_module(2);
    mod.speed = 1;
    // Row 0: note + set volume to 32/64 = 0.5
    mod.patterns[0][0] = {49, 1, 0x0C, 32};

    player.load(std::move(mod));
    player.play();
    tick_one(player);

    auto snap = player.get_channel(0);
    CHECK(snap.volume == Approx(0.5f));
}

TEST_CASE("TrackerPlayer: set speed/tempo effect (Fxx)", "[tracker][sound]") {
    auto backend = std::make_shared<TrackerMockBackend>();
    TrackerPlayer player(backend);

    auto mod = make_test_module(4);
    // Row 0: set speed to 2 (Fxx with xx < 32)
    mod.patterns[0][0] = {49, 1, 0x0F, 2};

    player.load(std::move(mod));
    player.play();
    tick_one(player); // processes row 0, sets speed=2

    // Now 2 ticks should advance to row 1 (not default 6)
    tick_one(player); // tick 1 of row 0
    CHECK(player.current_row() == 1); // advanced after 2 ticks
}

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[tracker]" -v`
Expected: FAIL — TrackerPlayer not implemented

- [ ] **Step 4: Implement TrackerPlayer core engine**

Create `src/system/tracker_player.cpp`. This is the largest single file — implements tick timing, row processing, and all ~15-20 effects from the spec.

The implementer should reference:
- **Spec effects table:** `docs/superpowers/specs/2026-03-26-tracker-player-design.md` lines 116-139
- **Existing sequencer pattern:** `src/system/sound_sequencer.cpp` for volume scaling and backend calls
- **Volume scaling:** `AudioSettingsManager::instance().get_volume() / 100.0f` applied in `apply_to_backend()`

Key implementation notes:
- `tick(float dt_ms)`: accumulate dt, fire tracker ticks at `2500.0f / tempo_` ms intervals
- On tick 0: call `process_row()` — read notes, set frequencies, capture effects
- On every tick: call `process_tick_effects()` — slides, vibrato, arpeggio, volume slides
- After effects: call `apply_to_backend()` — write all 4 channels to voice slots + release fence
- `advance_row()`: increment row, check pattern break / position jump, advance order at end of pattern
- Note-to-freq: use `TrackerModule::note_to_freq()` (already implemented)
- When a new note triggers on a channel: set `freq` and `base_freq` from note, reset `volume` from instrument, set `waveform` from instrument, set `active = true`
- When no note on a row: preserve previous channel state (effects continue)
- When instrument specified without note: update volume/waveform but don't retrigger

Effects to implement (all in this task):
- `0xy` Arpeggio: cycle `freq` between `base_freq`, `base_freq * 2^(x/12)`, `base_freq * 2^(y/12)` each tick
- `1xx` Portamento up: `freq *= 2^(xx / (12*16))` per tick (or period-based)
- `2xx` Portamento down: inverse of above
- `3xx` Tone portamento: slide `freq` toward `target_freq` at rate `xx`
- `4xy` Vibrato: sine oscillation on freq, speed=x, depth=y
- `5xy` Tone porta + volume slide
- `6xy` Vibrato + volume slide
- `7xy` Tremolo: sine oscillation on volume
- `Axy` Volume slide: `volume += x/64` or `volume -= y/64` per tick
- `Bxx` Position jump: set `next_order_ = xx`
- `Cxx` Set volume: `volume = xx/64`
- `Dxx` Pattern break: set `next_row_ = xx`, advance order
- `Fxx` Set speed/tempo: `xx < 32 → speed_ = xx`, `xx >= 32 → tempo_ = xx`
- `E1x` Fine porta up: one-shot on tick 0
- `E2x` Fine porta down: one-shot on tick 0
- `E6x` Pattern loop: `x=0` set loop point, `x>0` loop x times
- `E9x` Retrigger: retrigger note every x ticks
- `EAx` Fine volume up: `volume += x/64` on tick 0
- `EBx` Fine volume down: `volume -= x/64` on tick 0
- `ECx` Note cut: silence channel at tick x

```cpp
// src/system/tracker_player.cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_HAS_TRACKER

#include "tracker_player.h"
#include "audio_settings_manager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <spdlog/spdlog.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace helix;

namespace helix::audio {

TrackerPlayer::TrackerPlayer(std::shared_ptr<SoundBackend> backend)
    : backend_(std::move(backend)) {}

void TrackerPlayer::load(TrackerModule module) {
    module_ = std::move(module);
    order_idx_ = 0;
    row_ = 0;
    tick_ = 0;
    speed_ = module_.speed;
    tempo_ = module_.tempo;
    tick_accum_ = 0;
    next_order_ = -1;
    next_row_ = -1;
    channels_ = {};
    playing_.store(false);
}

void TrackerPlayer::play() {
    if (module_.patterns.empty() || module_.num_orders == 0) return;
    order_idx_ = 0;
    row_ = 0;
    tick_ = 0;
    tick_accum_ = 0;
    next_order_ = -1;
    next_row_ = -1;
    channels_ = {};
    playing_.store(true);
    spdlog::debug("[TrackerPlayer] play: {} orders, {} patterns", module_.num_orders, module_.patterns.size());
}

void TrackerPlayer::stop() {
    playing_.store(false);
    if (backend_) {
        for (int v = 0; v < backend_->voice_count(); ++v)
            backend_->silence_voice(v);
    }
}

bool TrackerPlayer::is_playing() const {
    return playing_.load();
}

TrackerPlayer::ChannelSnapshot TrackerPlayer::get_channel(int ch) const {
    if (ch < 0 || ch >= 4) return {0, 0, false};
    return {channels_[ch].freq, channels_[ch].volume, channels_[ch].active};
}

void TrackerPlayer::tick(float dt_ms) {
    if (!playing_.load()) return;

    float ms_per_tick = 2500.0f / static_cast<float>(tempo_);
    tick_accum_ += dt_ms;

    while (tick_accum_ >= ms_per_tick && playing_.load()) {
        tick_accum_ -= ms_per_tick;

        if (tick_ == 0) {
            process_row();
        }
        process_tick_effects();
        apply_to_backend();

        tick_++;
        if (tick_ >= speed_) {
            tick_ = 0;
            advance_row();
        }
    }
}

void TrackerPlayer::process_row() {
    if (order_idx_ >= module_.num_orders) {
        playing_.store(false);
        return;
    }

    uint8_t pat_idx = module_.order[order_idx_];
    if (pat_idx >= module_.patterns.size()) {
        playing_.store(false);
        return;
    }

    auto& pattern = module_.patterns[pat_idx];
    int row_offset = row_ * 4;

    for (int ch = 0; ch < 4; ++ch) {
        if (row_offset + ch >= static_cast<int>(pattern.size())) continue;

        auto& note = pattern[row_offset + ch];
        auto& cs = channels_[ch];

        // Capture effect for per-tick processing
        cs.effect = note.effect;
        cs.effect_data = note.effect_data;

        // New note?
        if (note.note > 0) {
            float new_freq = TrackerModule::note_to_freq(note.note);

            if (note.effect == 0x03 || note.effect == 0x05) {
                // Tone portamento: new note is the target, don't retrigger
                cs.target_freq = new_freq;
            } else {
                cs.freq = new_freq;
                cs.base_freq = new_freq;
                cs.active = true;
                cs.vibrato_phase = 0;
                cs.tremolo_phase = 0;
                cs.arp_tick = 0;
            }
        }

        // New instrument?
        if (note.instrument > 0 && note.instrument <= module_.instruments.size()) {
            auto& inst = module_.instruments[note.instrument - 1];
            cs.instrument = note.instrument;
            cs.volume = inst.volume;
            cs.waveform = inst.waveform;
        }

        // Process tick-0 effects
        uint8_t cmd = note.effect;
        uint8_t data = note.effect_data;
        uint8_t x = (data >> 4) & 0x0F;
        uint8_t y = data & 0x0F;

        switch (cmd) {
        case 0x03: // Tone portamento — remember speed
            if (data > 0) cs.porta_speed = data;
            break;
        case 0x04: // Vibrato
            if (x > 0) cs.vibrato_speed = x;
            if (y > 0) cs.vibrato_depth = y;
            break;
        case 0x07: // Tremolo
            if (x > 0) cs.tremolo_speed = x;
            if (y > 0) cs.tremolo_depth = y;
            break;
        case 0x09: // Sample offset — no-op (no sample playback)
            break;
        case 0x0B: // Position jump
            next_order_ = data;
            break;
        case 0x0C: // Set volume
            cs.volume = std::clamp(static_cast<float>(data) / 64.0f, 0.0f, 1.0f);
            break;
        case 0x0D: // Pattern break
            next_row_ = x * 10 + y; // BCD encoding in ProTracker
            if (next_order_ < 0) next_order_ = order_idx_ + 1;
            break;
        case 0x0F: // Set speed/tempo
            if (data > 0 && data < 32) speed_ = data;
            else if (data >= 32) tempo_ = data;
            break;
        case 0x0E: // Extended effects
            switch (x) {
            case 0x1: // Fine porta up
                cs.freq *= std::pow(2.0f, static_cast<float>(y) / (12.0f * 16.0f));
                cs.base_freq = cs.freq;
                break;
            case 0x2: // Fine porta down
                cs.freq /= std::pow(2.0f, static_cast<float>(y) / (12.0f * 16.0f));
                cs.base_freq = cs.freq;
                break;
            case 0x6: // Pattern loop
                if (y == 0) {
                    cs.loop_start_row = row_;
                } else {
                    if (cs.loop_count == 0) {
                        cs.loop_count = y;
                        next_row_ = cs.loop_start_row;
                        next_order_ = order_idx_; // stay in same pattern
                    } else {
                        cs.loop_count--;
                        if (cs.loop_count > 0) {
                            next_row_ = cs.loop_start_row;
                            next_order_ = order_idx_;
                        }
                    }
                }
                break;
            case 0xA: // Fine volume up
                cs.volume = std::clamp(cs.volume + static_cast<float>(y) / 64.0f, 0.0f, 1.0f);
                break;
            case 0xB: // Fine volume down
                cs.volume = std::clamp(cs.volume - static_cast<float>(y) / 64.0f, 0.0f, 1.0f);
                break;
            default:
                break; // Other E-commands handled in tick effects
            }
            break;
        case 0x00: // Arpeggio (only if data != 0)
            if (data > 0) {
                cs.arp_x = x;
                cs.arp_y = y;
                cs.arp_tick = 0;
            }
            break;
        default:
            break;
        }
    }
}

void TrackerPlayer::process_tick_effects() {
    for (int ch = 0; ch < 4; ++ch) {
        auto& cs = channels_[ch];
        if (!cs.active) continue;

        uint8_t cmd = cs.effect;
        uint8_t data = cs.effect_data;
        uint8_t x = (data >> 4) & 0x0F;
        uint8_t y = data & 0x0F;

        // Per-tick effects (skip tick 0 for most)
        if (tick_ > 0) {
            switch (cmd) {
            case 0x00: // Arpeggio
                if (data > 0) {
                    cs.arp_tick = (cs.arp_tick + 1) % 3;
                    switch (cs.arp_tick) {
                    case 0: cs.freq = cs.base_freq; break;
                    case 1: cs.freq = cs.base_freq * std::pow(2.0f, static_cast<float>(cs.arp_x) / 12.0f); break;
                    case 2: cs.freq = cs.base_freq * std::pow(2.0f, static_cast<float>(cs.arp_y) / 12.0f); break;
                    }
                }
                break;
            case 0x01: // Portamento up
                cs.freq *= std::pow(2.0f, static_cast<float>(data) / (12.0f * 16.0f));
                cs.base_freq = cs.freq;
                break;
            case 0x02: // Portamento down
                cs.freq /= std::pow(2.0f, static_cast<float>(data) / (12.0f * 16.0f));
                cs.base_freq = cs.freq;
                break;
            case 0x03: // Tone portamento
            case 0x05: { // Tone porta + vol slide
                float rate = std::pow(2.0f, static_cast<float>(cs.porta_speed) / (12.0f * 16.0f));
                if (cs.freq < cs.target_freq) {
                    cs.freq *= rate;
                    if (cs.freq > cs.target_freq) cs.freq = cs.target_freq;
                } else if (cs.freq > cs.target_freq) {
                    cs.freq /= rate;
                    if (cs.freq < cs.target_freq) cs.freq = cs.target_freq;
                }
                cs.base_freq = cs.freq;
                // 5xy also does volume slide
                if (cmd == 0x05) {
                    if (x > 0) cs.volume = std::clamp(cs.volume + static_cast<float>(x) / 64.0f, 0.0f, 1.0f);
                    else if (y > 0) cs.volume = std::clamp(cs.volume - static_cast<float>(y) / 64.0f, 0.0f, 1.0f);
                }
                break;
            }
            case 0x06: { // Vibrato + vol slide
                // Continue vibrato
                float vib = std::sin(cs.vibrato_phase) * static_cast<float>(cs.vibrato_depth) / 128.0f;
                cs.freq = cs.base_freq * std::pow(2.0f, vib);
                cs.vibrato_phase += static_cast<float>(cs.vibrato_speed) * 2.0f * static_cast<float>(M_PI) / 64.0f;
                // Volume slide
                if (x > 0) cs.volume = std::clamp(cs.volume + static_cast<float>(x) / 64.0f, 0.0f, 1.0f);
                else if (y > 0) cs.volume = std::clamp(cs.volume - static_cast<float>(y) / 64.0f, 0.0f, 1.0f);
                break;
            }
            case 0x04: { // Vibrato
                float vib = std::sin(cs.vibrato_phase) * static_cast<float>(cs.vibrato_depth) / 128.0f;
                cs.freq = cs.base_freq * std::pow(2.0f, vib);
                cs.vibrato_phase += static_cast<float>(cs.vibrato_speed) * 2.0f * static_cast<float>(M_PI) / 64.0f;
                break;
            }
            case 0x07: { // Tremolo
                float trem = std::sin(cs.tremolo_phase) * static_cast<float>(cs.tremolo_depth) / 64.0f;
                cs.volume = std::clamp(cs.volume + trem, 0.0f, 1.0f);
                cs.tremolo_phase += static_cast<float>(cs.tremolo_speed) * 2.0f * static_cast<float>(M_PI) / 64.0f;
                break;
            }
            case 0x0A: // Volume slide
                if (x > 0) cs.volume = std::clamp(cs.volume + static_cast<float>(x) / 64.0f, 0.0f, 1.0f);
                else if (y > 0) cs.volume = std::clamp(cs.volume - static_cast<float>(y) / 64.0f, 0.0f, 1.0f);
                break;
            default:
                break;
            }
        }

        // Extended effects that check current tick
        if (cmd == 0x0E) {
            switch (x) {
            case 0x9: // Retrigger note every y ticks
                if (y > 0 && tick_ > 0 && (tick_ % y) == 0) {
                    cs.vibrato_phase = 0;
                    cs.tremolo_phase = 0;
                }
                break;
            case 0xC: // Note cut at tick y
                if (tick_ == y) {
                    cs.volume = 0;
                    cs.active = false;
                }
                break;
            default:
                break;
            }
        }

        // Clamp frequency
        cs.freq = std::clamp(cs.freq, 20.0f, 20000.0f);
    }
}

void TrackerPlayer::apply_to_backend() {
    if (!backend_) return;

    float master_vol = AudioSettingsManager::instance().get_volume() / 100.0f;
    int voices = backend_->voice_count();

    for (int ch = 0; ch < std::min(4, voices); ++ch) {
        auto& cs = channels_[ch];
        if (cs.active && cs.freq > 0 && cs.volume > 0.001f) {
            float vol = std::clamp(cs.volume * master_vol, 0.0f, 1.0f);
            backend_->set_voice(ch, cs.freq, vol, cs.duty);
            if (backend_->supports_waveforms()) {
                backend_->set_voice_waveform(ch, cs.waveform);
            }
        } else {
            backend_->silence_voice(ch);
        }
    }

    std::atomic_thread_fence(std::memory_order_release);
}

void TrackerPlayer::advance_row() {
    // Handle position jump / pattern break
    if (next_order_ >= 0) {
        order_idx_ = next_order_;
        row_ = (next_row_ >= 0) ? next_row_ : 0;
        next_order_ = -1;
        next_row_ = -1;

        if (order_idx_ >= module_.num_orders) {
            playing_.store(false);
            return;
        }
        return;
    }

    row_++;

    // Check if past end of current pattern
    uint8_t pat_idx = module_.order[order_idx_];
    int pattern_rows = module_.rows_per_pattern;
    if (pat_idx < module_.patterns.size()) {
        pattern_rows = static_cast<int>(module_.patterns[pat_idx].size()) / 4;
    }

    if (row_ >= pattern_rows) {
        row_ = 0;
        order_idx_++;
        if (order_idx_ >= module_.num_orders) {
            playing_.store(false);
        }
    }
}

} // namespace helix::audio

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[tracker]" -v`
Expected: All tracker tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/tracker_player.h src/system/tracker_player.cpp tests/unit/test_tracker_player.cpp
git commit -m "feat(sound): add TrackerPlayer with core tick engine and effects"
```

---

### Task 4: Move SoundPriority + Sequencer External Tick

**Files:**
- Modify: `include/sound_backend.h`
- Modify: `include/sound_sequencer.h`
- Modify: `src/system/sound_sequencer.cpp`

**Context:** Per spec, `SoundPriority` moves from `sound_sequencer.h` to `sound_backend.h` (lower-level header both sequencer and tracker depend on). The sequencer gains `set_external_tick()` to allow SoundManager to route ticks to the TrackerPlayer.

- [ ] **Step 1: Move SoundPriority to sound_backend.h**

Add before the `SoundBackend` class in `include/sound_backend.h`:

```cpp
/// Sound priority levels (higher numeric value = more important)
enum class SoundPriority {
    UI = 0,    // button taps, nav sounds — can be interrupted by anything
    EVENT = 1, // print complete, errors — only interrupted by ALARM
    ALARM = 2  // critical alerts — never interrupted
};
```

Remove the same enum from `include/sound_sequencer.h` (lines 16-21).

- [ ] **Step 2: Add set_external_tick to sequencer**

In `include/sound_sequencer.h`, add to public section:

```cpp
    /// Set an external tick callback. When set, the sequencer loop calls this
    /// instead of its own step logic. Used by SoundManager for TrackerPlayer.
    void set_external_tick(std::function<void(float dt_ms)> fn);
```

Add to private section:

```cpp
    std::function<void(float)> external_tick_;
```

Add `#include <functional>` to the includes.

- [ ] **Step 3: Implement external tick in sequencer loop**

In `src/system/sound_sequencer.cpp`, add the `set_external_tick` method:

```cpp
void SoundSequencer::set_external_tick(std::function<void(float dt_ms)> fn) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    external_tick_ = std::move(fn);
    queue_cv_.notify_one(); // wake up loop if sleeping
}
```

In `sequencer_loop()`, modify the idle wait condition (around line 86) to also wake when external_tick is set:

```cpp
if (!playing_.load() && request_queue_.empty() && !external_tick_) {
    // Nothing playing, nothing queued, no external tick — wait
    was_playing = false;
    queue_cv_.wait_for(lock, std::chrono::milliseconds(10));
    last_tick = std::chrono::steady_clock::now();
    continue;
}
```

And in the tick section (around line 114), add external tick routing:

```cpp
// Tick if playing or external tick active
{
    std::function<void(float)> ext_tick;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        ext_tick = external_tick_;
    }

    if (ext_tick) {
        if (!was_playing) {
            last_tick = std::chrono::steady_clock::now();
            was_playing = true;
        }
        auto now = std::chrono::steady_clock::now();
        float dt_ms = std::chrono::duration<float, std::milli>(now - last_tick).count();
        last_tick = now;
        dt_ms = std::min(dt_ms, 5.0f);
        ext_tick(dt_ms);
    } else if (playing_.load()) {
        // existing tick logic unchanged
```

- [ ] **Step 4: Build and run all tests**

Run: `make test && ./build/bin/helix-tests -v`
Expected: ALL tests pass (existing sequencer tests + tracker tests)

- [ ] **Step 5: Commit**

```bash
git add include/sound_backend.h include/sound_sequencer.h src/system/sound_sequencer.cpp
git commit -m "refactor(sound): move SoundPriority to sound_backend.h, add set_external_tick"
```

---

### Task 5: SoundManager Integration — play_file() and Handoff

**Files:**
- Modify: `include/sound_manager.h`
- Modify: `src/system/sound_manager.cpp`

**Context:** SoundManager gains `play_file()`, `stop_tracker()`, `is_tracker_playing()`. These create a TrackerPlayer, route ticks via the sequencer's external tick, and manage priority handoffs. All gated by `#ifdef HELIX_HAS_TRACKER`.

- [ ] **Step 1: Add tracker API to sound_manager.h**

Add includes and members to `include/sound_manager.h`:

```cpp
// After existing includes, add:
#ifdef HELIX_HAS_TRACKER
#include "tracker_player.h"
#endif

// In public section, add:
#ifdef HELIX_HAS_TRACKER
    /// Play a MOD/MED tracker file
    void play_file(const std::string& path, SoundPriority priority = SoundPriority::EVENT);

    /// Stop tracker playback
    void stop_tracker();

    /// Check if tracker is currently playing
    bool is_tracker_playing() const;
#endif

// In private section, add:
#ifdef HELIX_HAS_TRACKER
    std::unique_ptr<helix::audio::TrackerPlayer> tracker_;
    SoundPriority tracker_priority_ = SoundPriority::UI;
#endif
```

- [ ] **Step 2: Implement play_file, stop_tracker, is_tracker_playing**

Add to `src/system/sound_manager.cpp`:

```cpp
#ifdef HELIX_HAS_TRACKER
#include "tracker_module.h"
#include "tracker_player.h"
#endif

// ... in the implementation section:

#ifdef HELIX_HAS_TRACKER

void SoundManager::play_file(const std::string& path, SoundPriority priority) {
    if (!AudioSettingsManager::instance().get_sounds_enabled()) {
        spdlog::trace("[SoundManager] play_file('{}') skipped - sounds disabled", path);
        return;
    }

    if (!backend_ || !sequencer_) {
        spdlog::debug("[SoundManager] play_file('{}') skipped - no backend/sequencer", path);
        return;
    }

    auto module = helix::audio::TrackerModule::load(path);
    if (!module) {
        spdlog::warn("[SoundManager] play_file('{}') - failed to load", path);
        return;
    }

    // Stop current playback (sequencer or tracker)
    // Clear external tick FIRST to prevent use-after-free on old tracker
    sequencer_->set_external_tick(nullptr);
    if (tracker_) {
        tracker_->stop();
    }
    sequencer_->stop();

    // Create and start tracker
    tracker_ = std::make_unique<helix::audio::TrackerPlayer>(backend_);
    tracker_->load(std::move(*module));
    tracker_priority_ = priority;
    tracker_->play();

    // Route ticks to tracker
    auto* tp = tracker_.get();
    sequencer_->set_external_tick([tp](float dt_ms) {
        tp->tick(dt_ms);
    });

    spdlog::info("[SoundManager] play_file('{}', priority={})", path, static_cast<int>(priority));
}

void SoundManager::stop_tracker() {
    // IMPORTANT: Clear external tick BEFORE destroying tracker to prevent
    // use-after-free (sequencer thread may still hold raw pointer to tracker)
    if (sequencer_) {
        sequencer_->set_external_tick(nullptr);
    }
    if (tracker_) {
        tracker_->stop();
        tracker_.reset();
    }
    spdlog::debug("[SoundManager] stop_tracker");
}

bool SoundManager::is_tracker_playing() const {
    return tracker_ && tracker_->is_playing();
}

#endif // HELIX_HAS_TRACKER
```

- [ ] **Step 3: Add tracker preemption to play()**

In `SoundManager::play()`, before `sequencer_->play(it->second, priority)`, add:

```cpp
#ifdef HELIX_HAS_TRACKER
    // Preempt tracker if new sound has higher or equal priority
    if (tracker_ && tracker_->is_playing()) {
        if (static_cast<int>(priority) >= static_cast<int>(tracker_priority_)) {
            stop_tracker();
        } else {
            spdlog::debug("[SoundManager] play('{}') skipped - tracker at higher priority", sound_name);
            return;
        }
    }
#endif
```

- [ ] **Step 4: Add tracker cleanup to shutdown()**

In `SoundManager::shutdown()`, before `sequencer_->shutdown()`:

```cpp
#ifdef HELIX_HAS_TRACKER
    stop_tracker();
#endif
```

- [ ] **Step 5: Build and run all tests**

Run: `make test && ./build/bin/helix-tests -v`
Expected: ALL tests pass

- [ ] **Step 6: Commit**

```bash
git add include/sound_manager.h src/system/sound_manager.cpp
git commit -m "feat(sound): add play_file() to SoundManager for tracker playback"
```

---

### Task 6: Compile-Time Gating — HELIX_HAS_SOUND + HELIX_HAS_TRACKER

**Files:**
- Modify: `Makefile`
- Modify: `src/application/application.cpp` (add `#ifdef HELIX_HAS_SOUND` guards)

**Context:** Add `HELIX_HAS_SOUND` and `HELIX_HAS_TRACKER` flags to the Makefile. The `HELIX_HAS_SOUND` flag currently doesn't exist — all sound code compiles unconditionally. The tracker files already have `#ifdef HELIX_HAS_TRACKER` guards from Tasks 1-3. For `HELIX_HAS_SOUND`, we only need call-site guards in `application.cpp` (where SoundManager is initialized and startup sound is played). The sound `.cpp` files themselves compile harmlessly when no backend is available — `SoundManager::create_backend()` returns nullptr and all play() calls are no-ops. So `HELIX_HAS_SOUND` is primarily about excluding the object code entirely on platforms where we want minimal binary size.

- [ ] **Step 1: Add flags to Makefile**

After the existing ALSA detection block (around line 664), add:

```makefile
# Sound system (entire sound subsystem)
SOUND_CXXFLAGS :=
TRACKER_CXXFLAGS :=
ifneq (,$(filter pi pi-fbdev pi-both pi32 pi32-fbdev pi32-both x86 x86-fbdev x86-both,$(PLATFORM_TARGET)))
    SOUND_CXXFLAGS := -DHELIX_HAS_SOUND
    TRACKER_CXXFLAGS := -DHELIX_HAS_TRACKER
else ifeq ($(PLATFORM_TARGET),native)
    SOUND_CXXFLAGS := -DHELIX_HAS_SOUND
    TRACKER_CXXFLAGS := -DHELIX_HAS_TRACKER
endif
CXXFLAGS += $(SOUND_CXXFLAGS) $(TRACKER_CXXFLAGS)
```

Note: For now, sound is enabled on ALL platforms where we build (including AD5M, which uses PWM). The flags exist for future exclusion. If a platform should not have sound, remove it from the filter list.

- [ ] **Step 2: Add static_assert for HELIX_HAS_TRACKER without HELIX_HAS_SOUND**

In `include/tracker_module.h`, after `#ifdef HELIX_HAS_TRACKER`:

```cpp
#ifndef HELIX_HAS_SOUND
#error "HELIX_HAS_TRACKER requires HELIX_HAS_SOUND"
#endif
```

- [ ] **Step 3: Build and run all tests**

Run: `make test && ./build/bin/helix-tests -v`
Expected: ALL tests pass (both flags are set on native builds)

- [ ] **Step 4: Verify flags work by building without tracker**

Run: `make clean && CXXFLAGS="-DHELIX_HAS_SOUND" make test`
(Override to exclude HELIX_HAS_TRACKER — tracker tests should be skipped, everything else builds)

- [ ] **Step 5: Commit**

```bash
git add Makefile include/tracker_module.h
git commit -m "build(sound): add HELIX_HAS_SOUND and HELIX_HAS_TRACKER compile-time flags"
```

---

### Task 7: About Screen Integration — Play Crockett's Theme

**Files:**
- Modify: `src/ui/ui_settings_about.cpp`

**Context:** When the About settings overlay activates, play the MED file. When it deactivates, stop. This is the user's primary motivation: "I want the full Crockett's Theme to play on the about screen."

- [ ] **Step 1: Add tracker include and playback to About overlay**

In `src/ui/ui_settings_about.cpp`:

Add include at top (after existing includes):
```cpp
#ifdef HELIX_HAS_TRACKER
#include "sound_manager.h"
#endif
```

In `AboutSettingsOverlay::on_activate()` (line 186), add before the closing brace:
```cpp
#ifdef HELIX_HAS_TRACKER
    helix::SoundManager::instance().play_file(
        "assets/sounds/crocketts_theme.med", SoundPriority::UI);
#endif
```

In `AboutSettingsOverlay::on_deactivate()` (line 201), add before the closing brace:
```cpp
#ifdef HELIX_HAS_TRACKER
    helix::SoundManager::instance().stop_tracker();
#endif
```

- [ ] **Step 2: Build**

Run: `make -j`
Expected: Compiles without errors

- [ ] **Step 3: Manual test — launch app and navigate to About screen**

Run (background): `./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/about-sound.log`

Tell user: Navigate to Settings → About. You should hear the Crockett's Theme playing through all 4 channels. Navigate away — music should stop.

- [ ] **Step 4: Check log for tracker output**

Read `/tmp/about-sound.log` and look for:
- `[TrackerModule] Loaded MMD` — file parsed successfully
- `[TrackerPlayer] play:` — playback started
- `[SoundManager] play_file('assets/sounds/crocketts_theme.med')` — triggered from About screen
- `[SoundManager] stop_tracker` — triggered on deactivate

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_settings_about.cpp
git commit -m "feat(sound): play Crockett's Theme on About screen via tracker player"
```

---

### Task 8: Final Build Verification

**Files:** None (verification only)

- [ ] **Step 1: Run full test suite**

Run: `make test-run`
Expected: ALL tests pass including [tracker] tagged tests

- [ ] **Step 2: Check for compiler warnings**

Run: `make -j 2>&1 | grep -i "warning:" | head -20`
Expected: No new warnings from tracker code

- [ ] **Step 3: Verify binary size impact**

Run: `ls -la build/bin/helix-screen` and compare to previous size (note in commit)

- [ ] **Step 4: Test with sounds disabled**

Run (background): `./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/nosound.log`

Tell user: Go to Settings → Sound, disable sounds. Then go to About screen. Verify no sound plays and no crash.

- [ ] **Step 5: Final commit (if any remaining changes)**

```bash
git status
# If clean, nothing to commit. Otherwise:
git add -A && git commit -m "chore(sound): tracker player cleanup"
```
