# HiDPI Font Scaling & Per-Platform Font Pruning — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix unreadable text on HiDPI displays by adding an XXLARGE breakpoint tier, scaling XLARGE properly, and pruning unused fonts from constrained platform builds.

**Architecture:** Split the current XLARGE tier (>700px) into XLARGE (701-1000) and XXLARGE (>1000). Generate larger font/icon assets for these tiers. Add a per-platform `FONT_TIERS` mechanism in the build system so each platform only compiles the fonts it needs. The theme registration auto-discovers `_xxlarge` suffixed constants from XML (existing pattern) and silently falls back when fonts are pruned.

**Tech Stack:** LVGL 9.5 XML theming, lv_font_conv (npm), GNU Make, C++

**Spec:** `docs/superpowers/specs/2026-04-13-hidpi-font-scaling-design.md`

---

### Task 1: Add XXLARGE breakpoint to ui_breakpoint.h

**Files:**
- Modify: `include/ui_breakpoint.h:22-38`

- [ ] **Step 1: Add XLARGE_MAX threshold and XXLarge enum value**

In `include/ui_breakpoint.h`, update the enum and add the new threshold:

```cpp
enum class UiBreakpoint : int32_t {
    Micro = 0,  // height ≤ 272  — 480x272
    Tiny = 1,   // height ≤ 390  — 480x320
    Small = 2,  // height ≤ 460  — 480x400, 1920x440
    Medium = 3, // height ≤ 550  — 800x480
    Large = 4,  // height ≤ 700  — 1024x600
    XLarge = 5, // height ≤ 1000 — 1280x720, 1024x768
    XXLarge = 6, // height > 1000 — 1440p, 4K
};
```

Add threshold constant after `UI_BREAKPOINT_LARGE_MAX`:

```cpp
#define UI_BREAKPOINT_XLARGE_MAX 1000  // height 701-1000 → XLARGE
```

Note: `XLarge` comment changes from `height > 700` to `height ≤ 1000`.

- [ ] **Step 2: Update as_breakpoint() clamp range**

The `as_breakpoint()` function clamps to valid range. Update the upper bound from `5` to `6`:

```cpp
inline UiBreakpoint as_breakpoint(int32_t raw)
{
    if (raw < 0) return UiBreakpoint::Micro;
    if (raw > 6) return UiBreakpoint::XXLarge;
    return static_cast<UiBreakpoint>(raw);
}
```

- [ ] **Step 3: Commit**

```bash
git add include/ui_breakpoint.h
git commit -m "feat(theme): add XXLARGE breakpoint tier for HiDPI displays (prestonbrown/helixscreen#773)"
```

---

### Task 2: Update theme_manager breakpoint suffix and label resolution

**Files:**
- Modify: `src/ui/theme_manager.cpp:785-799` (get_breakpoint_suffix)
- Modify: `src/ui/theme_manager.cpp:813-824` (register_responsive_spacing label)
- Modify: `src/ui/theme_manager.cpp:1062-1132` (register_responsive_fonts)

- [ ] **Step 1: Update theme_manager_get_breakpoint_suffix()**

At `src/ui/theme_manager.cpp:785-799`, add the XLARGE_MAX check:

```cpp
const char* theme_manager_get_breakpoint_suffix(int32_t resolution) {
    if (resolution <= UI_BREAKPOINT_MICRO_MAX) {
        return "_micro";
    } else if (resolution <= UI_BREAKPOINT_TINY_MAX) {
        return "_tiny";
    } else if (resolution <= UI_BREAKPOINT_SMALL_MAX) {
        return "_small";
    } else if (resolution <= UI_BREAKPOINT_MEDIUM_MAX) {
        return "_medium";
    } else if (resolution <= UI_BREAKPOINT_LARGE_MAX) {
        return "_large";
    } else if (resolution <= UI_BREAKPOINT_XLARGE_MAX) {
        return "_xlarge";
    } else {
        return "_xxlarge";
    }
}
```

- [ ] **Step 2: Update size_label in register_responsive_spacing()**

At `src/ui/theme_manager.cpp:819-824`, update the label ternary:

```cpp
    const char* size_label = (ver_res <= UI_BREAKPOINT_MICRO_MAX)    ? "MICRO"
                             : (ver_res <= UI_BREAKPOINT_TINY_MAX)   ? "TINY"
                             : (ver_res <= UI_BREAKPOINT_SMALL_MAX)  ? "SMALL"
                             : (ver_res <= UI_BREAKPOINT_MEDIUM_MAX) ? "MEDIUM"
                             : (ver_res <= UI_BREAKPOINT_LARGE_MAX)  ? "LARGE"
                             : (ver_res <= UI_BREAKPOINT_XLARGE_MAX) ? "XLARGE"
                                                                     : "XXLARGE";
```

- [ ] **Step 3: Add _xxlarge token discovery and selection in register_responsive_spacing()**

At `src/ui/theme_manager.cpp:861-867`, add xxlarge discovery:

```cpp
    auto micro_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_micro");
    auto tiny_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_tiny");
    auto small_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_small");
    auto medium_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_medium");
    auto large_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_large");
    auto xlarge_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_xlarge");
    auto xxlarge_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "px", "_xxlarge");
```

Then in the selection logic (around line 898), replace the final `else` block:

```cpp
            } else if (strcmp(size_suffix, "_xlarge") == 0) {
                auto xlarge_it = xlarge_tokens.find(base_name);
                value = (xlarge_it != xlarge_tokens.end()) ? xlarge_it->second.c_str()
                                                           : large_it->second.c_str();
            } else {
                // _xxlarge: use xxlarge if available, fall back to _xlarge, then _large
                auto xxlarge_it = xxlarge_tokens.find(base_name);
                if (xxlarge_it != xxlarge_tokens.end()) {
                    value = xxlarge_it->second.c_str();
                } else {
                    auto xlarge_it = xlarge_tokens.find(base_name);
                    value = (xlarge_it != xlarge_tokens.end()) ? xlarge_it->second.c_str()
                                                               : large_it->second.c_str();
                }
            }
```

- [ ] **Step 4: Update size_label in register_responsive_fonts()**

At `src/ui/theme_manager.cpp:1067-1072`, same label ternary update as step 2.

- [ ] **Step 5: Add _xxlarge token discovery and selection in register_responsive_fonts()**

At `src/ui/theme_manager.cpp:1082-1087`, same pattern as step 3 but for `"string"` type:

```cpp
    auto xxlarge_tokens = theme_manager_parse_all_xml_for_suffix("ui_xml", "string", "_xxlarge");
```

And same selection logic update in the font variant selection (around line 1118).

- [ ] **Step 6: Build to verify compilation**

```bash
make -j
```
Expected: Clean build with no errors.

- [ ] **Step 7: Commit**

```bash
git add src/ui/theme_manager.cpp
git commit -m "feat(theme): support XXLARGE breakpoint in responsive font and spacing registration"
```

---

### Task 3: Generate new text font assets

**Files:**
- Modify: `scripts/regen_text_fonts.sh:162-164` (size arrays)
- Create: `assets/fonts/noto_sans_32.c`, `assets/fonts/noto_sans_40.c`
- Create: `assets/fonts/noto_sans_bold_32.c`, `assets/fonts/noto_sans_bold_40.c`
- Create: `assets/fonts/noto_sans_light_20.c`, `assets/fonts/noto_sans_light_26.c`
- Create: `assets/fonts/source_code_pro_18.c`, `assets/fonts/source_code_pro_20.c`, `assets/fonts/source_code_pro_24.c`

- [ ] **Step 1: Update size arrays in regen_text_fonts.sh**

At `scripts/regen_text_fonts.sh:162-164`, update the size arrays:

Regular sizes:
```bash
SIZES_REGULAR="8 10 11 12 14 16 18 20 24 26 28 32 40"
```

Light sizes:
```bash
SIZES_LIGHT="10 11 12 14 16 18 20 26"
```

Bold sizes:
```bash
SIZES_BOLD="14 16 18 20 24 28 32 40"
```

Monospace sizes (find the Source Code Pro size array in the same script):
```bash
SIZES_MONO="8 10 12 14 16 18 20 24"
```

- [ ] **Step 2: Run the text font regeneration script**

```bash
make regen-text-fonts
```
Expected: New `.c` files appear in `assets/fonts/` for each new size. Script prints a confirmation message.

- [ ] **Step 3: Verify the new files were created**

Check that all expected files exist:
```bash
ls -la assets/fonts/noto_sans_32.c assets/fonts/noto_sans_40.c assets/fonts/noto_sans_bold_32.c assets/fonts/noto_sans_bold_40.c assets/fonts/noto_sans_light_20.c assets/fonts/noto_sans_light_26.c assets/fonts/source_code_pro_18.c assets/fonts/source_code_pro_20.c assets/fonts/source_code_pro_24.c
```

- [ ] **Step 4: Commit**

```bash
git add scripts/regen_text_fonts.sh assets/fonts/noto_sans_32.c assets/fonts/noto_sans_40.c assets/fonts/noto_sans_bold_32.c assets/fonts/noto_sans_bold_40.c assets/fonts/noto_sans_light_20.c assets/fonts/noto_sans_light_26.c assets/fonts/source_code_pro_18.c assets/fonts/source_code_pro_20.c assets/fonts/source_code_pro_24.c
git commit -m "feat(fonts): generate text fonts for XLARGE/XXLARGE breakpoints"
```

---

### Task 4: Generate new icon font assets

**Files:**
- Modify: `scripts/regen_mdi_fonts.sh:322` (sizes array)
- Create: `assets/fonts/mdi_icons_20.c`, `assets/fonts/mdi_icons_28.c`, `assets/fonts/mdi_icons_40.c`, `assets/fonts/mdi_icons_56.c`, `assets/fonts/mdi_icons_80.c`, `assets/fonts/mdi_icons_96.c`, `assets/fonts/mdi_icons_128.c`

- [ ] **Step 1: Update icon sizes in regen_mdi_fonts.sh**

At `scripts/regen_mdi_fonts.sh:322`, update the sizes array:

```bash
SIZES="14 16 20 24 28 32 40 48 56 64 80 96 128"
```

- [ ] **Step 2: Run the icon font regeneration**

```bash
make regen-fonts
```
Expected: New `.c` files appear in `assets/fonts/` for each new icon size.

- [ ] **Step 3: Verify the new files were created**

```bash
ls -la assets/fonts/mdi_icons_20.c assets/fonts/mdi_icons_28.c assets/fonts/mdi_icons_40.c assets/fonts/mdi_icons_56.c assets/fonts/mdi_icons_80.c assets/fonts/mdi_icons_96.c assets/fonts/mdi_icons_128.c
```

- [ ] **Step 4: Commit**

```bash
git add scripts/regen_mdi_fonts.sh assets/fonts/mdi_icons_20.c assets/fonts/mdi_icons_28.c assets/fonts/mdi_icons_40.c assets/fonts/mdi_icons_56.c assets/fonts/mdi_icons_80.c assets/fonts/mdi_icons_96.c assets/fonts/mdi_icons_128.c
git commit -m "feat(fonts): generate icon fonts for XLARGE/XXLARGE breakpoints"
```

---

### Task 5: Add extern declarations for new fonts

**Files:**
- Modify: `include/ui_fonts.h:11-53`

- [ ] **Step 1: Add MDI icon font declarations**

After the existing MDI declarations (line 16 area), add:

```c
LV_FONT_DECLARE(mdi_icons_128); // HiDPI hero icons (XXLARGE)
LV_FONT_DECLARE(mdi_icons_96);  // HiDPI large icons (XXLARGE)
LV_FONT_DECLARE(mdi_icons_80);  // HiDPI nav icons (XLARGE)
LV_FONT_DECLARE(mdi_icons_56);  // HiDPI large icons (XLARGE)
LV_FONT_DECLARE(mdi_icons_40);  // HiDPI medium icons (XLARGE)
LV_FONT_DECLARE(mdi_icons_28);  // HiDPI small icons (XLARGE)
LV_FONT_DECLARE(mdi_icons_20);  // HiDPI extra-small icons (XLARGE)
```

- [ ] **Step 2: Add Noto Sans Regular declarations**

After existing `noto_sans_28` declaration (line 30 area), add:

```c
LV_FONT_DECLARE(noto_sans_32);
LV_FONT_DECLARE(noto_sans_40);
```

- [ ] **Step 3: Add Noto Sans Light declarations**

After existing `noto_sans_light_18` declaration (line 38 area), add:

```c
LV_FONT_DECLARE(noto_sans_light_20);
LV_FONT_DECLARE(noto_sans_light_26);
```

- [ ] **Step 4: Add Noto Sans Bold declarations**

After existing `noto_sans_bold_28` declaration (line 46 area), add:

```c
LV_FONT_DECLARE(noto_sans_bold_32);
LV_FONT_DECLARE(noto_sans_bold_40);
```

- [ ] **Step 5: Add Source Code Pro declarations**

After existing `source_code_pro_16` declaration (line 53 area), add:

```c
LV_FONT_DECLARE(source_code_pro_18);
LV_FONT_DECLARE(source_code_pro_20);
LV_FONT_DECLARE(source_code_pro_24);
```

- [ ] **Step 6: Commit**

```bash
git add include/ui_fonts.h
git commit -m "feat(fonts): add extern declarations for XLARGE/XXLARGE font assets"
```

---

### Task 6: Update FONT_SRCS in Makefile to include new fonts

**Files:**
- Modify: `Makefile:359-368` (FONT_SRCS)

- [ ] **Step 1: Add new font sources to FONT_SRCS**

At `Makefile:359`, extend each section:

After the MDI icons line (`assets/fonts/mdi_icons_14.c`), add the new icon sizes:
```makefile
FONT_SRCS += assets/fonts/mdi_icons_128.c assets/fonts/mdi_icons_96.c \
             assets/fonts/mdi_icons_80.c assets/fonts/mdi_icons_56.c \
             assets/fonts/mdi_icons_40.c assets/fonts/mdi_icons_28.c \
             assets/fonts/mdi_icons_20.c
```

After `noto_sans_28.c`, add:
```makefile
             assets/fonts/noto_sans_32.c assets/fonts/noto_sans_40.c
```

After `noto_sans_light_18.c`, add:
```makefile
             assets/fonts/noto_sans_light_20.c assets/fonts/noto_sans_light_26.c
```

After `noto_sans_bold_28.c`, add:
```makefile
             assets/fonts/noto_sans_bold_32.c assets/fonts/noto_sans_bold_40.c
```

After `source_code_pro_16.c`, add:
```makefile
             assets/fonts/source_code_pro_18.c assets/fonts/source_code_pro_20.c \
             assets/fonts/source_code_pro_24.c
```

- [ ] **Step 2: Build to verify all fonts compile and link**

```bash
make -j
```
Expected: Clean build. All new font symbols resolve.

- [ ] **Step 3: Commit**

```bash
git add Makefile
git commit -m "build: add XLARGE/XXLARGE font sources to FONT_SRCS"
```

---

### Task 7: Update globals.xml with XLARGE and XXLARGE values

**Files:**
- Modify: `ui_xml/globals.xml`

This is the largest single change. Update every responsive constant section.

- [ ] **Step 1: Update comments at top of responsive sections**

At `ui_xml/globals.xml:26-29`, update the breakpoint documentation comment:

```xml
    <!-- Runtime selects appropriate variant via ui_theme_register_responsive_spacing()
         based on screen height: MICRO (≤272), TINY (273-390), SMALL (391-460),
         MEDIUM (461-550), LARGE (551-700), XLARGE (701-1000), XXLARGE (>1000)
         Target hardware: 480x272 (MICRO), 480x320 (TINY), 480x400 (SMALL),
         800x480 (MEDIUM), 1024x600 (LARGE), 1280x720 (XLARGE), 1440p+ (XXLARGE)
         _tiny, _xlarge and _xxlarge variants are optional — they fall back to
         _small, _large, and _xlarge respectively. -->
```

- [ ] **Step 2: Update spacing constants**

Add `_xlarge` and `_xxlarge` variants for each spacing token. After each existing `_large` line, add the two new variants:

```xml
    <!-- space_xxs -->
    <px name="space_xxs_xlarge" value="5"/>
    <px name="space_xxs_xxlarge" value="6"/>
    <!-- space_xs -->
    <px name="space_xs_xlarge" value="8"/>
    <px name="space_xs_xxlarge" value="10"/>
    <!-- space_sm -->
    <px name="space_sm_xlarge" value="10"/>
    <px name="space_sm_xxlarge" value="12"/>
    <!-- space_md -->
    <px name="space_md_xlarge" value="16"/>
    <px name="space_md_xxlarge" value="20"/>
    <!-- space_lg -->
    <px name="space_lg_xlarge" value="24"/>
    <px name="space_lg_xxlarge" value="32"/>
    <!-- space_xl -->
    <px name="space_xl_xlarge" value="32"/>
    <px name="space_xl_xxlarge" value="40"/>
    <!-- space_2xl -->
    <px name="space_2xl_xlarge" value="48"/>
    <px name="space_2xl_xxlarge" value="64"/>
    <!-- space_2xl_neg -->
    <px name="space_2xl_neg_xlarge" value="-48"/>
    <px name="space_2xl_neg_xxlarge" value="-64"/>
```

- [ ] **Step 3: Update icon_size**

Add after `icon_size_large`:

```xml
    <string name="icon_size_xlarge" value="xl"/>
    <string name="icon_size_xxlarge" value="xl"/>
```

- [ ] **Step 4: Update badge_size**

Add after `badge_size_large`:

```xml
    <px name="badge_size_xlarge" value="24"/>
    <px name="badge_size_xxlarge" value="28"/>
```

- [ ] **Step 5: Update spinner sizes**

Add after each spinner `_large` line:

```xml
    <px name="spinner_lg_xlarge" value="80"/>
    <px name="spinner_lg_xxlarge" value="96"/>
    <px name="spinner_md_xlarge" value="40"/>
    <px name="spinner_md_xxlarge" value="48"/>
    <px name="spinner_sm_xlarge" value="24"/>
    <px name="spinner_sm_xxlarge" value="28"/>
    <px name="spinner_xs_xlarge" value="18"/>
    <px name="spinner_xs_xxlarge" value="20"/>
    <px name="spinner_arc_lg_xlarge" value="5"/>
    <px name="spinner_arc_lg_xxlarge" value="6"/>
    <px name="spinner_arc_md_xlarge" value="4"/>
    <px name="spinner_arc_md_xxlarge" value="4"/>
```

- [ ] **Step 6: Update AMS sizes**

Add after each AMS `_large` line:

```xml
    <px name="ams_logo_size_xlarge" value="32"/>
    <px name="ams_logo_size_xxlarge" value="40"/>
    <px name="ams_bars_height_xlarge" value="68"/>
    <px name="ams_bars_height_xxlarge" value="80"/>
    <px name="ams_card_min_width_xlarge" value="140"/>
    <px name="ams_card_min_width_xxlarge" value="180"/>
    <px name="ams_card_max_width_xlarge" value="280"/>
    <px name="ams_card_max_width_xxlarge" value="360"/>
```

- [ ] **Step 7: Update button heights**

Add after each button height `_large` line:

```xml
    <px name="button_height_sm_xlarge" value="48"/>
    <px name="button_height_sm_xxlarge" value="56"/>
    <px name="button_height_xlarge" value="80"/>
    <px name="button_height_xxlarge" value="96"/>
    <px name="button_height_lg_xlarge" value="112"/>
    <px name="button_height_lg_xxlarge" value="128"/>
```

- [ ] **Step 8: Update header_height and temp_card_height**

```xml
    <px name="header_height_xlarge" value="68"/>
    <px name="header_height_xxlarge" value="80"/>
    <px name="temp_card_height_xlarge" value="96"/>
    <px name="temp_card_height_xxlarge" value="112"/>
```

- [ ] **Step 9: Update border_radius**

Add a `_xlarge` and `_xxlarge` variant:

```xml
    <px name="border_radius_xlarge" value="14"/>
    <px name="border_radius_xxlarge" value="16"/>
```

- [ ] **Step 10: Fix existing XLARGE font values and add XXLARGE font constants**

Replace existing `_xlarge` font lines and add `_xxlarge`:

```xml
    <!-- font_heading -->
    <string name="font_heading_xlarge" value="noto_sans_32"/>
    <string name="font_heading_xxlarge" value="noto_sans_40"/>
    <!-- font_xl (bold) -->
    <string name="font_xl_xlarge" value="noto_sans_bold_32"/>
    <string name="font_xl_xxlarge" value="noto_sans_bold_40"/>
    <!-- font_body -->
    <string name="font_body_xlarge" value="noto_sans_24"/>
    <string name="font_body_xxlarge" value="noto_sans_32"/>
    <!-- font_small (light) -->
    <string name="font_small_xlarge" value="noto_sans_light_20"/>
    <string name="font_small_xxlarge" value="noto_sans_light_26"/>
    <!-- font_xs (light) -->
    <string name="font_xs_xlarge" value="noto_sans_light_16"/>
    <string name="font_xs_xxlarge" value="noto_sans_light_20"/>
    <!-- font_mono -->
    <string name="font_mono_xlarge" value="source_code_pro_18"/>
    <string name="font_mono_xxlarge" value="source_code_pro_24"/>
```

- [ ] **Step 11: Fix existing XLARGE icon font values and add XXLARGE icon font constants**

Replace existing `_xlarge` icon font lines and add `_xxlarge`:

```xml
    <!-- icon_font_xs -->
    <string name="icon_font_xs_xlarge" value="mdi_icons_24"/>
    <string name="icon_font_xs_xxlarge" value="mdi_icons_32"/>
    <!-- icon_font_sm -->
    <string name="icon_font_sm_xlarge" value="mdi_icons_32"/>
    <string name="icon_font_sm_xxlarge" value="mdi_icons_48"/>
    <!-- icon_font_md -->
    <string name="icon_font_md_xlarge" value="mdi_icons_48"/>
    <string name="icon_font_md_xxlarge" value="mdi_icons_64"/>
    <!-- icon_font_lg -->
    <string name="icon_font_lg_xlarge" value="mdi_icons_64"/>
    <string name="icon_font_lg_xxlarge" value="mdi_icons_96"/>
    <!-- icon_font_xl -->
    <string name="icon_font_xl_xlarge" value="mdi_icons_80"/>
    <string name="icon_font_xl_xxlarge" value="mdi_icons_128"/>
```

- [ ] **Step 12: Launch the app to verify at default resolution**

```bash
./build/bin/helix-screen --test -vv 2>&1 | head -50
```
Expected: No font warnings, theme reports correct breakpoint for the display.

- [ ] **Step 13: Commit**

```bash
git add ui_xml/globals.xml
git commit -m "feat(theme): add XLARGE/XXLARGE responsive values for all font, icon, spacing, and component constants"
```

---

### Task 8: Update CJK font manager for new font sizes

**Files:**
- Modify: `src/system/cjk_font_manager.cpp:17-46` (font mapping arrays)
- Modify: `scripts/regen_text_fonts.sh` (CJK .bin size arrays, if separate from main sizes)

- [ ] **Step 1: Add new regular font mappings**

At `src/system/cjk_font_manager.cpp`, add to `REGULAR_FONTS` array after the `noto_sans_28` entry:

```cpp
    {&noto_sans_32, "noto_sans_cjk_32.bin"},
    {&noto_sans_40, "noto_sans_cjk_40.bin"},
```

- [ ] **Step 2: Add new bold font mappings**

Add to `BOLD_FONTS` array after the `noto_sans_bold_28` entry:

```cpp
    {&noto_sans_bold_32, "noto_sans_cjk_bold_32.bin"},
    {&noto_sans_bold_40, "noto_sans_cjk_bold_40.bin"},
```

- [ ] **Step 3: Add new light font mappings**

Add to `LIGHT_FONTS` array after the `noto_sans_light_18` entry:

```cpp
    {&noto_sans_light_20, "noto_sans_cjk_light_20.bin"},
    {&noto_sans_light_26, "noto_sans_cjk_light_26.bin"},
```

- [ ] **Step 4: Verify CJK .bin size arrays in regen_text_fonts.sh match**

Check that `scripts/regen_text_fonts.sh` generates `.bin` files for the new sizes. The CJK `.bin` generation section should use the same size arrays updated in Task 3. If CJK sizes are specified separately, update them to match.

- [ ] **Step 5: Regenerate CJK .bin files**

```bash
make regen-text-fonts
```
Expected: New `.bin` files appear in `assets/fonts/cjk/` for sizes 32, 40 (regular+bold) and 20, 26 (light).

- [ ] **Step 6: Verify the new .bin files**

```bash
ls -la assets/fonts/cjk/noto_sans_cjk_32.bin assets/fonts/cjk/noto_sans_cjk_40.bin assets/fonts/cjk/noto_sans_cjk_bold_32.bin assets/fonts/cjk/noto_sans_cjk_bold_40.bin assets/fonts/cjk/noto_sans_cjk_light_20.bin assets/fonts/cjk/noto_sans_cjk_light_26.bin
```

- [ ] **Step 7: Build and verify**

```bash
make -j
```
Expected: Clean build.

- [ ] **Step 8: Commit**

```bash
git add src/system/cjk_font_manager.cpp assets/fonts/cjk/
git commit -m "feat(fonts): add CJK fallback mappings and .bin files for XLARGE/XXLARGE sizes"
```

---

### Task 9: Per-platform font tier pruning — build system

**Files:**
- Modify: `mk/cross.mk` (add `FONT_TIERS` to each platform)
- Modify: `mk/fonts.mk` (add per-tier font lists)
- Modify: `Makefile:359-368` (replace flat FONT_SRCS with tier-based assembly)

- [ ] **Step 1: Define per-tier font lists in mk/fonts.mk**

Add a new section at the top of `mk/fonts.mk` that maps each tier to its font files. Each list contains exactly the fonts referenced by that tier's `globals.xml` constants:

```makefile
# =============================================================================
# Per-tier font file lists
# =============================================================================
# Each tier declares the text fonts and icon fonts it references in globals.xml.
# FONT_SRCS is assembled from the union of all tiers in FONT_TIERS.

FONTS_MICRO := assets/fonts/noto_sans_10.c assets/fonts/noto_sans_14.c \
               assets/fonts/noto_sans_bold_16.c \
               assets/fonts/noto_sans_light_10.c \
               assets/fonts/source_code_pro_8.c \
               assets/fonts/mdi_icons_16.c assets/fonts/mdi_icons_24.c \
               assets/fonts/mdi_icons_32.c

FONTS_TINY := assets/fonts/noto_sans_12.c assets/fonts/noto_sans_16.c \
              assets/fonts/noto_sans_bold_18.c \
              assets/fonts/noto_sans_light_11.c \
              assets/fonts/source_code_pro_10.c \
              assets/fonts/mdi_icons_24.c assets/fonts/mdi_icons_32.c \
              assets/fonts/mdi_icons_48.c

FONTS_SMALL := assets/fonts/noto_sans_14.c assets/fonts/noto_sans_20.c \
               assets/fonts/noto_sans_bold_20.c \
               assets/fonts/noto_sans_light_12.c \
               assets/fonts/source_code_pro_12.c \
               assets/fonts/mdi_icons_16.c assets/fonts/mdi_icons_24.c \
               assets/fonts/mdi_icons_32.c assets/fonts/mdi_icons_48.c \
               assets/fonts/mdi_icons_64.c

FONTS_MEDIUM := assets/fonts/noto_sans_18.c assets/fonts/noto_sans_26.c \
                assets/fonts/noto_sans_bold_28.c \
                assets/fonts/noto_sans_light_16.c \
                assets/fonts/source_code_pro_14.c \
                assets/fonts/mdi_icons_16.c assets/fonts/mdi_icons_24.c \
                assets/fonts/mdi_icons_32.c assets/fonts/mdi_icons_48.c \
                assets/fonts/mdi_icons_64.c

FONTS_LARGE := assets/fonts/noto_sans_20.c assets/fonts/noto_sans_28.c \
               assets/fonts/noto_sans_bold_28.c \
               assets/fonts/noto_sans_light_14.c assets/fonts/noto_sans_light_18.c \
               assets/fonts/source_code_pro_16.c \
               assets/fonts/mdi_icons_16.c assets/fonts/mdi_icons_24.c \
               assets/fonts/mdi_icons_32.c assets/fonts/mdi_icons_48.c \
               assets/fonts/mdi_icons_64.c

FONTS_XLARGE := assets/fonts/noto_sans_24.c assets/fonts/noto_sans_32.c \
                assets/fonts/noto_sans_bold_32.c \
                assets/fonts/noto_sans_light_16.c assets/fonts/noto_sans_light_20.c \
                assets/fonts/source_code_pro_18.c \
                assets/fonts/mdi_icons_24.c assets/fonts/mdi_icons_32.c \
                assets/fonts/mdi_icons_48.c assets/fonts/mdi_icons_64.c \
                assets/fonts/mdi_icons_80.c

FONTS_XXLARGE := assets/fonts/noto_sans_32.c assets/fonts/noto_sans_40.c \
                 assets/fonts/noto_sans_bold_40.c \
                 assets/fonts/noto_sans_light_20.c assets/fonts/noto_sans_light_26.c \
                 assets/fonts/source_code_pro_20.c assets/fonts/source_code_pro_24.c \
                 assets/fonts/mdi_icons_32.c assets/fonts/mdi_icons_48.c \
                 assets/fonts/mdi_icons_64.c assets/fonts/mdi_icons_96.c \
                 assets/fonts/mdi_icons_128.c

FONTS_ALL := $(sort $(FONTS_MICRO) $(FONTS_TINY) $(FONTS_SMALL) $(FONTS_MEDIUM) \
             $(FONTS_LARGE) $(FONTS_XLARGE) $(FONTS_XXLARGE))

# Assemble FONT_SRCS from declared tiers (sort deduplicates)
FONT_TIERS ?= all
ifeq ($(FONT_TIERS),all)
    TIER_FONT_SRCS := $(FONTS_ALL)
else
    TIER_FONT_SRCS :=
    ifneq ($(filter micro,$(FONT_TIERS)),)
        TIER_FONT_SRCS += $(FONTS_MICRO)
    endif
    ifneq ($(filter tiny,$(FONT_TIERS)),)
        TIER_FONT_SRCS += $(FONTS_TINY)
    endif
    ifneq ($(filter small,$(FONT_TIERS)),)
        TIER_FONT_SRCS += $(FONTS_SMALL)
    endif
    ifneq ($(filter medium,$(FONT_TIERS)),)
        TIER_FONT_SRCS += $(FONTS_MEDIUM)
    endif
    ifneq ($(filter large,$(FONT_TIERS)),)
        TIER_FONT_SRCS += $(FONTS_LARGE)
    endif
    ifneq ($(filter xlarge,$(FONT_TIERS)),)
        TIER_FONT_SRCS += $(FONTS_XLARGE)
    endif
    ifneq ($(filter xxlarge,$(FONT_TIERS)),)
        TIER_FONT_SRCS += $(FONTS_XXLARGE)
    endif
    TIER_FONT_SRCS := $(sort $(TIER_FONT_SRCS))
endif
```

- [ ] **Step 2: Replace flat FONT_SRCS in Makefile with tier-based assembly**

At `Makefile:359-368`, replace the entire `FONT_SRCS` block with:

```makefile
# Font sources — assembled per-platform from mk/fonts.mk tier lists
# FONT_TIERS is set per-platform in mk/cross.mk (default: all)
FONT_SRCS := $(TIER_FONT_SRCS)
```

Ensure `mk/fonts.mk` is included before this line in the Makefile (check include order).

- [ ] **Step 3: Add FONT_TIERS to each platform in mk/cross.mk**

Add `FONT_TIERS :=` to each platform block:

```makefile
# cc1 block (around line 279):
    FONT_TIERS := micro tiny

# snapmaker-u1 block (around line 443):
    FONT_TIERS := tiny small

# mips/k1 block (around line 320):
    FONT_TIERS := small medium

# k1-dynamic block (around line 375):
    FONT_TIERS := small medium

# ad5m block (around line 203):
    FONT_TIERS := medium large

# ad5x block (around line 245):
    FONT_TIERS := medium large

# k2 block (around line 411):
    FONT_TIERS := large xlarge

# pi block (around line 57):
    FONT_TIERS := all

# pi-fbdev block (around line 85):
    FONT_TIERS := all

# pi32 block (around line 130):
    FONT_TIERS := all

# pi32-fbdev block (around line 156):
    FONT_TIERS := all

# x86 block (around line 470):
    FONT_TIERS := all

# x86-fbdev block (around line 494):
    FONT_TIERS := all

# native/SDL block (around line 538):
    FONT_TIERS := all
```

- [ ] **Step 4: Build native to verify no fonts are missing**

```bash
make -j
```
Expected: Clean build with all fonts. Native uses `FONT_TIERS := all`.

- [ ] **Step 5: Verify pruning works for a constrained platform**

```bash
PLATFORM_TARGET=ad5m make -n 2>&1 | grep "noto_sans_32"
```
Expected: `noto_sans_32.c` should NOT appear (AD5M only has medium+large tiers).

```bash
PLATFORM_TARGET=ad5m make -n 2>&1 | grep "noto_sans_18"
```
Expected: `noto_sans_18.c` SHOULD appear (it's in FONTS_MEDIUM).

- [ ] **Step 6: Commit**

```bash
git add mk/fonts.mk mk/cross.mk Makefile
git commit -m "build: per-platform font tier pruning — each platform ships only needed font sizes"
```

---

### Task 10: Smart fallback warnings in theme registration

**Files:**
- Modify: `mk/cross.mk` (add `HELIX_MAX_FONT_TIER` define per platform)
- Modify: `src/ui/theme_manager.cpp` (use tier constant for warning logic)

- [ ] **Step 1: Add HELIX_MAX_FONT_TIER compile-time constant**

In `mk/cross.mk`, add a `TARGET_CFLAGS` define for each platform based on its highest compiled tier. Add this alongside existing `TARGET_CFLAGS` lines:

For platforms with `FONT_TIERS := micro tiny`:
```makefile
    TARGET_CFLAGS += -DHELIX_MAX_FONT_TIER=1  # tiny
```

For `FONT_TIERS := tiny small`:
```makefile
    TARGET_CFLAGS += -DHELIX_MAX_FONT_TIER=2  # small
```

For `FONT_TIERS := small medium`:
```makefile
    TARGET_CFLAGS += -DHELIX_MAX_FONT_TIER=3  # medium
```

For `FONT_TIERS := medium large`:
```makefile
    TARGET_CFLAGS += -DHELIX_MAX_FONT_TIER=4  # large
```

For `FONT_TIERS := large xlarge`:
```makefile
    TARGET_CFLAGS += -DHELIX_MAX_FONT_TIER=5  # xlarge
```

For `FONT_TIERS := all`:
```makefile
    TARGET_CFLAGS += -DHELIX_MAX_FONT_TIER=6  # xxlarge (all tiers)
```

Also add a default for native builds in the Makefile (not cross-compiled):
```makefile
CFLAGS += -DHELIX_MAX_FONT_TIER=6
```

- [ ] **Step 2: Update the fallback logic in register_responsive_fonts()**

In `src/ui/theme_manager.cpp`, add a helper that maps tier suffix to tier number:

```cpp
static int tier_number_from_suffix(const char* suffix) {
    if (strcmp(suffix, "_micro") == 0) return 0;
    if (strcmp(suffix, "_tiny") == 0) return 1;
    if (strcmp(suffix, "_small") == 0) return 2;
    if (strcmp(suffix, "_medium") == 0) return 3;
    if (strcmp(suffix, "_large") == 0) return 4;
    if (strcmp(suffix, "_xlarge") == 0) return 5;
    if (strcmp(suffix, "_xxlarge") == 0) return 6;
    return -1;
}
```

Then in the fallback paths of both `register_responsive_spacing()` and `register_responsive_fonts()`, when a token's selected-tier value can't be resolved to a real font (font symbol not linked), check whether the tier is within the compiled range:

```cpp
// After resolving value but before registering:
// (This is conceptual — the actual check depends on how font resolution
// works. If the XML constant exists but points to a font name that isn't
// linked, LVGL's XML parser will emit a warning. The key change is:
// the theme system should log at warn level if the missing font's tier
// <= HELIX_MAX_FONT_TIER, and at trace level if above.)

#ifndef HELIX_MAX_FONT_TIER
#define HELIX_MAX_FONT_TIER 6  // default: all tiers
#endif
```

The specific implementation depends on whether LVGL's `lv_xml_register_const()` validates font names or just stores strings. If it stores strings and the validation happens at render time, the warning logic may need to go in the font lookup path instead. The implementer should check `lv_xml_get_const_internal()` behavior and place the tier check accordingly.

- [ ] **Step 3: Build and test**

```bash
make -j
```
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add mk/cross.mk Makefile src/ui/theme_manager.cpp
git commit -m "feat(theme): smart fallback warnings — warn for expected fonts, silent for pruned tiers"
```

---

### Task 11: Smoke test at XLARGE and XXLARGE resolutions

**Files:** None (testing only)

- [ ] **Step 1: Test at 720p (XLARGE tier)**

```bash
./build/bin/helix-screen --test -vv -s 1280x720 2>&1 | grep -E "XLARGE|XXLARGE|breakpoint|font"
```
Expected: Log shows "XLARGE" breakpoint selected. No font warnings.

- [ ] **Step 2: Test at 1440p (XXLARGE tier)**

```bash
./build/bin/helix-screen --test -vv -s 2560x1440 2>&1 | grep -E "XLARGE|XXLARGE|breakpoint|font"
```
Expected: Log shows "XXLARGE" breakpoint selected. No font warnings. Larger fonts and icons render.

- [ ] **Step 3: Test at 800x480 (MEDIUM, regression check)**

```bash
./build/bin/helix-screen --test -vv -s 800x480 2>&1 | grep -E "MEDIUM|breakpoint|font"
```
Expected: Log shows "MEDIUM" breakpoint. Existing behavior unchanged.

- [ ] **Step 4: Visual inspection**

Launch at XLARGE and XXLARGE and visually confirm text and icons are proportionally larger. Ask user to inspect if display hardware is available.

```bash
./build/bin/helix-screen --test -vv -s 1280x720
```

```bash
./build/bin/helix-screen --test -vv -s 2560x1440
```

---

### Task 12: Update issue #773 with progress

**Files:** None

- [ ] **Step 1: Comment on the issue**

Post a comment on prestonbrown/helixscreen#773 summarizing the changes and asking ago1776 to test a preview build when available:

```
gh issue comment 773 --body "Implemented XXLARGE breakpoint tier and per-platform font pruning. Changes include:

- New XXLARGE tier (>1000px height) with scaled fonts (32px body, 40px heading) and icons (up to 128px)
- Fixed XLARGE tier (701-1000px) to scale properly instead of plateauing at LARGE values
- Per-platform font pruning — constrained devices only ship the fonts they need
- 7 new icon font sizes, 9 new text font sizes

Would appreciate testing on your 5.5\" QHD panel when the next build is available."
```
