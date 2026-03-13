# Runtime CJK Font Loading — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load CJK font glyphs on demand so non-CJK users pay zero memory cost, while the wizard welcome page always renders Chinese/Japanese.

**Architecture:** Two-tier fonts — 12 CJK codepoints compiled into `.c` fonts (wizard welcome), remaining ~1035 CJK chars in `.bin` files loaded at runtime via LVGL's `lv_binfont_create()` fallback chain. New `CjkFontManager` singleton manages lifecycle.

**Tech Stack:** LVGL 9.5 binfont loader, lv_font_conv (Node.js), Catch2 tests, bash font generation scripts

**Spec:** `docs/superpowers/specs/2026-03-12-runtime-cjk-fonts-design.md`

**Note on CJK font weights:** Only `NotoSansCJKsc-Regular.otf` and `NotoSansCJKjp-Regular.otf` are available. Bold/Light `.bin` files use the Regular CJK source — this matches the existing approach (the compiled fonts already do this). CJK scripts have less perceptual weight variation than Latin, so this is acceptable.

**Build system:** Both app sources (`src/*/*.cpp`) and test sources (`tests/unit/*.cpp`) use Makefile wildcards. Creating new `.cpp` files in the right directories is sufficient — no manual registration needed.

---

## Chunk 1: Font Generation Pipeline

Modify `regen_text_fonts.sh` to produce two outputs: compiled `.c` fonts with only 12 wizard CJK codepoints, and `.bin` files with the full CJK character set.

### Task 1: Update regen script — split CJK into compiled + runtime

**Files:**
- Modify: `scripts/regen_text_fonts.sh`

- [ ] **Step 1: Add hardcoded wizard codepoints constant**

Near the top of the script (after the `UNICODE_RANGES` block, around line 93), add:

```bash
# Wizard welcome page CJK codepoints — always compiled into .c fonts
# 欢迎！中文ようこそ！日本語
WIZARD_CJK="0x3046,0x3053,0x305d,0x3088,0x4e2d,0x6587,0x65e5,0x672c,0x6b22,0x8a9e,0x8fce,0xff01"
```

- [ ] **Step 2: Modify CJK extraction to separate wizard chars from runtime chars**

Replace the current `CJKCHARS` extraction block (lines ~95-146) with:

```bash
# Extract CJK characters from translations and C++ sources
echo "Extracting CJK characters from translations and C++ sources..."
ALL_CJKCHARS=$(python3 << 'EOF'
import glob
import re

chars = set()

CJK_RANGES = [
    r'[\u3000-\u303f]',
    r'[\u3040-\u309f]',
    r'[\u30a0-\u30ff]',
    r'[\u3400-\u4dbf]',
    r'[\u4e00-\u9fff]',
    r'[\uff00-\uffef]',
]

def extract_cjk(content):
    found = set()
    for pattern in CJK_RANGES:
        found.update(re.findall(pattern, content))
    return found

for path in ['translations/zh.yml', 'translations/ja.yml']:
    try:
        with open(path, 'r') as f:
            chars.update(extract_cjk(f.read()))
    except FileNotFoundError:
        pass

for pattern in ['src/**/*.cpp', 'src/**/*.h', 'include/**/*.h']:
    for path in glob.glob(pattern, recursive=True):
        try:
            with open(path, 'r') as f:
                chars.update(extract_cjk(f.read()))
        except (FileNotFoundError, UnicodeDecodeError):
            pass

if chars:
    print(','.join(f'0x{ord(c):04x}' for c in sorted(chars)))
EOF
)

if [ -n "$ALL_CJKCHARS" ]; then
    ALL_CJK_COUNT=$(echo "$ALL_CJKCHARS" | tr ',' '\n' | wc -l | tr -d ' ')
    echo "Found $ALL_CJK_COUNT unique CJK characters total"

    # Runtime chars = full set (includes wizard chars — .bin fonts have everything)
    RUNTIME_CJKCHARS="$ALL_CJKCHARS"
    echo "Wizard codepoints (compiled into .c): 12"
    echo "Runtime codepoints (in .bin files): $ALL_CJK_COUNT"
else
    echo "WARNING: No CJK characters found in translations or source files"
    RUNTIME_CJKCHARS=""
fi
```

- [ ] **Step 3: Modify .c generation to use only wizard codepoints**

In each weight's generation loop, change the CJK condition from `if [ -n "$CJKCHARS" ]` to use `WIZARD_CJK`:

For **Regular** (replace the current if/else block inside the `for SIZE in $SIZES_REGULAR` loop):

```bash
    lv_font_conv \
        --font "$FONT_REGULAR" --size "$SIZE" --range "$UNICODE_RANGES" \
        --font "$FONT_CJK_SC" --size "$SIZE" --range "$WIZARD_CJK" \
        --font "$FONT_CJK_JP" --size "$SIZE" --range "$WIZARD_CJK" \
        --bpp 4 --format lvgl \
        --no-compress \
        -o "$OUTPUT"
```

Same pattern for **Light** and **Bold** loops — always include wizard CJK (no conditional needed since CJK fonts are always downloaded now).

Remove the `if [ -n "$CJKCHARS" ]; then ... else ... fi` conditionals entirely from all three weight loops.

- [ ] **Step 4: Add .bin generation section**

After the Source Code Pro section (before the final summary), add:

```bash
# Generate CJK runtime .bin fonts
if [ -n "$RUNTIME_CJKCHARS" ]; then
    mkdir -p assets/fonts/cjk

    echo ""
    echo "CJK Runtime fonts (.bin):"

    echo ""
    echo "  Regular:"
    for SIZE in $SIZES_REGULAR; do
        OUTPUT="assets/fonts/cjk/noto_sans_cjk_${SIZE}.bin"
        echo "    Generating noto_sans_cjk_${SIZE}.bin"
        lv_font_conv \
            --font "$FONT_CJK_SC" --size "$SIZE" --range "$RUNTIME_CJKCHARS" \
            --font "$FONT_CJK_JP" --size "$SIZE" --range "$RUNTIME_CJKCHARS" \
            --bpp 4 --format bin \
            --no-compress \
            -o "$OUTPUT"
    done

    echo ""
    echo "  Bold:"
    for SIZE in $SIZES_BOLD; do
        OUTPUT="assets/fonts/cjk/noto_sans_cjk_bold_${SIZE}.bin"
        echo "    Generating noto_sans_cjk_bold_${SIZE}.bin"
        lv_font_conv \
            --font "$FONT_CJK_SC" --size "$SIZE" --range "$RUNTIME_CJKCHARS" \
            --font "$FONT_CJK_JP" --size "$SIZE" --range "$RUNTIME_CJKCHARS" \
            --bpp 4 --format bin \
            --no-compress \
            -o "$OUTPUT"
    done

    echo ""
    echo "  Light:"
    for SIZE in $SIZES_LIGHT; do
        OUTPUT="assets/fonts/cjk/noto_sans_cjk_light_${SIZE}.bin"
        echo "    Generating noto_sans_cjk_light_${SIZE}.bin"
        lv_font_conv \
            --font "$FONT_CJK_SC" --size "$SIZE" --range "$RUNTIME_CJKCHARS" \
            --font "$FONT_CJK_JP" --size "$SIZE" --range "$RUNTIME_CJKCHARS" \
            --bpp 4 --format bin \
            --no-compress \
            -o "$OUTPUT"
    done

    BIN_COUNT=$(ls -1 assets/fonts/cjk/*.bin 2>/dev/null | wc -l | tr -d ' ')
    BIN_SIZE=$(du -sh assets/fonts/cjk/ 2>/dev/null | cut -f1)
    echo ""
    echo "Generated $BIN_COUNT CJK .bin files ($BIN_SIZE total)"
fi
```

- [ ] **Step 5: Run the updated regen script**

```bash
./scripts/regen_text_fonts.sh
```

Expected: 22 `.bin` files in `assets/fonts/cjk/`, `.c` files much smaller than before (only 12 CJK codepoints instead of ~1047).

- [ ] **Step 6: Verify compiled fonts have only wizard codepoints**

```bash
# Should find exactly 12 CJK codepoints in the font
python3 -c "
import re
with open('assets/fonts/noto_sans_14.c') as f:
    content = f.read()
cjk = set()
for m in re.finditer(r'0x([0-9a-fA-F]{4,5})', content):
    v = int(m.group(1), 16)
    if (0x3000 <= v <= 0x303f or 0x3040 <= v <= 0x309f or 0x30a0 <= v <= 0x30ff or
        0x3400 <= v <= 0x4dbf or 0x4e00 <= v <= 0x9fff or 0xff00 <= v <= 0xffef):
        cjk.add(v)
print(f'CJK codepoints in compiled font: {len(cjk)}')
expected = {0x3046,0x3053,0x305d,0x3088,0x4e2d,0x6587,0x65e5,0x672c,0x6b22,0x8a9e,0x8fce,0xff01}
assert cjk == expected, f'Mismatch! Got {cjk - expected} extra, missing {expected - cjk}'
print('PASS: Only wizard codepoints compiled in')
"
```

- [ ] **Step 7: Verify .bin files exist and are non-empty**

```bash
echo "Regular:"; ls -lh assets/fonts/cjk/noto_sans_cjk_{10,11,12,14,16,18,20,24,26,28}.bin
echo "Bold:"; ls -lh assets/fonts/cjk/noto_sans_cjk_bold_{14,16,18,20,24,28}.bin
echo "Light:"; ls -lh assets/fonts/cjk/noto_sans_cjk_light_{10,11,12,14,16,18}.bin
```

Expected: 22 files, each non-empty (should be 100KB-1MB+ depending on size).

- [ ] **Step 8: Commit**

```bash
git add scripts/regen_text_fonts.sh assets/fonts/*.c assets/fonts/cjk/
git commit -m "feat(i18n): split CJK fonts — 12 wizard codepoints compiled, rest as runtime .bin"
```

---

## Chunk 2: CjkFontManager — Tests First

Write the failing tests for CjkFontManager before any implementation.

### Task 2: Write CjkFontManager unit tests

**Files:**
- Create: `tests/unit/test_cjk_font_manager.cpp` (auto-discovered by Makefile wildcard)

- [ ] **Step 1: Write the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "cjk_font_manager.h"

#include "../catch_amalgamated.hpp"

using namespace helix::system;

class CjkFontManagerFixture : public LVGLTestFixture {
  public:
    CjkFontManagerFixture() : LVGLTestFixture() {}

    ~CjkFontManagerFixture() override {
        // Ensure clean state between tests
        CjkFontManager::instance().shutdown();
    }
};

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: not loaded by default", "[cjk_font]") {
    REQUIRE_FALSE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: English does not load CJK", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("en");
    REQUIRE_FALSE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: German does not load CJK", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("de");
    REQUIRE_FALSE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: Chinese loads CJK", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("zh");
    REQUIRE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: Japanese loads CJK", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("ja");
    REQUIRE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: switching zh to en unloads", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("zh");
    REQUIRE(CjkFontManager::instance().is_loaded());

    CjkFontManager::instance().on_language_changed("en");
    REQUIRE_FALSE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: double load is idempotent", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("zh");
    REQUIRE(CjkFontManager::instance().is_loaded());

    // Should not crash or double-load
    CjkFontManager::instance().on_language_changed("zh");
    REQUIRE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: switching ja to zh stays loaded", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("ja");
    REQUIRE(CjkFontManager::instance().is_loaded());

    // Both are CJK — should stay loaded (idempotent)
    CjkFontManager::instance().on_language_changed("zh");
    REQUIRE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: shutdown cleans up", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("zh");
    REQUIRE(CjkFontManager::instance().is_loaded());

    CjkFontManager::instance().shutdown();
    REQUIRE_FALSE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: shutdown when not loaded is safe", "[cjk_font]") {
    REQUIRE_FALSE(CjkFontManager::instance().is_loaded());
    CjkFontManager::instance().shutdown();  // Should not crash
    REQUIRE_FALSE(CjkFontManager::instance().is_loaded());
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: load sets fallback on compiled fonts", "[cjk_font]") {
    // Before loading, compiled font should have no fallback (or default)
    const lv_font_t* pre_fallback = noto_sans_14.fallback;

    CjkFontManager::instance().on_language_changed("zh");

    // After loading, compiled font should have a non-null fallback
    REQUIRE(noto_sans_14.fallback != nullptr);
    REQUIRE(noto_sans_14.fallback != pre_fallback);
}

TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: unload clears fallback on compiled fonts", "[cjk_font]") {
    CjkFontManager::instance().on_language_changed("zh");
    REQUIRE(noto_sans_14.fallback != nullptr);

    CjkFontManager::instance().on_language_changed("en");
    // Fallback should be cleared (or restored to original)
    REQUIRE(noto_sans_14.fallback == nullptr);
}
```

- [ ] **Step 2: Build tests — verify they fail**

```bash
make test 2>&1 | tail -20
```

Expected: Compilation fails because `cjk_font_manager.h` doesn't exist yet. This confirms the tests are wired up correctly.

- [ ] **Step 3: Commit failing tests**

```bash
git add tests/unit/test_cjk_font_manager.cpp
git commit -m "test(i18n): add failing tests for CjkFontManager"
```

---

## Chunk 3: CjkFontManager — Implementation

Implement the CjkFontManager to make the tests pass.

### Task 3: Create CjkFontManager header

**Files:**
- Create: `include/cjk_font_manager.h`

- [ ] **Step 1: Write the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

#include <string>
#include <vector>

namespace helix::system {

/**
 * Manages runtime loading of CJK font files (.bin) as fallbacks on compiled Latin fonts.
 *
 * When a CJK locale is active (zh, ja), loads .bin font files via lv_binfont_create()
 * and sets them as fallback on compiled fonts. When switching away from CJK, unloads
 * and clears fallbacks. The 12 wizard welcome codepoints are always compiled in.
 */
class CjkFontManager {
  public:
    static CjkFontManager& instance();

    /**
     * React to language change. Loads CJK fonts if locale needs them, unloads if not.
     * Idempotent: calling with same locale category (CJK vs non-CJK) is a no-op.
     */
    void on_language_changed(const std::string& lang);

    /** Whether CJK runtime fonts are currently loaded. */
    bool is_loaded() const { return loaded_; }

    /** Cleanup before lv_deinit(). Safe to call when not loaded. */
    void shutdown();

  private:
    CjkFontManager() = default;

    static bool needs_cjk(const std::string& lang);
    bool load();
    void unload();

    static void set_fallback(const lv_font_t* compiled, lv_font_t* cjk);
    static void clear_fallback(const lv_font_t* compiled);

    struct FontEntry {
        const lv_font_t* compiled_font;
        lv_font_t* cjk_font;
    };

    std::vector<FontEntry> loaded_fonts_;
    std::string current_lang_;
    bool loaded_ = false;
};

}  // namespace helix::system
```

- [ ] **Step 2: Verify tests still fail to compile (missing .cpp)**

```bash
make test 2>&1 | grep -i "undefined reference\|error" | head -5
```

Expected: Linker errors for `CjkFontManager::instance()` etc.

### Task 4: Implement CjkFontManager

**Files:**
- Create: `src/system/cjk_font_manager.cpp`

- [ ] **Step 1: Write the implementation**

Read the following files first to understand patterns:
- `include/ui_fonts.h` — font symbol names
- `src/application/asset_manager.cpp` — how fonts map to symbols

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cjk_font_manager.h"
#include "ui_fonts.h"

#include "spdlog/spdlog.h"

namespace helix::system {

// Font mapping table: compiled font symbol → .bin filename
// Must match the sizes in regen_text_fonts.sh
struct FontMapping {
    const lv_font_t* compiled;
    const char* bin_filename;
};

// clang-format off
static const FontMapping REGULAR_FONTS[] = {
    {&noto_sans_10, "noto_sans_cjk_10.bin"},
    {&noto_sans_11, "noto_sans_cjk_11.bin"},
    {&noto_sans_12, "noto_sans_cjk_12.bin"},
    {&noto_sans_14, "noto_sans_cjk_14.bin"},
    {&noto_sans_16, "noto_sans_cjk_16.bin"},
    {&noto_sans_18, "noto_sans_cjk_18.bin"},
    {&noto_sans_20, "noto_sans_cjk_20.bin"},
    {&noto_sans_24, "noto_sans_cjk_24.bin"},
    {&noto_sans_26, "noto_sans_cjk_26.bin"},
    {&noto_sans_28, "noto_sans_cjk_28.bin"},
};

static const FontMapping BOLD_FONTS[] = {
    {&noto_sans_bold_14, "noto_sans_cjk_bold_14.bin"},
    {&noto_sans_bold_16, "noto_sans_cjk_bold_16.bin"},
    {&noto_sans_bold_18, "noto_sans_cjk_bold_18.bin"},
    {&noto_sans_bold_20, "noto_sans_cjk_bold_20.bin"},
    {&noto_sans_bold_24, "noto_sans_cjk_bold_24.bin"},
    {&noto_sans_bold_28, "noto_sans_cjk_bold_28.bin"},
};

static const FontMapping LIGHT_FONTS[] = {
    {&noto_sans_light_10, "noto_sans_cjk_light_10.bin"},
    {&noto_sans_light_11, "noto_sans_cjk_light_11.bin"},
    {&noto_sans_light_12, "noto_sans_cjk_light_12.bin"},
    {&noto_sans_light_14, "noto_sans_cjk_light_14.bin"},
    {&noto_sans_light_16, "noto_sans_cjk_light_16.bin"},
    {&noto_sans_light_18, "noto_sans_cjk_light_18.bin"},
};
// clang-format on

static constexpr const char* CJK_FONT_DIR = "A:assets/fonts/cjk/";

CjkFontManager& CjkFontManager::instance() {
    static CjkFontManager s_instance;
    return s_instance;
}

bool CjkFontManager::needs_cjk(const std::string& lang) {
    return lang == "zh" || lang == "ja";
}

void CjkFontManager::on_language_changed(const std::string& lang) {
    if (needs_cjk(lang)) {
        if (!loaded_) {
            if (!load()) {
                spdlog::warn("[CjkFontManager] Failed to load CJK fonts for '{}'", lang);
            }
        }
        current_lang_ = lang;
    } else {
        if (loaded_) {
            unload();
        }
        current_lang_.clear();
    }
}

void CjkFontManager::set_fallback(const lv_font_t* compiled, lv_font_t* cjk) {
    const_cast<lv_font_t*>(compiled)->fallback = cjk;
}

void CjkFontManager::clear_fallback(const lv_font_t* compiled) {
    const_cast<lv_font_t*>(compiled)->fallback = nullptr;
}

bool CjkFontManager::load() {
    spdlog::info("[CjkFontManager] Loading CJK runtime fonts...");

    auto load_set = [this](const FontMapping* mappings, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            std::string path = std::string(CJK_FONT_DIR) + mappings[i].bin_filename;
            lv_font_t* cjk = lv_binfont_create(path.c_str());
            if (cjk) {
                set_fallback(mappings[i].compiled, cjk);
                loaded_fonts_.push_back({mappings[i].compiled, cjk});
            } else {
                spdlog::warn("[CjkFontManager] Failed to load: {}", path);
            }
        }
    };

    load_set(REGULAR_FONTS, std::size(REGULAR_FONTS));
    load_set(BOLD_FONTS, std::size(BOLD_FONTS));
    load_set(LIGHT_FONTS, std::size(LIGHT_FONTS));

    if (loaded_fonts_.empty()) {
        spdlog::error("[CjkFontManager] No CJK fonts loaded — check assets/fonts/cjk/");
        return false;
    }

    loaded_ = true;
    spdlog::info("[CjkFontManager] Loaded {} CJK fallback fonts", loaded_fonts_.size());
    return true;
}

void CjkFontManager::unload() {
    if (!loaded_) return;

    spdlog::info("[CjkFontManager] Unloading {} CJK fonts", loaded_fonts_.size());

    for (auto& entry : loaded_fonts_) {
        clear_fallback(entry.compiled_font);
        lv_binfont_destroy(entry.cjk_font);
    }
    loaded_fonts_.clear();
    loaded_ = false;
    current_lang_.clear();
}

void CjkFontManager::shutdown() {
    unload();
}

}  // namespace helix::system
```

- [ ] **Step 2: Build and run tests**

```bash
make test && ./build/bin/helix-tests "[cjk_font]" -v
```

Expected: All tests pass. If `.bin` files don't exist in the test working directory, the `load()` tests will fail — ensure the test runner's working directory has access to `assets/fonts/cjk/`.

- [ ] **Step 3: Fix any test failures**

The test for `load sets fallback on compiled fonts` requires `.bin` files to exist relative to the test working directory. If tests run from the project root, this should work. If not, check and fix the path or skip the load/fallback tests with a file-existence guard.

- [ ] **Step 4: Run all tests to check for regressions**

```bash
make test-run 2>&1 | tail -20
```

Expected: All existing tests still pass + new `[cjk_font]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/cjk_font_manager.h src/system/cjk_font_manager.cpp
git commit -m "feat(i18n): implement CjkFontManager for runtime CJK font loading"
```

**CODE REVIEW CHECKPOINT:** Review CjkFontManager implementation against spec. Check: const_cast safety, fallback pointer management, error handling on missing files, singleton lifecycle.

---

## Chunk 4: Integration — Hook CjkFontManager into App Lifecycle

Wire CjkFontManager into language change, startup, and shutdown paths.

### Task 5: Write integration tests

**Files:**
- Modify: `tests/unit/test_cjk_font_manager.cpp`

- [ ] **Step 1: Add integration-style test for language change flow**

Add to the existing test file:

```cpp
TEST_CASE_METHOD(CjkFontManagerFixture,
                 "CjkFontManager: full lifecycle — load, switch, unload, shutdown",
                 "[cjk_font][integration]") {
    auto& mgr = CjkFontManager::instance();

    // Start with English
    mgr.on_language_changed("en");
    REQUIRE_FALSE(mgr.is_loaded());

    // Switch to Chinese
    mgr.on_language_changed("zh");
    REQUIRE(mgr.is_loaded());

    // Switch to Japanese (should stay loaded — both CJK)
    mgr.on_language_changed("ja");
    REQUIRE(mgr.is_loaded());

    // Switch to French
    mgr.on_language_changed("fr");
    REQUIRE_FALSE(mgr.is_loaded());

    // Back to Chinese
    mgr.on_language_changed("zh");
    REQUIRE(mgr.is_loaded());

    // Shutdown
    mgr.shutdown();
    REQUIRE_FALSE(mgr.is_loaded());

    // Double shutdown is safe
    mgr.shutdown();
    REQUIRE_FALSE(mgr.is_loaded());
}
```

- [ ] **Step 2: Build and run to confirm test passes**

```bash
make test && ./build/bin/helix-tests "[cjk_font][integration]" -v
```

### Task 6: Hook into SystemSettingsManager

**Files:**
- Modify: `src/system/system_settings_manager.cpp`

- [ ] **Step 1: Read the current file around the hook point**

Read `src/system/system_settings_manager.cpp` lines 97-121.

- [ ] **Step 2: Add include and hook**

Add at top of file:
```cpp
#include "cjk_font_manager.h"
```

In `set_language()`, after `lv_translation_set_language(lang.c_str());` (line ~106), add:
```cpp
    // Load/unload CJK runtime fonts based on locale
    CjkFontManager::instance().on_language_changed(lang);
```

- [ ] **Step 3: Build to verify compilation**

```bash
make -j 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add src/system/system_settings_manager.cpp
git commit -m "feat(i18n): hook CjkFontManager into language change"
```

### Task 7: Hook into Application startup and shutdown

**Files:**
- Modify: `src/application/application.cpp`

- [ ] **Step 1: Read the hook points**

Read `src/application/application.cpp` around lines 1115-1120 (init_translations) and lines 3075-3085 (shutdown).

- [ ] **Step 2: Add include**

Add at top of file:
```cpp
#include "cjk_font_manager.h"
```

- [ ] **Step 3: Add startup hook**

In `init_translations()`, after `lv_i18n_set_locale(lang.c_str());`, add:
```cpp
    // Load CJK runtime fonts if persisted language is CJK
    helix::system::CjkFontManager::instance().on_language_changed(lang);
```

- [ ] **Step 4: Add shutdown hook**

In `shutdown()`, after `StaticSubjectRegistry::instance().deinit_all();` and before `theme_manager_deinit();`, add:
```cpp
    // Destroy runtime CJK fonts before LVGL shutdown
    helix::system::CjkFontManager::instance().shutdown();
```

- [ ] **Step 5: Build and run all tests**

```bash
make -j && make test-run 2>&1 | tail -10
```

- [ ] **Step 6: Commit**

```bash
git add src/application/application.cpp
git commit -m "feat(i18n): hook CjkFontManager into startup and shutdown"
```

**CODE REVIEW CHECKPOINT:** Review all integration hooks. Verify: startup ordering (after fs init, before UI creation), shutdown ordering (after subjects, before lv_deinit), unconditional call pattern in set_language.

---

## Chunk 5: Build System & Packaging

Update Makefile targets and ensure `.bin` files are included in releases.

### Task 8: Update mk/fonts.mk

**Files:**
- Modify: `mk/fonts.mk`

- [ ] **Step 1: Read current fonts.mk**

Read `mk/fonts.mk` to understand existing targets.

- [ ] **Step 2: Verify no Makefile changes needed**

The `regen-text-fonts` target already runs `regen_text_fonts.sh`, which now generates both `.c` and `.bin` files. The `$(TEXT_FONT_STAMP)` auto-regen stamp also calls the same script. No new Makefile targets needed.

- [ ] **Step 3: Verify release packaging includes .bin files**

Check `scripts/package.sh` — the `rsync -a ... "${PROJECT_DIR}/assets/" "$pkg_dir/assets/"` at line ~130 copies the entire `assets/` directory, so `assets/fonts/cjk/` is already included. Confirm this with: `grep -n 'assets' scripts/package.sh`.

- [ ] **Step 4: Verify .bin files are tracked by git**

`.gitignore` only ignores `assets/fonts/NotoSansCJK*.otf` (source fonts). There is no rule for `assets/fonts/cjk/`, so `.bin` files are tracked. Confirm: `git check-ignore assets/fonts/cjk/noto_sans_cjk_14.bin` should return nothing.

- [ ] **Step 5: Build and verify**

```bash
make -j 2>&1 | tail -5
```

- [ ] **Step 6: Commit**

```bash
git add mk/fonts.mk
git commit -m "chore(build): add CJK .bin font targets to Makefile"
```

---

## Chunk 6: Manual Verification & Final Commit

### Task 9: Manual testing

- [ ] **Step 1: Run with English and check logs for no CJK loading**

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/test.log
```

Run in background. Tell user: navigate to wizard, verify "欢迎！" and "中文" / "日本語" render on the language chooser page. Then switch language to English and navigate around — check logs show no CJK font loading.

- [ ] **Step 2: Read logs and verify**

```
Read /tmp/test.log
```

Check for: no `[CjkFontManager] Loading` messages when English is active.

- [ ] **Step 3: Run with Chinese locale and verify CJK text renders**

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/test-zh.log
```

Run in background. Tell user: set language to Chinese in wizard or settings, navigate panels, verify Chinese text renders everywhere.

- [ ] **Step 4: Read logs and verify CJK loaded**

```
Read /tmp/test-zh.log
```

Check for: `[CjkFontManager] Loaded 22 CJK fallback fonts` (or close to 22).

- [ ] **Step 5: Run full test suite one final time**

```bash
make test-run 2>&1 | tail -20
```

Expected: All tests pass including `[cjk_font]` tests.

**CODE REVIEW CHECKPOINT:** Final review — verify the complete feature against the spec. Check: memory savings (compare binary size before/after), all hook points correct, no regressions in non-CJK rendering, wizard welcome page works.

### Task 10: Final commit with all generated files

- [ ] **Step 1: Stage and commit any remaining generated files**

```bash
git add -A assets/fonts/cjk/ mk/fonts.mk
git status
git commit -m "feat(i18n): runtime CJK font loading — zero cost for non-CJK users"
```
