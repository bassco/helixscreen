<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Snapmaker U1 Support

HelixScreen supports the Snapmaker U1 toolchanger as an alternative touchscreen UI. The U1 runs Klipper with Moonraker on a Rockchip ARM64 SoC, and HelixScreen can replace the stock display interface when deployed via SSH.

## Hardware

| Spec | Value |
|------|-------|
| SoC | Rockchip RK3562 — quad Cortex-A53 @ 2GHz (aarch64) |
| GPU | Mali-G52 2EE (OpenGL ES 3.2) |
| RAM | Unknown (RK3562 supports up to 4GB LPDDR4) |
| Display | 3.5" touch, framebuffer (`/dev/fb0`) — **resolution unconfirmed** (build assumes 480x320, community reports suggest 800x480 or 1024x600; needs hardware verification via `fbset`) |
| Touch Input | `/dev/input/event0` |
| Storage | 26GB eMMC (SquashFS rootfs read-only, `/home/lava/` writable) |
| Recovery | A/B firmware slots + Rockchip MaskRom (unbrickable) |
| Firmware | Klipper + Moonraker |
| OS | Debian Trixie (ARM64) |
| Drivers | TMC2240 steppers |
| Filament | 4-channel RFID reader (FM175xx), OpenSpool NTAG215/216 |
| Camera | MIPI CSI + USB (Rockchip MPP/VPU) |
| Toolheads | 4 independent heads (SnapSwap system) |
| Max Speed | 500mm/s |

### SnapSwap Toolchanger

The U1 is a 4-toolhead color printer. Each head has its own nozzle, extruder, heater, and filament sensor. Tool changes take approximately 5 seconds with no purging required.

The U1 does **not** use the standard [viesturz/klipper-toolchanger](https://github.com/viesturz/klipper-toolchanger) module. Instead, it presents as a standard multi-extruder printer via Moonraker (`extruder`, `extruder1`, `extruder2`, `extruder3`). HelixScreen currently detects multi-extruder configurations but does not have dedicated toolchanger UI for the U1's native multi-extruder approach.

## Cross-Compilation

The U1 target uses the same aarch64 cross-compiler as the Raspberry Pi, with fully static linking to avoid glibc version dependencies.

### Build via Docker (Recommended)

```bash
# Build the Docker toolchain (first time only — cached after)
make snapmaker-u1-docker
```

The Docker image (`docker/Dockerfile.snapmaker-u1`) is based on Debian Trixie with `crossbuild-essential-arm64`. It uses Debian's `aarch64-linux-gnu` toolchain with static linking for a self-contained binary.

### Build Directly (Requires Toolchain)

```bash
make PLATFORM_TARGET=snapmaker-u1 -j
```

### Build Configuration

| Setting | Value |
|---------|-------|
| Architecture | aarch64 (ARMv8-A) |
| Toolchain | `aarch64-linux-gnu-gcc` (Debian cross) |
| Linking | Fully static |
| Display backend | fbdev (`/dev/fb0`) |
| Input | evdev (auto-detected) |
| SSL | Enabled |
| Optimization | `-Os` (size-optimized) |
| Platform define | `HELIX_PLATFORM_SNAPMAKER_U1` |

### CI/Release Status

The Snapmaker U1 target is **deliberately excluded** from the release pipeline (`release-all` and `package-all`). It will not build in GitHub Actions until a workflow job is explicitly added to `.github/workflows/release.yml`. Manual packaging is available:

```bash
make package-snapmaker-u1
```

## Installation

### Prerequisites

- Snapmaker U1 with [extended firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) installed (provides SSH access)
- SSH access: `lava@<printer-ip>` (password: `snapmaker`)

### Deploy

```bash
# Full deploy (binary + assets)
make deploy-snapmaker-u1 SNAPMAKER_U1_HOST=192.168.1.xxx

# Deploy and run in foreground with debug logging
make deploy-snapmaker-u1-fg SNAPMAKER_U1_HOST=192.168.1.xxx

# Deploy binary only (fast iteration during development)
make deploy-snapmaker-u1-bin SNAPMAKER_U1_HOST=192.168.1.xxx

# SSH into the printer
make snapmaker-u1-ssh SNAPMAKER_U1_HOST=192.168.1.xxx
```

Default SSH user is `lava` (override with `SNAPMAKER_U1_USER`). Default deploy directory is `/opt/helixscreen` (override with `SNAPMAKER_U1_DEPLOY_DIR`).

## Reversible Deployment Strategy

HelixScreen can be deployed to the U1 without modifying the stock firmware. The approach is fully reversible at multiple levels.

### Level 1: Manual SSH Deployment (Simplest, Fully Reversible)

Deploy HelixScreen to the writable `/home/lava/` or `/opt/helixscreen/` partition:

1. Install [Extended Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) for SSH access
2. SSH in: `ssh root@<ip>` (password: `snapmaker`)
3. Identify and stop the stock UI: `killall unisrv` (stock UI binary is `/usr/bin/unisrv`)
4. Deploy HelixScreen to writable storage (does NOT touch read-only rootfs)
5. Run HelixScreen directly on the framebuffer

**To revert**: Kill HelixScreen, restart stock UI (`/usr/bin/unisrv`), or simply reboot. The stock UI is on the read-only SquashFS rootfs and cannot be damaged.

### Level 2: Systemd Override (Persistent, Reversible)

Create a systemd override to start HelixScreen instead of the stock UI on boot:

1. Create override in writable storage that masks the stock UI service
2. Create a HelixScreen systemd service file
3. HelixScreen starts on boot; stock UI stays dormant

**To revert**: Remove the systemd override files and reboot. Stock UI resumes automatically.

### Level 3: Extended Firmware Overlay (Cleanest, Reversible)

Package HelixScreen as an overlay in paxx12's Extended Firmware build system:

1. Add a HelixScreen overlay that deploys the binary and service file
2. Build a custom firmware .bin with the overlay included
3. Flash via USB like any firmware update

**To revert**: Flash stock firmware (or Extended Firmware without the HelixScreen overlay) via USB. A/B firmware slots ensure the previous firmware is preserved.

### Safety Guarantees

| Risk | Mitigation |
|------|-----------|
| Bricked device | Impossible — Rockchip MaskRom mode provides hardware-level recovery |
| Lost stock UI | Stock UI lives on read-only SquashFS — cannot be accidentally deleted |
| Klipper/Moonraker disrupted | HelixScreen only replaces the display UI; Klipper (S60klipper) and Moonraker (S61moonraker) are independent services |
| Can't revert | Multiple revert paths: reboot, kill process, remove override, reflash firmware |
| Firmware slot corruption | A/B slots — switch with `updateEngine --misc=other --reboot` |

### Display Backend

HelixScreen renders directly to `/dev/fb0`. The exact display resolution needs hardware verification — the build assumes 480x320 but community sources suggest 800x480. HelixScreen auto-detects resolution from the framebuffer via `FBIOGET_VSCREENINFO`, so this will work regardless. The stock Snapmaker touchscreen UI must be stopped first to release the display. The platform hooks script (`config/platform/hooks-snapmaker-u1.sh`) handles this automatically during deployment by stopping known Snapmaker UI services and killing residual processes.

The stock Snapmaker touchscreen UI is `/usr/bin/unisrv` (Universal Service). It renders directly to `/dev/fb0` and handles touch input from `/dev/input/event0`. The exact systemd service name is unconfirmed — the platform hooks script attempts to stop several candidate names.

### Touch Input

HelixScreen uses evdev and should auto-detect `/dev/input/event0` on the U1. The `lava` user (or root) should have appropriate permissions on the input device.

## Auto-Detection

HelixScreen auto-detects the Snapmaker U1 using several heuristics from `config/printer_database.json`:

| Heuristic | Confidence | Description |
|-----------|------------|-------------|
| `fm175xx_reader` object | 99 | FM175xx RFID reader -- definitive U1 signature |
| `FILAMENT_DT_UPDATE` macro | 95 | RFID filament detection macro (extended firmware) |
| `FILAMENT_DT_QUERY` macro | 95 | RFID filament query macro (extended firmware) |
| Hostname `u1` | 90 | Hostname contains "u1" |
| Hostname `snapmaker` | 85 | Hostname contains "snapmaker" |
| `tmc2240` object | 60 | TMC2240 stepper driver presence |
| CoreXY kinematics | 40 | CoreXY motion system |
| Cartesian kinematics | 20 | Cartesian motion system |

No manual printer configuration is needed in most cases. The FM175xx RFID reader is the strongest signal -- it is unique to the U1 and provides near-certain identification.

## Print Start Tracking

HelixScreen uses the `snapmaker_u1` print start profile (`config/print_start_profiles/snapmaker_u1.json`) to track progress through the startup sequence. The profile uses weighted progress mode with these phases:

1. Homing (10%)
2. Bed heating (20%)
3. Nozzle heating (20%)
4. Z tilt adjust (15%)
5. Bed mesh calibration (15%)
6. Nozzle cleaning (10%)
7. Purging (10%)

The progress bar updates as each phase completes, so you can see exactly where your printer is in its startup routine.

## 480x320 Display Considerations

The U1's 480x320 display uses the TINY layout preset. This is the smallest resolution HelixScreen supports, and several UI panels have known layout issues at this size. See the [480x320 UI Audit](480x320_UI_AUDIT.md) for a panel-by-panel breakdown. Key issues:

- **Navbar icons clipped** at screen edges
- **Controls panel** labels overlapping, z-offset value wrapping
- **Print select list view** fundamentally broken at this size
- **Numeric keypad overlay** too tall, bottom rows cut off
- **Filament panel** cards pushed off-screen

These are resolution-specific issues, not Snapmaker-specific. Any 480x320 device benefits from the same fixes.

## Known Limitations

- **Untested on real hardware** -- Cross-compilation target and detection heuristics are implemented but not yet validated on actual U1 hardware. Community testers are very welcome.
- **No toolchanger UI** -- The U1 presents as a standard multi-extruder printer, not as a `toolchanger` object. HelixScreen detects the extruders but does not have dedicated tool-switching UI for the U1's native approach. See the [toolchanger research](printer-research/SNAPMAKER_U1_RESEARCH.md) for details.
- **RFID filament integration not implemented** -- The U1 has a 4-channel FM175xx RFID reader with OpenSpool support. HelixScreen does not yet read RFID filament data. Klipper commands (`FILAMENT_DT_UPDATE`, `FILAMENT_DT_QUERY`) are available for future integration.
- **Not in CI/release pipeline** -- Must be built manually. No automated release artifacts yet.
- **480x320 UI needs work** -- Multiple panels have layout issues at this resolution (see above).
- **Extended firmware required** -- SSH access (needed for deployment) requires the community extended firmware. Stock firmware does not provide SSH.
- **Snapmaker Klipper/Moonraker source released March 30, 2026** ([u1-klipper](https://github.com/Snapmaker/u1-klipper), [u1-moonraker](https://github.com/Snapmaker/u1-moonraker)). Touchscreen UI remains proprietary.

## Future Work

### Analyze Snapmaker Klipper Fork

The [u1-klipper](https://github.com/Snapmaker/u1-klipper) source reveals the exact Moonraker API surface. The `lava/` directory contains Snapmaker-specific modules and configs. Key investigation: what objects/fields are exposed for tool state, tool switching, and RFID filament. This unblocks proper toolchanger UI implementation.

### RFID Filament Integration

The U1's 4-channel RFID reader could be integrated following the existing AMS backend pattern (`AmsBackendRfid`). Klipper commands:

- `FILAMENT_DT_UPDATE CHANNEL=<n>` -- Read RFID tag
- `FILAMENT_DT_QUERY CHANNEL=<n>` -- Query cached tag data
- OpenSpool JSON format with material type, color, and temperature ranges

### Extended Firmware Overlay

HelixScreen could be packaged as an extended firmware overlay for cleaner installation (vs. manual SSH deployment).

### Open Source Firmware Analysis

Snapmaker released their Klipper and Moonraker forks on March 30, 2026 ([u1-klipper](https://github.com/Snapmaker/u1-klipper), [u1-moonraker](https://github.com/Snapmaker/u1-moonraker)). The full Moonraker API surface can now be documented. This enables proper toolchanger detection and tool-switching UI.

## Community Testing

We need testers with Snapmaker U1 hardware. If you can help:

1. Install the [extended firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) for SSH access
2. Build the binary: `make snapmaker-u1-docker`
3. Deploy: `make deploy-snapmaker-u1-fg SNAPMAKER_U1_HOST=<ip>` (runs in foreground with debug logging)
4. Report back: Does it start? Does the display render at 480x320? Does touch work? Is your printer detected correctly?
5. File issues at the HelixScreen GitHub repository

Even a quick "it boots and shows the home screen" report is valuable.

## Related Resources

- **[Extended Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware)** -- Adds SSH access and community features to the U1
- **[U1 Config Example](https://github.com/JNP-1/Snapmaker-U1-Config)** -- Community reverse-engineered Klipper configuration
- **[Snapmaker Forum](https://forum.snapmaker.com/c/snapmaker-products/87)** -- Official U1 discussion
- **[Toolchanger Research](printer-research/SNAPMAKER_U1_RESEARCH.md)** -- Detailed analysis of U1's toolchanger implementation vs. standard Klipper toolchanger module
- **[Snapmaker/u1-klipper](https://github.com/Snapmaker/u1-klipper)** -- Open source Klipper fork
- **[Snapmaker/u1-moonraker](https://github.com/Snapmaker/u1-moonraker)** -- Open source Moonraker fork
- **[Snapmaker/u1-fluidd](https://github.com/Snapmaker/u1-fluidd)** -- Open source Fluidd fork
- **[paxx12/u1-firmware-tools](https://github.com/paxx12/u1-firmware-tools)** -- Firmware unpack/repack tools
- **[480x320 UI Audit](480x320_UI_AUDIT.md)** -- Panel-by-panel breakdown of layout issues at this resolution
