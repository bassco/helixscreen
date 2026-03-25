# Creality K1 & K2 Toolhead Renderers — Design Spec

**Date:** 2026-03-24
**Status:** Draft

## Overview

Add two new procedural toolhead renderers for Creality K1 and K2 series printers. These are visually distinct from each other and from existing renderers (Bambu, Stealthburner, A4T, AntHead, JabberWocky).

## Reference Images

Based on analysis of:
- K1: 3/4 CAD render (purple), front-on photo (metallic silver-gray)
- K2: CAD model (STEP file render), front cover photo (dark charcoal)

## K1 Renderer — `nozzle_renderer_creality_k1`

### Visual Identity
- **Color**: Metallic silver-gray — use `theme_manager_get_color("filament_metal")` like the Bambu renderer
- **Proportions**: Slightly taller than wide (~1:1.2 W:H ratio for the body)
- **Key feature**: Large circular fan dominating the lower ~60% of the front face

### Geometry (front face, top to bottom)
1. **Upper section** — slopes backward gently (~10-12°). In isometric this shows as the left edge being diagonal (top is further back). Contains the area above the fan.
2. **Breakover** — subtle edge where slope meets the vertical fan section
3. **Fan section** — vertical face with large circular fan opening. Fan has thick outer bezel ring, dark opening, center hub, subtle blade cross hints. Bezel highlight arc on top-left.
4. **Bottom taper + V-cut** — body tapers narrower, then chamfered bottom corners create V-shape leading to nozzle

### Chamfers / Edge Treatment
- **Smooth rounded bevels** on all front edges — NOT sharp 45° polygon cuts
- In LVGL: achieve with gradient strips and subtle color transitions along the edges rather than hard geometric chamfer faces
- The body tapers slightly: wider at the fan, narrower toward the top and bottom

### Nozzle
- Small brass/copper nozzle tip. Heat block + tapered nozzle + tip glint.
- Barely visible — just the tip poking below the V-cut

### Isometric Convention
- Front face visible, right side face visible, top face visible
- Light source from top-left (matching all existing renderers)
- Iso depth: proportional to scale_unit, same convention as Bambu renderer
- Front edge of iso top is level; back edge offsets up-right
- Back/right side is straight vertical

## K2 Renderer — `nozzle_renderer_creality_k2`

### Visual Identity
- **Color**: Dark charcoal gray (~#2A2A2A body)
- **Proportions**: Tall tower, roughly 2:1 H:W ratio
- **Key features**: 13 vertical vent slits, U-shaped nozzle cutout, no central fan

### Geometry (front face, top to bottom)
1. **Upper section** — slopes backward gently (~10-12°). Same isometric treatment as K1: left edge diagonal, back is straight vertical.
2. **Breakover** — subtle edge line at transition
3. **Vent section** — vertical front face containing 13 evenly-spaced vertical vent slits running full width. Slits are dark lines cut into the front surface.
4. **Bottom pillars + U-cutout** — body continues as two pillars flanking a U-shaped cutout. The U-cutout is tall with rounded corners at the top of the U.

### Chamfers / Edge Treatment
- **Smooth rounded bevels** on upper section front edges (above the vents)
- Lower vent section is rectangular — no chamfers
- Gradient strips for edge bevels, not sharp polygon cuts
- Upper section tapers: slightly narrower at top than at the breakover

### Nozzle
- Heat block + nozzle visible inside the U-cutout
- Nozzle tip barely protrudes below the body — much less than K1

### Isometric Convention
- Same convention as K1 and all other renderers
- Back/right side straight vertical full height

## Integration

### ToolheadStyle Enum
Add two new values to `ToolheadStyle` in `settings_manager.h`:
```cpp
enum class ToolheadStyle {
    AUTO = 0, DEFAULT = 1, A4T = 2, ANTHEAD = 3,
    JABBERWOCKY = 4, STEALTHBURNER = 5,
    CREALITY_K1 = 6, CREALITY_K2 = 7  // NEW
};
```

### Auto-Detection
In `SettingsManager::get_effective_toolhead_style()`, add detection before the `DEFAULT` fallthrough:
```cpp
if (PrinterDetector::is_creality_k1()) {
    return ToolheadStyle::CREALITY_K1;
}
if (PrinterDetector::is_creality_k2()) {
    return ToolheadStyle::CREALITY_K2;
}
return ToolheadStyle::DEFAULT;
```

Add detection methods to `PrinterDetector`:
- `is_creality_k1()` — matches printer names containing "K1" (K1, K1C, K1 Max, K1 SE)
- `is_creality_k2()` — matches printer names containing "K2" (K2, K2 Plus, K2 Max, K2 Pro)

### Dispatch
Add cases in `nozzle_renderer_dispatch.h`:
```cpp
case helix::ToolheadStyle::CREALITY_K1:
    draw_nozzle_creality_k1(layer, cx, cy, filament_color, scale_unit, opa);
    break;
case helix::ToolheadStyle::CREALITY_K2:
    draw_nozzle_creality_k2(layer, cx, cy, filament_color, scale_unit, opa);
    break;
```

### Settings Dropdown
Update `get_toolhead_style_options()` to include "Creality K1\nCreality K2" options. Update the clamp range from 5 to 7.

### Files
| File | Action |
|------|--------|
| `include/nozzle_renderer_creality_k1.h` | New header |
| `include/nozzle_renderer_creality_k2.h` | New header |
| `src/rendering/nozzle_renderer_creality_k1.cpp` | New implementation |
| `src/rendering/nozzle_renderer_creality_k2.cpp` | New implementation |
| `include/nozzle_renderer_dispatch.h` | Add cases |
| `include/settings_manager.h` | Add enum values |
| `src/system/settings_manager.cpp` | Add auto-detection, update options/clamp |
| `src/printer/printer_detector.cpp` | Add `is_creality_k1()`, `is_creality_k2()` |
| `include/printer_detector.h` | Add method declarations |
| `Makefile` | Add new .cpp files to build |

## Rendering Approach

Both renderers use the **procedural primitive** approach (like the Bambu renderer), NOT polygon tracing (like A4T/Stealthburner). This means:

- `nr_draw_gradient_rect()` for body faces with vertical gradients
- `nr_draw_iso_side()` for right side isometric face
- `nr_draw_iso_top()` for top isometric face
- `lv_draw_arc()` for fan bezel, fan hub (K1)
- `lv_draw_fill()` with `radius` for rounded rects
- `nr_draw_nozzle_tip()` for nozzle assembly
- Gradient strips along edges for smooth bevel effect (NOT polygon chamfer faces)
- Per-scanline fills for the slope section (similar to Bambu's tapered top)

### Signature
Both follow the standard renderer signature:
```cpp
void draw_nozzle_creality_k1(lv_layer_t* layer, int32_t cx, int32_t cy,
                              lv_color_t filament_color, int32_t scale_unit,
                              lv_opa_t opa = LV_OPA_COVER);
```

### Dim Helper
Both use the same alpha-to-premultiplied-black pattern as the Bambu renderer for the `opa` parameter.

## What's NOT Included
- No Creality logo text (too small to read at render size)
- No fan blade detail beyond hint lines (not visible at 40-60px)
- No dot patterns or textures
- No color customization beyond filament color tinting on nozzle tip
