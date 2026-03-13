# Runtime CJK Font Loading

**Date:** 2026-03-12
**Status:** Draft
**Problem:** v0.98.3 shipped with broken CJK glyphs because `regen_text_fonts.sh` silently fell back to Latin-only when CJK source fonts were missing. The immediate fix (auto-download + hard-fail) restores CJK, but compiling all ~1047 CJK glyphs into every font size adds ~15-20 MB to .rodata — memory wasted for non-CJK users.

## Goal

CJK glyphs load on demand: zero memory cost for non-CJK users, full Chinese/Japanese support when needed. The wizard welcome page always renders CJK regardless of locale.

## Design

### Two-Tier Font Strategy

**Tier 1 — Compiled-in (12 codepoints, always available):**

The wizard language chooser displays CJK text before any locale is selected. These 12 codepoints are baked into every compiled Noto Sans `.c` font:

| Char | Codepoint | Source |
|------|-----------|--------|
| 欢 | U+6B22 | Welcome cycling text |
| 迎 | U+8FCE | Welcome cycling text |
| ！ | U+FF01 | Welcome cycling text (shared) |
| 中 | U+4E2D | Language list button |
| 文 | U+6587 | Language list button |
| よ | U+3088 | Welcome cycling text |
| う | U+3046 | Welcome cycling text |
| こ | U+3053 | Welcome cycling text |
| そ | U+305D | Welcome cycling text |
| 日 | U+65E5 | Language list button |
| 本 | U+672C | Language list button |
| 語 | U+8A9E | Language list button |

Memory cost: negligible (~few KB across all sizes).

**Tier 2 — Runtime-loaded (.bin fallback fonts, on demand):**

The remaining ~1035 CJK characters (extracted from `translations/zh.yml`, `translations/ja.yml`, and C++ sources) are generated as LVGL binary font files (`.bin`). Loaded via `lv_binfont_create()` when a CJK locale is selected, set as `fallback` on compiled Latin fonts. Destroyed when switching away from CJK or at shutdown.

### .bin Font Sizes

Only sizes where translated text actually renders. Matches the responsive breakpoint system:

| Weight | Sizes | .bin files |
|--------|-------|------------|
| Regular | 10, 11, 12, 14, 16, 18, 20, 24, 26, 28 | 10 files |
| Bold | 14, 16, 18, 20, 24, 28 | 6 files |
| Light | 10, 11, 12, 14, 16, 18 | 6 files |

**Total: 22 `.bin` files.** Each contains both SC (Chinese) and JP (Japanese) glyphs — they share the same extracted character set.

Stored at: `assets/fonts/cjk/noto_sans_cjk_{size}.bin`, `noto_sans_cjk_bold_{size}.bin`, `noto_sans_cjk_light_{size}.bin`.

### Fallback Mechanism

LVGL fonts have a `fallback` pointer that is resolved recursively during glyph lookup. When CJK fonts are loaded:

```
noto_sans_14 (compiled, Latin + 12 CJK)
  └─ fallback → noto_sans_cjk_14 (runtime .bin, ~1035 CJK)
```

This is transparent to XML — no binding changes needed. LVGL automatically follows the fallback chain when a glyph isn't found in the primary font.

### Const Font Mutation

`lv_font_conv` generates fonts as `const lv_font_t` (LVGL 8+ path). Setting the `fallback` pointer requires a `const_cast`. This is safe because:

1. The font structs have non-trivial initializers (function pointers, data pointers) so the linker places them in `.data`, not `.rodata` — they're in writable memory.
2. We only modify the `fallback` pointer, which is initialized to `NULL` by `lv_font_conv` — we're not corrupting any meaningful state.
3. The mutation happens from the UI thread only, and LVGL rendering also runs on the UI thread — no data race.

Alternative considered: stripping `const` from generated `.c` files via sed in the regen script. Rejected as fragile — a `const_cast` in one well-documented place is cleaner.

```cpp
// In CjkFontManager — the only place this const_cast is used
static void set_fallback(const lv_font_t* compiled, lv_font_t* cjk) {
    const_cast<lv_font_t*>(compiled)->fallback = cjk;
}
```

### New Code: CjkFontManager

**Files:** `include/cjk_font_manager.h`, `src/system/cjk_font_manager.cpp`

Singleton managing the CJK font lifecycle:

```cpp
class CjkFontManager {
public:
    static CjkFontManager& instance();

    // Load/unload CJK fonts based on locale.
    // Decides internally whether the locale needs CJK — callers don't check.
    // Idempotent: calling with same locale is a no-op.
    void on_language_changed(const std::string& lang);

    // Whether CJK fonts are currently loaded.
    bool is_loaded() const;

    // Cleanup before lv_deinit(). Called from Application::shutdown().
    void shutdown();

private:
    static bool needs_cjk(const std::string& lang);
    bool load();
    void unload();

    struct FontEntry {
        const lv_font_t* compiled_font;  // The compiled Latin font to set fallback on
        lv_font_t* cjk_font;            // The runtime-loaded .bin font
    };
    std::vector<FontEntry> loaded_fonts_;
    bool loaded_ = false;
    std::string current_lang_;
};
```

**Key behaviors:**
- `on_language_changed()` decides internally whether to load or unload — the call site is unconditional
- `needs_cjk()` is the single source of truth for which locales need CJK (currently `"zh"` and `"ja"`, easily extended for `"ko"`, `"zh-TW"`, etc.)
- `load()` calls `lv_binfont_create()` for each font and sets fallback pointers
- `unload()` calls `lv_binfont_destroy()` for each loaded font and nulls the fallback pointers
- `shutdown()` calls `unload()` — must be called before `lv_deinit()`
- Logs via spdlog at info level (load/unload) and warn level (failures)
- If a `.bin` file fails to load, logs a warning and continues — partial CJK is better than crashing

### Hook Points

**1. Language change** — `SystemSettingsManager::set_language()`, after `lv_translation_set_language()`:
```cpp
CjkFontManager::instance().on_language_changed(lang);
```
Unconditional call. The manager decides whether to load or unload.

**2. Startup** — `Application::init_translations()`, after `lv_i18n_set_locale()` (line ~1115), before `init_ui()`:
```cpp
CjkFontManager::instance().on_language_changed(lang);
```
This is a separate code path from `set_language()` — startup reads directly from `Config::get_language()` and never calls `SystemSettingsManager::set_language()`. Must have its own explicit call.

**3. Shutdown** — `Application::shutdown()`, after `StaticSubjectRegistry::deinit_all()` (line ~3078) but before `m_display.reset()` (line ~3091):
```cpp
CjkFontManager::instance().shutdown();
```
Ordering matters: panels and subjects must be destroyed first (they may reference fonts during destruction), but LVGL must still be alive for `lv_binfont_destroy()` to work.

### Soft Restart (tear_down_printer_state)

`tear_down_printer_state()` destroys and recreates panels when switching printers. Language settings persist across soft restarts (global config, not per-printer). CJK fonts are **not affected** — they stay loaded since the language didn't change. The fallback pointers on compiled fonts survive panel rebuild since fonts aren't touched. No special handling needed.

### Font Generation Changes

**`scripts/regen_text_fonts.sh` modifications:**

1. **Compiled fonts (.c):** Include only the 12 wizard codepoints for CJK (hardcoded list), not the full translation extraction. Latin/Cyrillic ranges unchanged.

2. **New: Generate .bin files:** After generating `.c` files, generate `.bin` files with the full ~1035 CJK character set for each size/weight. Uses `lv_font_conv --format bin`.

3. **Wizard codepoints are hardcoded**, not extracted from translations. They're structural (wizard UI) not content (translations), so they shouldn't change unless the wizard changes.

**`mk/fonts.mk` modifications:**

- Add `regen-cjk-fonts` target
- Update `regen-text-fonts` to also generate `.bin` files
- `.bin` files tracked in git (same as `.c` files) — they're build artifacts needed for cross-compilation

### Filesystem

LVGL POSIX filesystem driver is already enabled (`LV_USE_FS_POSIX=1`, letter `'A'`, empty path prefix). Font path resolution:

- Development: `"A:assets/fonts/cjk/noto_sans_cjk_14.bin"`
- Device: Same — `A:` prefix resolves relative to working directory, which is `~/helixscreen/` on device

### Release Packaging

`.bin` files ship in the release tarball alongside the binary. They're small compared to the source `.otf` files (~1-3 MB each for 1035 glyphs, vs ~16 MB per `.otf`). No on-device downloading needed.

### Memory Budget

| Scenario | .rodata (compiled) | Heap (runtime) |
|----------|-------------------|----------------|
| Non-CJK user | ~11 MB (Latin + 12 CJK codepoints) | 0 |
| CJK user | ~11 MB | ~5-10 MB (22 .bin fonts loaded) |
| Current (all compiled) | ~47 MB (all CJK compiled in) | 0 |

Net savings for non-CJK users: ~36 MB of .rodata eliminated.

Note: heap estimate will be validated during implementation by measuring actual `.bin` file sizes.

### Testing Strategy

1. **Unit tests for CjkFontManager:**
   - `on_language_changed("zh")` loads fonts, `is_loaded()` returns true
   - `on_language_changed("en")` unloads, `is_loaded()` returns false
   - `on_language_changed("zh")` twice is idempotent
   - `shutdown()` cleans up after load
   - Missing `.bin` file logs warning, doesn't crash
   - `on_language_changed("ja")` also loads (both CJK locales work)

2. **Font generation tests (shell):**
   - Compiled `.c` fonts contain the 12 wizard codepoints
   - Compiled `.c` fonts do NOT contain other CJK codepoints (spot-check a few)
   - `.bin` files are generated for all 22 expected sizes/weights
   - `.bin` files are non-empty and valid (lv_binfont_create succeeds)

3. **Integration tests:**
   - Language switch to zh loads CJK fonts (verify via is_loaded)
   - Language switch from zh to en unloads CJK fonts
   - Wizard welcome text renders correctly (compiled-in codepoints)

4. **Manual verification:**
   - Run with `--test -vv`, switch to Chinese, verify all text renders
   - Run with English, verify no CJK fonts loaded (check logs)
   - Wizard page: verify welcome cycling and language buttons render CJK

## Files Changed

| File | Change |
|------|--------|
| `include/cjk_font_manager.h` | **New** — CjkFontManager class declaration |
| `src/system/cjk_font_manager.cpp` | **New** — Implementation |
| `src/system/system_settings_manager.cpp` | Hook `on_language_changed()` after translation switch |
| `src/application/application.cpp` | Hook startup load + shutdown cleanup |
| `scripts/regen_text_fonts.sh` | Split CJK: 12 compiled codepoints + .bin generation |
| `mk/fonts.mk` | Add .bin generation targets |
| `assets/fonts/*.c` | Regenerated with only 12 CJK codepoints |
| `assets/fonts/cjk/*.bin` | **New** — Runtime CJK font files |
| `tests/test_cjk_font_manager.cpp` | **New** — Unit tests |

## Non-Goals

- Korean support (no translations exist yet — add when needed)
- Per-language font files (SC and JP share the same .bin — simpler)
- Font subsetting per-locale (not worth the complexity for ~1035 chars)
- Streaming/lazy glyph loading (LVGL doesn't support it)
- Lazy per-size loading (optimization for future — load all 22 at once for now)
