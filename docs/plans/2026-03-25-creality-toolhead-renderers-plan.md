# Creality K1 & K2 Toolhead Renderers — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two procedural toolhead renderers for Creality K1 and K2 printers so they display their distinctive toolhead shapes instead of the generic default.

**Architecture:** Procedural primitive rendering (like `nozzle_renderer_bambu.cpp`) using gradient rects, arcs, and isometric helpers from `nozzle_renderer_common.h`. Integration via `ToolheadStyle` enum, dispatch switch, and auto-detection from printer type config.

**Tech Stack:** C++ / LVGL 9.5 drawing primitives, Catch2 for tests.

**Spec:** `docs/plans/2026-03-24-creality-toolhead-renderers-design.md`

---

### Task 1: Enum, Clamps, and Settings Dropdown

**Files:**
- Modify: `include/settings_manager.h:20`
- Modify: `src/system/settings_manager.cpp:29,81,182,201`

- [ ] **Step 1: Add enum values**

In `include/settings_manager.h:20`, change:
```cpp
enum class ToolheadStyle { AUTO = 0, DEFAULT = 1, A4T = 2, ANTHEAD = 3, JABBERWOCKY = 4, STEALTHBURNER = 5 };
```
to:
```cpp
enum class ToolheadStyle { AUTO = 0, DEFAULT = 1, A4T = 2, ANTHEAD = 3, JABBERWOCKY = 4, STEALTHBURNER = 5, CREALITY_K1 = 6, CREALITY_K2 = 7 };
```

- [ ] **Step 2: Update dropdown options string**

In `src/system/settings_manager.cpp:29`, change:
```cpp
static const char* TOOLHEAD_STYLE_OPTIONS_TEXT = "Auto\nDefault\nA4T\nAntHead\nJabberWocky\nStealthburner";
```
to:
```cpp
static const char* TOOLHEAD_STYLE_OPTIONS_TEXT = "Auto\nDefault\nA4T\nAntHead\nJabberWocky\nStealthburner\nCreality K1\nCreality K2";
```

- [ ] **Step 3: Update all three clamp sites from 5 to 7**

In `src/system/settings_manager.cpp`, update these three lines:
- Line 81: `std::clamp(toolhead_style, 0, 5)` → `std::clamp(toolhead_style, 0, 7)`
- Line 182: `std::clamp(val, 0, 5)` → `std::clamp(val, 0, 7)`
- Line 201: `std::clamp(val, 0, 5)` → `std::clamp(val, 0, 7)`

- [ ] **Step 4: Build to verify no compilation errors**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add include/settings_manager.h src/system/settings_manager.cpp
git commit -m "feat(settings): add Creality K1/K2 toolhead style enum values"
```

---

### Task 2: Auto-Detection in PrinterDetector

**Files:**
- Modify: `include/printer_detector.h:385` (after `is_pfa_printer`)
- Modify: `src/printer/printer_detector.cpp:1468` (after `is_pfa_printer` implementation)
- Modify: `src/system/settings_manager.cpp:190-196` (in `get_effective_toolhead_style`)
- Test: `tests/unit/test_printer_detector.cpp` (existing file, append)

- [ ] **Step 1: Write failing tests**

Append to `tests/unit/test_printer_detector.cpp` (already includes `printer_detector.h` and `catch_amalgamated.hpp`):
```cpp
TEST_CASE("Creality K1 printer detection", "[printer_detector]") {
    // These tests verify the static detection helpers.
    // They rely on printer_type_contains() reading from Config,
    // so they need a Config instance with the printer type set.

    auto* config = Config::get_instance();
    REQUIRE(config != nullptr);
    std::string df = config->df();

    SECTION("K1 detected from Creality K1 type") {
        config->set<std::string>(df + "/printer/type", "Creality K1");
        REQUIRE(PrinterDetector::is_creality_k1() == true);
        REQUIRE(PrinterDetector::is_creality_k2() == false);
    }

    SECTION("K1C detected as K1 variant") {
        config->set<std::string>(df + "/printer/type", "Creality K1C");
        REQUIRE(PrinterDetector::is_creality_k1() == true);
    }

    SECTION("K2 Plus detected as K2") {
        config->set<std::string>(df + "/printer/type", "Creality K2 Plus");
        REQUIRE(PrinterDetector::is_creality_k2() == true);
        REQUIRE(PrinterDetector::is_creality_k1() == false);
    }

    SECTION("Non-Creality K1 not detected") {
        config->set<std::string>(df + "/printer/type", "SomeOther K1 Printer");
        REQUIRE(PrinterDetector::is_creality_k1() == false);
    }

    SECTION("Voron not detected as Creality") {
        config->set<std::string>(df + "/printer/type", "Voron 2.4");
        REQUIRE(PrinterDetector::is_creality_k1() == false);
        REQUIRE(PrinterDetector::is_creality_k2() == false);
    }

    // Clean up
    config->set<std::string>(df + "/printer/type", "");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[printer_detector]" -v 2>&1 | tail -10`
Expected: FAIL — `is_creality_k1` and `is_creality_k2` are not declared.

- [ ] **Step 3: Add declarations to printer_detector.h**

In `include/printer_detector.h`, after `is_pfa_printer()` (line 385), add:
```cpp

    /**
     * @brief Check if connected printer is a Creality K1 series
     * @return true if printer type contains both "creality" and "k1"
     */
    static bool is_creality_k1();

    /**
     * @brief Check if connected printer is a Creality K2 series
     * @return true if printer type contains both "creality" and "k2"
     */
    static bool is_creality_k2();
```

- [ ] **Step 4: Add implementations to printer_detector.cpp**

In `src/printer/printer_detector.cpp`, after the `is_pfa_printer()` implementation (line 1470), add:
```cpp

bool PrinterDetector::is_creality_k1() {
    return printer_type_contains("creality") && printer_type_contains("k1");
}

bool PrinterDetector::is_creality_k2() {
    return printer_type_contains("creality") && printer_type_contains("k2");
}
```

- [ ] **Step 5: Wire into auto-detection**

In `src/system/settings_manager.cpp`, in `get_effective_toolhead_style()`, add after the `is_voron_printer()` check (around line 194) and before `return ToolheadStyle::DEFAULT`:
```cpp
    if (PrinterDetector::is_creality_k1()) {
        return ToolheadStyle::CREALITY_K1;
    }
    if (PrinterDetector::is_creality_k2()) {
        return ToolheadStyle::CREALITY_K2;
    }
```

- [ ] **Step 6: Run tests**

Run: `make test && ./build/bin/helix-tests "[printer_detector]" -v 2>&1 | tail -20`
Expected: All Creality detection tests PASS.

- [ ] **Step 7: Commit**

```bash
git add include/printer_detector.h src/printer/printer_detector.cpp src/system/settings_manager.cpp tests/unit/test_printer_detector.cpp
git commit -m "feat(detection): auto-detect Creality K1/K2 for toolhead style"
```

---

### Task 3: K1 Renderer — Header and Stub

**Files:**
- Create: `include/nozzle_renderer_creality_k1.h`
- Create: `src/rendering/nozzle_renderer_creality_k1.cpp`
- Modify: `include/nozzle_renderer_dispatch.h:9-10,30-31`

- [ ] **Step 1: Create header**

Create `include/nozzle_renderer_creality_k1.h`:
```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_creality_k1.h
/// @brief Creality K1 series toolhead renderer

#pragma once

#include "lvgl/lvgl.h"

/// @brief Draw Creality K1 toolhead
///
/// Metallic silver-gray body with large circular fan, sloped upper section,
/// and chamfered V-cut bottom. Proportions ~1:1.2 W:H.
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament_color Color of loaded filament (tints nozzle tip)
/// @param scale_unit Base scaling unit
/// @param opa Opacity (default LV_OPA_COVER)
void draw_nozzle_creality_k1(lv_layer_t* layer, int32_t cx, int32_t cy,
                              lv_color_t filament_color, int32_t scale_unit,
                              lv_opa_t opa = LV_OPA_COVER);
```

- [ ] **Step 2: Create stub implementation**

Create `src/rendering/nozzle_renderer_creality_k1.cpp`:
```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_creality_k1.cpp
/// @brief Creality K1 toolhead renderer — metallic body with large fan

#include "nozzle_renderer_creality_k1.h"

#include "nozzle_renderer_common.h"
#include "theme_manager.h"

void draw_nozzle_creality_k1(lv_layer_t* layer, int32_t cx, int32_t cy,
                              lv_color_t filament_color, int32_t scale_unit,
                              lv_opa_t opa) {
    // Stub — delegates to Bambu for now, replaced in Task 5
    // TODO: implement K1-specific rendering
    (void)layer;
    (void)cx;
    (void)cy;
    (void)filament_color;
    (void)scale_unit;
    (void)opa;
}
```

- [ ] **Step 3: Add to dispatch**

In `include/nozzle_renderer_dispatch.h`, add includes after line 12 (`#include "nozzle_renderer_jabberwocky.h"`):
```cpp
#include "nozzle_renderer_creality_k1.h"
```

Add case before `default:` in the switch:
```cpp
        case helix::ToolheadStyle::CREALITY_K1:
            draw_nozzle_creality_k1(layer, cx, cy, filament_color, scale_unit, opa);
            break;
```

- [ ] **Step 4: Build**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add include/nozzle_renderer_creality_k1.h src/rendering/nozzle_renderer_creality_k1.cpp include/nozzle_renderer_dispatch.h
git commit -m "feat(renderer): add Creality K1 toolhead stub + dispatch wiring"
```

---

### Task 4: K2 Renderer — Header and Stub

**Files:**
- Create: `include/nozzle_renderer_creality_k2.h`
- Create: `src/rendering/nozzle_renderer_creality_k2.cpp`
- Modify: `include/nozzle_renderer_dispatch.h`

- [ ] **Step 1: Create header**

Create `include/nozzle_renderer_creality_k2.h`:
```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_creality_k2.h
/// @brief Creality K2 series toolhead renderer

#pragma once

#include "lvgl/lvgl.h"

/// @brief Draw Creality K2 toolhead
///
/// Dark charcoal tower body with 13 vertical vent slits, sloped upper
/// section, and U-shaped nozzle cutout. Proportions ~1:2 W:H.
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament_color Color of loaded filament (tints nozzle tip)
/// @param scale_unit Base scaling unit
/// @param opa Opacity (default LV_OPA_COVER)
void draw_nozzle_creality_k2(lv_layer_t* layer, int32_t cx, int32_t cy,
                              lv_color_t filament_color, int32_t scale_unit,
                              lv_opa_t opa = LV_OPA_COVER);
```

- [ ] **Step 2: Create stub implementation**

Create `src/rendering/nozzle_renderer_creality_k2.cpp`:
```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_creality_k2.cpp
/// @brief Creality K2 toolhead renderer — dark tower with vent slits

#include "nozzle_renderer_creality_k2.h"

#include "nozzle_renderer_common.h"

void draw_nozzle_creality_k2(lv_layer_t* layer, int32_t cx, int32_t cy,
                              lv_color_t filament_color, int32_t scale_unit,
                              lv_opa_t opa) {
    // Stub — replaced in Task 6
    (void)layer;
    (void)cx;
    (void)cy;
    (void)filament_color;
    (void)scale_unit;
    (void)opa;
}
```

- [ ] **Step 3: Add to dispatch**

In `include/nozzle_renderer_dispatch.h`, add include:
```cpp
#include "nozzle_renderer_creality_k2.h"
```

Add case before `default:` in the switch:
```cpp
        case helix::ToolheadStyle::CREALITY_K2:
            draw_nozzle_creality_k2(layer, cx, cy, filament_color, scale_unit, opa);
            break;
```

- [ ] **Step 4: Build and run full test suite**

Run: `make -j && make test-run 2>&1 | tail -10`
Expected: Build succeeds, all tests pass. Integration is complete — stubs compile and dispatch works.

- [ ] **Step 5: Commit**

```bash
git add include/nozzle_renderer_creality_k2.h src/rendering/nozzle_renderer_creality_k2.cpp include/nozzle_renderer_dispatch.h
git commit -m "feat(renderer): add Creality K2 toolhead stub + dispatch wiring"
```

---

### Task 5: K1 Renderer — Full Implementation

**Files:**
- Modify: `src/rendering/nozzle_renderer_creality_k1.cpp` (replace stub)

**Reference:** `src/rendering/nozzle_renderer_bambu.cpp` — follow the same structure (dim helper, color setup, dimensions, draw steps top-to-bottom).

**Reference images:** See `docs/plans/2026-03-24-creality-toolhead-renderers-design.md` § K1 Renderer.

- [ ] **Step 1: Implement the renderer**

Replace the stub in `src/rendering/nozzle_renderer_creality_k1.cpp` with the full implementation. Follow the Bambu renderer structure exactly:

1. **Dim helper lambda** — same alpha-to-premultiplied-black pattern as Bambu
2. **Base colors** — `theme_manager_get_color("filament_metal")` for metallic silver-gray body. Derive `front_light`, `front_dark`, `side_color`, `top_color`, `outline_color` using `nr_lighten`/`nr_darken`.
3. **Dimensions** — all proportional to `scale_unit`:
   - Body proportions: ~1:1.2 W:H (slightly taller than wide)
   - `body_half_width`: ~`(scale_unit * 18) / 10`
   - `body_height`: ~`(scale_unit * 30) / 10` (shorter than Bambu)
   - `body_depth`: ~`(scale_unit * 6) / 10`
   - Fan radius: ~`(scale_unit * 12) / 10` (large, dominant)
   - Upper section height: ~35% of body (slopes backward)
   - Bottom chamfer: ~15% of body height
4. **Draw steps** (top to bottom, back to front):
   - **Step 0: Sloped upper section** — per-scanline trapezoid (wider at bottom, narrower at top where it recedes). Use the same tapered technique as Bambu's cap section. Includes iso top and right side. Smooth gradient transitions along edges for rounded bevel effect.
   - **Step 1: Lower body** — `nr_draw_gradient_rect` for front face, `nr_draw_iso_side` for right face. Left edge highlight line.
   - **Step 2: Fan** — `lv_draw_arc` for bezel ring + highlight arc. `lv_draw_fill` with `radius` for dark fan opening and hub circle. Subtle cross lines for blade hints.
   - **Step 3: Bottom V-cut** — Two triangular fills for chamfered corners. Flat bottom rect between them.
   - **Step 4: Nozzle** — `nr_draw_nozzle_tip()` + heat block rect + filament color tinting + glint.

- [ ] **Step 2: Build**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Visual test**

Run: `HELIX_HOT_RELOAD=1 ./build/bin/helix-screen --test -vv`

In the UI, go to Settings → Appearance → Toolhead Style → select "Creality K1". Navigate to the AMS panel or Z-offset to see the rendered toolhead. Verify:
- Metallic silver-gray body
- Large fan circle visible and centered in lower section
- Sloped upper section with smooth edges
- V-cut bottom with nozzle tip
- Proper isometric 3D appearance (top face, right side)

- [ ] **Step 4: Take screenshot for reference**

Run: `./scripts/screenshot.sh helix-screen creality-k1-toolhead`

- [ ] **Step 5: Commit**

```bash
git add src/rendering/nozzle_renderer_creality_k1.cpp
git commit -m "feat(renderer): implement Creality K1 toolhead rendering"
```

---

### Task 6: K2 Renderer — Full Implementation

**Files:**
- Modify: `src/rendering/nozzle_renderer_creality_k2.cpp` (replace stub)

**Reference:** `src/rendering/nozzle_renderer_bambu.cpp` for structure, but K2 uses dark charcoal colors and has no fan.

**Reference images:** See `docs/plans/2026-03-24-creality-toolhead-renderers-design.md` § K2 Renderer.

- [ ] **Step 1: Implement the renderer**

Replace the stub in `src/rendering/nozzle_renderer_creality_k2.cpp` with the full implementation:

1. **Dim helper lambda** — same pattern
2. **Base colors** — hardcoded dark charcoal `lv_color_hex(0x2A2A2A)`. Derive lighting variants.
3. **Dimensions** — all proportional to `scale_unit`:
   - Body proportions: ~1:2 W:H (tall tower)
   - `body_half_width`: ~`(scale_unit * 12) / 10` (narrower than K1)
   - `body_height`: ~`(scale_unit * 50) / 10` (tall)
   - `body_depth`: ~`(scale_unit * 5) / 10`
   - Upper section: ~45% of body (slopes backward)
   - Vent section: ~30% of body
   - Bottom + U-cutout: ~25% of body
4. **Draw steps** (top to bottom, back to front):
   - **Step 0: Sloped upper section** — per-scanline trapezoid like K1. Smooth rounded bevel transitions at edges. Iso top and right side.
   - **Step 1: Vent section** — `nr_draw_gradient_rect` for front face. 13 evenly-spaced vertical dark lines drawn with `lv_draw_fill` (1-2px wide each).
   - **Step 2: Bottom pillars** — Two narrow rects flanking the U-cutout.
   - **Step 3: U-cutout** — Dark filled area. Draw as a dark rect with the top portion having rounded corners (use small arc segments or just a dark rect + slightly lighter border).
   - **Step 4: Nozzle** — Inside the U-cutout. Small heat block + nozzle tip. Barely protrudes.

- [ ] **Step 2: Build**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Visual test**

Run: `HELIX_HOT_RELOAD=1 ./build/bin/helix-screen --test -vv`

Select "Creality K2" in toolhead style settings. Verify:
- Dark charcoal tower body
- 13 vertical vent slits visible in lower section
- Sloped upper section with smooth edges
- U-shaped cutout at bottom with nozzle inside
- Proper isometric appearance

- [ ] **Step 4: Take screenshot**

Run: `./scripts/screenshot.sh helix-screen creality-k2-toolhead`

- [ ] **Step 5: Commit**

```bash
git add src/rendering/nozzle_renderer_creality_k2.cpp
git commit -m "feat(renderer): implement Creality K2 toolhead rendering"
```

---

### Task 7: Final Integration Test and Cleanup

**Files:**
- Review all modified files

- [ ] **Step 1: Run full test suite**

Run: `make test-run 2>&1 | tail -20`
Expected: All tests pass.

- [ ] **Step 2: Test auto-detection path**

Run: `./build/bin/helix-screen --test -vv` and check the log output for the effective toolhead style when using AUTO mode. Verify the mock printer type doesn't accidentally trigger Creality detection.

- [ ] **Step 3: Test all toolhead styles cycle**

In the running UI, cycle through all toolhead style options in Settings → Appearance to verify none are broken:
- Auto, Default, A4T, AntHead, JabberWocky, Stealthburner, Creality K1, Creality K2

- [ ] **Step 4: Verify filament color tinting**

On both K1 and K2, check that the nozzle tip tints with the filament color when filament is loaded (visible in AMS detail view).

- [ ] **Step 5: Commit any final fixes**

If fixes were needed, add specific changed files:
```bash
git add src/rendering/nozzle_renderer_creality_k1.cpp src/rendering/nozzle_renderer_creality_k2.cpp
git commit -m "fix(renderer): polish Creality toolhead renderers"
```
