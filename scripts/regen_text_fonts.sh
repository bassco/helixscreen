#!/bin/bash
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Regenerate Noto Sans text fonts for LVGL
#
# This script generates LVGL font files for UI text rendering.
# Includes:
# - Latin (ASCII, Western European, Central European)
# - Cyrillic (Russian, Ukrainian, etc.)
# - CJK subset (Chinese + Japanese from translation files)
#
# Source fonts:
# - assets/fonts/NotoSans-*.ttf (Latin/Cyrillic)
# - assets/fonts/NotoSansCJKsc-Regular.otf (Chinese)
# - assets/fonts/NotoSansCJKjp-Regular.otf (Japanese)

set -e
cd "$(dirname "$0")/.."

# Add node_modules/.bin to PATH for lv_font_conv
export PATH="$PWD/node_modules/.bin:$PATH"

# Font source files
FONT_REGULAR=assets/fonts/NotoSans-Regular.ttf
FONT_LIGHT=assets/fonts/NotoSans-Light.ttf
FONT_BOLD=assets/fonts/NotoSans-Bold.ttf
FONT_CJK_SC=assets/fonts/NotoSansCJKsc-Regular.otf
FONT_CJK_JP=assets/fonts/NotoSansCJKjp-Regular.otf

# Check Latin fonts exist
for FONT in "$FONT_REGULAR" "$FONT_LIGHT" "$FONT_BOLD"; do
    if [ ! -f "$FONT" ]; then
        echo "ERROR: Font not found: $FONT"
        echo "Download Noto Sans from: https://fonts.google.com/noto/specimen/Noto+Sans"
        exit 1
    fi
done

# Check if CJK fonts exist
CJK_AVAILABLE=false
if [ -f "$FONT_CJK_SC" ] && [ -f "$FONT_CJK_JP" ]; then
    CJK_AVAILABLE=true
    echo "CJK fonts found - will include Chinese and Japanese support"
else
    echo "CJK fonts not found - generating Latin-only fonts"
    echo "To add CJK support, download Noto Sans CJK from GitHub"
fi

# Unicode ranges for Latin/Cyrillic
UNICODE_RANGES=""
UNICODE_RANGES+="0x20-0x7F"      # Basic Latin (ASCII)
UNICODE_RANGES+=",0xA0-0xFF"     # Latin-1 Supplement (Western European)
UNICODE_RANGES+=",0x100-0x17F"   # Latin Extended-A (Central European)
UNICODE_RANGES+=",0x400-0x4FF"   # Cyrillic (Russian, Ukrainian, etc.)
UNICODE_RANGES+=",0x2013-0x2014" # En/Em dashes
UNICODE_RANGES+=",0x2018-0x201D" # Smart quotes
UNICODE_RANGES+=",0x2022"        # Bullet
UNICODE_RANGES+=",0x2026"        # Ellipsis
UNICODE_RANGES+=",0x20AC"        # Euro sign
UNICODE_RANGES+=",0x2122"        # Trademark

# Extract CJK characters from translations and C++ sources if fonts available
CJKCHARS=""
if [ "$CJK_AVAILABLE" = true ]; then
    echo "Extracting CJK characters from translations and C++ sources..."
    CJKCHARS=$(python3 << 'EOF'
import glob
import re

chars = set()

# CJK Unicode ranges to scan for
CJK_RANGES = [
    r'[\u3000-\u303f]',   # CJK Symbols and Punctuation
    r'[\u3040-\u309f]',   # Hiragana
    r'[\u30a0-\u30ff]',   # Katakana
    r'[\u3400-\u4dbf]',   # CJK Unified Ideographs Extension A
    r'[\u4e00-\u9fff]',   # CJK Unified Ideographs
    r'[\uff00-\uffef]',   # Halfwidth and Fullwidth Forms
]

def extract_cjk(content):
    """Extract all CJK characters from a string."""
    found = set()
    for pattern in CJK_RANGES:
        found.update(re.findall(pattern, content))
    return found

# Translation files
for path in ['translations/zh.yml', 'translations/ja.yml']:
    try:
        with open(path, 'r') as f:
            chars.update(extract_cjk(f.read()))
    except FileNotFoundError:
        pass

# C++ source files (catches hardcoded CJK strings like welcome text)
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
    if [ -n "$CJKCHARS" ]; then
        CJK_COUNT=$(echo "$CJKCHARS" | tr ',' '\n' | wc -l | tr -d ' ')
        echo "Found $CJK_COUNT unique CJK characters"
    fi
fi

# Font sizes
SIZES_REGULAR="10 11 12 14 16 18 20 24 26 28"
SIZES_LIGHT="10 11 12 14 16 18"
SIZES_BOLD="14 16 18 20 24 28"

echo ""
echo "Regenerating Noto Sans text fonts for LVGL..."

# Generate Regular weight
echo ""
echo "Regular weight:"
for SIZE in $SIZES_REGULAR; do
    OUTPUT="assets/fonts/noto_sans_${SIZE}.c"
    echo "  Generating noto_sans_${SIZE} -> $OUTPUT"

    if [ "$CJK_AVAILABLE" = true ] && [ -n "$CJKCHARS" ]; then
        lv_font_conv \
            --font "$FONT_REGULAR" --size "$SIZE" --range "$UNICODE_RANGES" \
            --font "$FONT_CJK_SC" --size "$SIZE" --range "$CJKCHARS" \
            --font "$FONT_CJK_JP" --size "$SIZE" --range "$CJKCHARS" \
            --bpp 4 --format lvgl \
            --no-compress \
            -o "$OUTPUT"
    else
        lv_font_conv \
            --font "$FONT_REGULAR" --size "$SIZE" --bpp 4 --format lvgl \
            --range "$UNICODE_RANGES" \
            --no-compress \
            -o "$OUTPUT"
    fi
done

# Generate Light weight
echo ""
echo "Light weight:"
for SIZE in $SIZES_LIGHT; do
    OUTPUT="assets/fonts/noto_sans_light_${SIZE}.c"
    echo "  Generating noto_sans_light_${SIZE} -> $OUTPUT"

    if [ "$CJK_AVAILABLE" = true ] && [ -n "$CJKCHARS" ]; then
        lv_font_conv \
            --font "$FONT_LIGHT" --size "$SIZE" --range "$UNICODE_RANGES" \
            --font "$FONT_CJK_SC" --size "$SIZE" --range "$CJKCHARS" \
            --font "$FONT_CJK_JP" --size "$SIZE" --range "$CJKCHARS" \
            --bpp 4 --format lvgl \
            --no-compress \
            -o "$OUTPUT"
    else
        lv_font_conv \
            --font "$FONT_LIGHT" --size "$SIZE" --bpp 4 --format lvgl \
            --range "$UNICODE_RANGES" \
            --no-compress \
            -o "$OUTPUT"
    fi
done

# Generate Bold weight
echo ""
echo "Bold weight:"
for SIZE in $SIZES_BOLD; do
    OUTPUT="assets/fonts/noto_sans_bold_${SIZE}.c"
    echo "  Generating noto_sans_bold_${SIZE} -> $OUTPUT"

    if [ "$CJK_AVAILABLE" = true ] && [ -n "$CJKCHARS" ]; then
        lv_font_conv \
            --font "$FONT_BOLD" --size "$SIZE" --range "$UNICODE_RANGES" \
            --font "$FONT_CJK_SC" --size "$SIZE" --range "$CJKCHARS" \
            --font "$FONT_CJK_JP" --size "$SIZE" --range "$CJKCHARS" \
            --bpp 4 --format lvgl \
            --no-compress \
            -o "$OUTPUT"
    else
        lv_font_conv \
            --font "$FONT_BOLD" --size "$SIZE" --bpp 4 --format lvgl \
            --range "$UNICODE_RANGES" \
            --no-compress \
            -o "$OUTPUT"
    fi
done

# Generate Source Code Pro Monospace (for console/terminal)
FONT_MONO=assets/fonts/SourceCodePro-Regular.ttf
SIZES_MONO="10 12 14 16"

if [ -f "$FONT_MONO" ]; then
    echo ""
    echo "Source Code Pro (Monospace):"
    for SIZE in $SIZES_MONO; do
        OUTPUT="assets/fonts/source_code_pro_${SIZE}.c"
        echo "  Generating source_code_pro_${SIZE} -> $OUTPUT"

        lv_font_conv \
            --font "$FONT_MONO" --size "$SIZE" --bpp 4 --format lvgl \
            --range "$UNICODE_RANGES" \
            --no-compress \
            -o "$OUTPUT"
    done
else
    echo ""
    echo "Source Code Pro not found ($FONT_MONO) - skipping monospace fonts"
    echo "Download from: https://github.com/adobe-fonts/source-code-pro/tree/release/TTF"
fi

echo ""
echo "Done! Generated text fonts with extended Unicode support."
echo ""
echo "Supported character sets:"
echo "  - ASCII (0x20-0x7F)"
echo "  - Western European: é, è, ê, ñ, ü, ö, ß, etc."
echo "  - Central European: ą, ę, ł, ő, etc."
echo "  - Cyrillic: А-Яа-я (Russian, Ukrainian, etc.)"
if [ "$CJK_AVAILABLE" = true ]; then
    echo "  - Chinese: Simplified Chinese characters (from translations + C++ sources)"
    echo "  - Japanese: Hiragana, Katakana, Kanji (from translations + C++ sources)"
fi
echo ""
echo "Rebuild the project: make -j"
