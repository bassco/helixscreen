#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Check if CJK text fonts need regeneration after translation changes.
# Called automatically after translation-sync.
#
# Compares the set of CJK characters in current translations against
# what's compiled into the font files. If new characters are found,
# warns the user to run 'make regen-text-fonts'.

set -e
cd "$(dirname "$0")/.."

CJK_FONT_SC=assets/fonts/NotoSansCJKsc-Regular.otf
CJK_FONT_JP=assets/fonts/NotoSansCJKjp-Regular.otf

# Skip check if CJK source fonts aren't available
if [ ! -f "$CJK_FONT_SC" ] || [ ! -f "$CJK_FONT_JP" ]; then
    exit 0
fi

# Extract current CJK characters needed from translations + sources
NEEDED=$(python3 << 'PYEOF'
import glob
import re

chars = set()
CJK_RANGES = [
    r'[\u3000-\u303f]', r'[\u3040-\u309f]', r'[\u30a0-\u30ff]',
    r'[\u3400-\u4dbf]', r'[\u4e00-\u9fff]', r'[\uff00-\uffef]',
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

for c in sorted(chars):
    print(f'0x{ord(c):04x}')
PYEOF
)

NEEDED_COUNT=$(echo "$NEEDED" | wc -l | tr -d ' ')

# Check what's compiled into fonts by looking at a representative font file
# The lv_font_conv output includes a comment with the glyph ranges
FONT_FILE=assets/fonts/noto_sans_14.c
if [ ! -f "$FONT_FILE" ]; then
    echo "⚠ Text font file not found - run 'make regen-text-fonts'"
    exit 0
fi

# Extract compiled CJK codepoints from the font C file
# lv_font_conv embeds the ranges in the glyph_id_ofs_list and unicode_list arrays
COMPILED=$(python3 << PYEOF
import re

with open('$FONT_FILE', 'r') as f:
    content = f.read()

# Find all unicode values in the unicode_list arrays
# Format: 0x4e00, 0x4e09, etc.
codepoints = set()
for match in re.finditer(r'0x([0-9a-fA-F]{4,5})', content):
    val = int(match.group(1), 16)
    # Only count CJK range codepoints
    if (0x3000 <= val <= 0x303f or 0x3040 <= val <= 0x309f or
        0x30a0 <= val <= 0x30ff or 0x3400 <= val <= 0x4dbf or
        0x4e00 <= val <= 0x9fff or 0xff00 <= val <= 0xffef):
        codepoints.add(val)

for cp in sorted(codepoints):
    print(f'0x{cp:04x}')
PYEOF
)

COMPILED_COUNT=$(echo "$COMPILED" | wc -l | tr -d ' ')

# Find characters needed but not compiled
MISSING=$(comm -23 <(echo "$NEEDED" | sort) <(echo "$COMPILED" | sort))
MISSING_COUNT=0
if [ -n "$MISSING" ]; then
    MISSING_COUNT=$(echo "$MISSING" | wc -l | tr -d ' ')
fi

if [ "$MISSING_COUNT" -gt 0 ]; then
    echo ""
    echo "⚠ $MISSING_COUNT new CJK characters found in translations that aren't in fonts."
    echo "  Run 'make regen-text-fonts' to include them, then rebuild."
    echo ""
fi
