# Snapmaker U1 Support Improvements

**Date**: 2026-04-01
**Status**: Approved
**Scope**: Detection heuristics, printer DB metadata, preset system (K1/K2/U1), installer platform detection, CI release target, documentation

## Context

The Snapmaker U1 has basic support (DB entry, AmsBackendSnapmaker, Docker cross-compilation, deploy scripts) but is missing several pieces needed for a complete, user-facing experience:

- Detection heuristics could be stronger with additional hardware signals
- Printer DB entry lacks metadata fields that other well-defined printers have
- The `"preset"` field in the DB is not wired to any code (K1/K2 have it but it's unused)
- The installer (`scripts/install.sh`) doesn't detect U1 as a platform
- U1 is excluded from the CI release pipeline
- Install docs don't cover U1

**Hard requirement**: Extended firmware (paxx12) is required. Stock firmware U1s are not supported.

## 1. Detection Heuristics

### Existing (keep all 9)

| Type | Pattern | Confidence |
|------|---------|------------|
| `object_exists` | `fm175xx_reader` | 99 |
| `macro_match` | `FILAMENT_DT_UPDATE` | 95 |
| `macro_match` | `FILAMENT_DT_QUERY` | 95 |
| `object_exists` | `tool` | 80 |
| `object_exists` | `camera` | 60 |
| `object_exists` | `tmc2240` | 60 |
| `kinematics_match` | `cartesian` | 20 |
| `hostname_match` | `snapmaker` | 85 |
| `hostname_match` | `u1` | 90 |

### New heuristics to add

| Type | Pattern/Config | Confidence | Rationale |
|------|---------------|------------|-----------|
| `tool_count` | `tool_count_4` | 75 | Exactly 4 extruders — strong signal combined with toolchanger |
| `build_volume_range` | ~270x270mm | 50 | Matches U1 bed plate (NOT 300x300) |
| `fan_match` | `e1_fan` | 70 | Per-tool fan naming unique to U1 |
| `fan_match` | `e2_fan` | 70 | Per-tool fan naming |
| `fan_match` | `e3_fan` | 70 | Per-tool fan naming |
| `object_exists` | `purifier` | 75 | Built-in air purifier — unusual for printers |
| `macro_match` | `EXTRUDER_OFFSET_ACTION_PROBE_CALIBRATE_ALL` | 85 | U1-specific calibration macro |
| `cpu_arch_match` | `aarch64` | 10 | Low but stacks with other signals |

## 2. Printer DB Metadata

Add missing fields to the `snapmaker_u1` entry in `config/printer_database.json`:

```json
{
  "id": "snapmaker_u1",
  "name": "Snapmaker U1",
  "manufacturer": "Snapmaker",
  "image": "snapmaker-u1.png",
  "toolhead_style": "snapmaker_u1",
  "probe_type": "eddy_current",
  "preset": "snapmaker_u1",
  "print_start_profile": "snapmaker_u1",
  "z_offset_calibration_strategy": "probe_calibrate",
  "heuristics": [ ... ]
}
```

**New fields:**
- `toolhead_style`: `"snapmaker_u1"` — for future toolhead UI rendering
- `probe_type`: `"eddy_current"` — inductance coil probing system
- `preset`: `"snapmaker_u1"` — triggers wizard hardware skip (see §3)
- `z_offset_calibration_strategy`: `"probe_calibrate"` — standard Klipper PROBE_CALIBRATE flow (eddy current probe)

`print_start_capabilities` deferred until we have confirmed macro parameter details from the U1's actual PRINT_START implementation.

## 3. Preset System (K1, K2, U1)

### Problem

The `"preset"` field exists in the printer DB for K1 (`"k1"`) and K2 (`"k2"`) entries, but no code reads it from the DB or uses it to drive wizard behavior. The wizard's preset mode is only activated by `Config::has_preset()` which reads from `settings.json`, not from printer detection.

### Solution

Wire the detection result to the wizard:

1. **PrinterDetector** already returns the matched DB entry. When a match is found with a `"preset"` field, store it.
2. **After detection**, if the matched printer has a `"preset"` value, write it to `Config` (e.g., `Config::set_preset(preset_value)`).
3. **Wizard** already checks `Config::has_preset()` and skips hardware steps in preset mode. This continues to work as-is.

### What preset mode does (existing behavior, no changes needed)

When `preset_mode = true`, the wizard:
- Skips printer identification step
- Skips heater selection step
- Skips fan selection step
- Skips AMS step
- Skips LED step
- Skips filament sensor step
- Skips input shaper step
- Skips summary step
- Shows telemetry opt-in step

### Affected printers

All printers with `"preset"` in the DB get this behavior:
- Creality K1 / K1 Max / K1C / K1 SE (`"preset": "k1"`)
- Creality K2 Plus / K2 Plus CF (`"preset": "k2"`)
- Snapmaker U1 (`"preset": "snapmaker_u1"`) — new

## 4. Installer Platform Detection

### Module: `scripts/lib/installer/platform.sh`

Add U1 detection to the platform detection chain. U1 must be checked **before** the generic Pi/ARM SBC check since it's also aarch64 Debian.

**Detection markers** (require 2+ for positive match):
- `/home/lava/` exists (extended firmware user home)
- `/home/lava/printer_data/` exists (Klipper data dir)
- SoC is RK3562 (check `/proc/device-tree/compatible` or `dmesg`)
- SquashFS root (`/` is squashfs in `mount` output)
- `/usr/bin/unisrv` exists (stock Snapmaker UI binary)
- `/oem/` partition exists

**Return value**: `snapmaker-u1`

### Install flow for U1

1. Download `helix-screen-snapmaker-u1.tar.gz` from R2/GitHub
2. Extract to `/userdata/helixscreen/` (writable partition)
3. Run `snapmaker-u1-setup-autostart.sh` to configure init.d
4. Create DRM keepalive hook (`config/platform/hooks-snapmaker-u1.sh`)
5. Stop stock UI (`unisrv`), start HelixScreen

### Install method

The U1 extended firmware has `curl` with HTTPS support (verified on device). Standard `curl | sh` one-liner works — no two-step fallback needed.

## 5. CI Release Pipeline

### `.github/workflows/release.yml`

Add `snapmaker-u1` to the `build-platforms` matrix:

```yaml
- platform: snapmaker-u1
  docker_image: helixscreen-snapmaker-u1
  dockerfile: docker/Dockerfile.snapmaker-u1
  cache_key: snapmaker-u1
```

This builds the U1 binary alongside all other platforms on every tagged release.

### Makefile

Verify `release-snapmaker-u1` and `package-snapmaker-u1` are already in `release-all` / `package-all`. The explore found `release-snapmaker-u1` listed — confirm `package-snapmaker-u1` is also included.

## 6. Documentation

### `docs/user/INSTALL.md`

Add a new section for Snapmaker U1 installation:

- **Requirements**: Extended firmware (paxx12) installed
- **One-liner**: `curl -sSL https://releases.helixscreen.org/install.sh | sh`
- **Manual install**: Download tarball, extract to `/userdata/helixscreen/`, run setup script
- **Autostart**: How `S99screen` delegation works
- **Reverting**: Remove HelixScreen, stock UI resumes automatically
- **Known limitations**: Display resolution may need manual configuration

### `docs/devel/printers/SNAPMAKER_U1_SUPPORT.md`

Update with:
- Detection heuristic summary
- Preset behavior description
- CI build target info
- Install flow details

### Platform support matrix

Update any tables listing supported platforms to include Snapmaker U1.

## 7. Image

Already have `assets/images/printers/snapmaker-u1.png` (3MB). No changes needed.

## Non-Goals

- Baking HelixScreen into the extended firmware overlay (future work)
- Supporting stock firmware U1s
- `print_start_capabilities` macro params (deferred until confirmed)
- KIAUH integration (won't run on U1)
- Display resolution auto-detection (separate issue, needs hardware verification)

## Files Changed

| File | Change |
|------|--------|
| `config/printer_database.json` | Add heuristics + metadata to U1 entry |
| `scripts/lib/installer/platform.sh` | Add U1 platform detection |
| `scripts/lib/installer/service.sh` | U1-specific service setup — delegates to existing `snapmaker-u1-setup-autostart.sh` (init.d, not systemd) |
| `.github/workflows/release.yml` | Add snapmaker-u1 build job |
| `mk/cross.mk` | Verify package-snapmaker-u1 in package-all |
| `src/printer/printer_detector.cpp` or `src/system/config.cpp` | Wire preset from DB → Config |
| `docs/user/INSTALL.md` | U1 install instructions |
| `docs/devel/printers/SNAPMAKER_U1_SUPPORT.md` | Update dev docs |
| `scripts/install.sh` | Regenerate via bundle-installer.sh |
