#!/usr/bin/env python3
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: Moonraker subscription handlers must not use .value() / .get<T>()
# on subscribed-status JSON without explicit type guards.
#
# Background: Moonraker delivers null for subscribed fields when the underlying
# Klipper object lacks them (e.g. Snapmaker U1's filament_motion_sensor reports
# no detection_count). nlohmann::json::value("k", default) and .get<T>() throw
# json::type_error::302 on null. An uncaught throw inside a subscription
# handler unwinds out of run() into main()'s top-level catch, exiting 134 and
# triggering a watchdog crash loop the user can't break out of (#filament_motion_sensor,
# fixed in f75b961d8).
#
# Documented in memory: feedback_moonraker_subscribed_null.md
#
# Approved patterns:
#   if (auto it = obj.find("k"); it != obj.end() && it->is_<type>()) {
#       v = it->get<T>();
#   }
#   v = helix::json::safe_int(obj, "k", default);   // (or safe_float/string/bool)
#
# Banned in subscription handlers (without // JSON_NULL_SAFE comment):
#   obj.value("k", default)                          // throws on null
#   obj["k"].get<T>()                                // throws on null/missing
#   if (obj.contains("k")) v = obj.value("k", ...);  // contains() returns true for null
#
# Per-line opt-out:
#   v = obj.value("foo", 0); // JSON_NULL_SAFE: caller already type-guarded
#
# Usage:
#   ./scripts/check_subscription_null_safety.py [files...]
#   ./scripts/check_subscription_null_safety.py --staged-only
#   (no args = scan src/ recursively)

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable

HANDLER_RE = re.compile(
    r'(?:^|\s)(?:[\w:<>~]+\s+)+'
    r'(?P<qual>[\w:]*::)?'
    r'(?P<name>update_from_status|update_from_subscription|update_from_notification'
    r'|update_from_backend|handle_subscription|apply_status_snapshot'
    r'|on_status_update|process_subscription_update)'
    r'\s*\([^;{]*\)\s*(?:const)?\s*(?:noexcept)?\s*\{',
    re.MULTILINE,
)

# nlohmann::json::value(key, default) takes a string-literal first arg.
# std::optional<T>::value() takes no args — exclude that to avoid false
# positives (e.g. tool_state.cpp `extruder_name.value() == ...`).
VALUE_CALL_RE = re.compile(r'\.value\s*\(\s*"')
GET_TYPE_RE = re.compile(r'\.get\s*<\s*(?:int|float|double|bool|std::string|long|short|uint\w*|int\w*)\s*>\s*\(')
# A guard on the same line or a recent enclosing line — any of these is sufficient
# to prove the .get<T>() can't fire on a null. .value() is never proven safe by
# this guard alone (it still throws on null even when the key is present), so
# .value() always needs the explicit JSON_NULL_SAFE opt-out.
IS_TYPE_GUARD_RE = re.compile(
    r'\.is_(?:number(?:_integer|_unsigned|_float)?|string|boolean|object|array|null)\s*\('
)
OPT_OUT = "JSON_NULL_SAFE"
GUARD_LOOKBACK = 15  # lines — covers typical inline-block sizes between
                     # the `is_<type>()` guard and the `.get<T>()` call.


def find_handler_bodies(text: str) -> list[tuple[str, int, int]]:
    """Return [(handler_name, body_start_line, body_end_line)]."""
    out: list[tuple[str, int, int]] = []
    for m in HANDLER_RE.finditer(text):
        # Brace match starting at the opening {
        open_pos = m.end() - 1
        depth = 0
        end = -1
        for i in range(open_pos, len(text)):
            c = text[i]
            if c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    end = i
                    break
        if end < 0:
            continue
        start_line = text.count('\n', 0, open_pos) + 1
        end_line = text.count('\n', 0, end) + 1
        out.append((m.group('name'), start_line, end_line))
    return out


def scan_file(path: Path) -> list[str]:
    """Return list of violation strings for this file."""
    try:
        text = path.read_text()
    except Exception:
        return []
    violations: list[str] = []
    bodies = find_handler_bodies(text)
    if not bodies:
        return []
    lines = text.splitlines()

    def has_recent_is_guard(ln: int) -> bool:
        """True if any of the last GUARD_LOOKBACK lines (incl. ln) contains
        an .is_<type>() check or a JSON_NULL_SAFE opt-out comment."""
        lo_g = max(1, ln - GUARD_LOOKBACK)
        for g in range(lo_g, ln + 1):
            if g <= 0 or g > len(lines):
                continue
            l = lines[g - 1]
            if OPT_OUT in l:
                return True
            if IS_TYPE_GUARD_RE.search(l):
                return True
        return False

    for handler, lo, hi in bodies:
        for ln in range(lo, hi + 1):
            if ln <= 0 or ln > len(lines):
                continue
            line = lines[ln - 1]
            if OPT_OUT in line:
                continue
            stripped = line.lstrip()
            if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                continue
            if VALUE_CALL_RE.search(line):
                # .value() is never safe via is_<type>() alone — even with a
                # type guard, a null delivery in a sibling field can flip the
                # type unexpectedly between the guard and the call. Always flag.
                violations.append(
                    f"{path}:{ln}: {handler}() uses .value() — throws on null. "
                    f"Use find()+is_<type>() or helix::json::safe_*. "
                    f"(suppress with `// JSON_NULL_SAFE: <reason>`)"
                )
            elif GET_TYPE_RE.search(line):
                if has_recent_is_guard(ln):
                    continue
                violations.append(
                    f"{path}:{ln}: {handler}() uses .get<T>() without an .is_<type>() guard. "
                    f"Use find()+is_<type>() or helix::json::safe_*. "
                    f"(suppress with `// JSON_NULL_SAFE: <reason>`)"
                )
    return violations


def collect_files(args: argparse.Namespace) -> Iterable[Path]:
    if args.staged_only:
        out = subprocess.run(
            ['git', 'diff', '--cached', '--name-only', '--diff-filter=ACM'],
            capture_output=True, text=True, check=False,
        )
        for line in out.stdout.splitlines():
            if line.endswith('.cpp') and Path(line).exists():
                yield Path(line)
        return
    if args.files:
        for f in args.files:
            p = Path(f)
            if p.suffix == '.cpp' and p.exists():
                yield p
        return
    for p in Path('src').rglob('*.cpp'):
        yield p


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('files', nargs='*', help='Files to check (default: scan src/)')
    ap.add_argument('--staged-only', action='store_true', help='Check only staged files')
    ap.add_argument('--max-allowed', type=int, default=None,
                    help='Pass if total violations ≤ N (ratcheting baseline). Default: fail on any.')
    ap.add_argument('--summary', action='store_true',
                    help='Print only summary count, not per-line violations')
    args = ap.parse_args()

    repo_root = subprocess.run(
        ['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True, check=False,
    ).stdout.strip()
    if repo_root:
        import os
        os.chdir(repo_root)

    all_violations: list[str] = []
    for path in collect_files(args):
        all_violations.extend(scan_file(path))

    count = len(all_violations)

    # Baseline-mode: pre-existing violations are tolerated up to a ceiling that
    # ratchets down. New code that pushes count above the ceiling fails CI.
    if args.max_allowed is not None:
        if count > args.max_allowed:
            print(f"❌ Subscription null-safety: {count} violations exceeds baseline ({args.max_allowed}).")
            if not args.summary:
                # Show only the *new* violations isn't trivial without diff context;
                # show all so the dev can spot which file they touched.
                for v in all_violations:
                    print(f"   {v}")
            print(f"   New code introduced {count - args.max_allowed} violation(s).")
            print("   Background: feedback_moonraker_subscribed_null.md / f75b961d8.")
            print("   Fix pattern:")
            print("     auto it = obj.find(\"k\");")
            print("     if (it != obj.end() && it->is_number_integer()) v = it->get<int>();")
            print("   Or use helix::json::safe_int / safe_float / safe_bool / safe_string.")
            print("   Suppress per-line: `// JSON_NULL_SAFE: <reason>`")
            return 1
        if count < args.max_allowed:
            print(f"✅ Subscription null-safety: {count} (baseline {args.max_allowed} — please ratchet down)")
        else:
            print(f"✅ Subscription null-safety: {count} == baseline ({args.max_allowed})")
        return 0

    if all_violations:
        print("❌ Subscription null-safety violations:")
        for v in all_violations:
            print(f"   {v}")
        print()
        print(f"   {count} violation(s) found.")
        print("   Background: feedback_moonraker_subscribed_null.md / f75b961d8.")
        print("   Fix pattern:")
        print("     auto it = obj.find(\"k\");")
        print("     if (it != obj.end() && it->is_number_integer()) v = it->get<int>();")
        print("   Or use helix::json::safe_int / safe_float / safe_bool / safe_string.")
        return 1

    print("✅ Subscription handlers null-safe")
    return 0


if __name__ == '__main__':
    sys.exit(main())
