<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# QIDI Printer Support

HelixScreen can run on QIDI printers that have a Linux framebuffer display. However, most older QIDI models use TJC HMI displays (a Chinese Nextion clone) connected over serial UART -- these are standalone MCU-driven screens that cannot be replaced by HelixScreen without a physical screen swap.

If your QIDI is running standard Moonraker -- whether through stock firmware, FreeDi, OpenQ1, or another community project -- and has a Linux framebuffer display, HelixScreen can replace the built-in display interface.

## Display Compatibility

QIDI uses two fundamentally different display architectures:

- **TJC HMI (serial)** -- A standalone microcontroller-driven display connected to the mainboard via serial UART. These are flashed with `.tft` firmware files via microSD card. HelixScreen **cannot** drive these displays. FreeDi targets this display type.
- **Linux framebuffer** -- A display driven directly by the Linux SoC via fbdev or DRM. HelixScreen **can** run on these.

## Models

QIDI uses two generations of mainboard:

- **Older models (X-Max 3, X-Plus 3, Q1 Pro, X-Smart 3):** MKSPI boards with Rockchip RK3328, ARM Cortex-A53 (aarch64), 1 GB RAM. These all use TJC HMI serial displays.
- **Newer models (Q2, Plus 4, Max 4):** New-generation boards with quad-core ARM Cortex-A35 (aarch64), ~498 MB RAM. These use Linux framebuffer displays driven by the SoC.

| Model | Display Type | Resolution | HelixScreen Compatible? | Notes |
|-------|-------------|------------|------------------------|-------|
| Q2 | Linux framebuffer (4.3" IPS capacitive) | 480x272 | **Yes** (confirmed) | Goodix touch controller. User-confirmed working install. WiFi requires wpa_supplicant backend (see below). |
| Plus 4 | Linux framebuffer (5" capacitive) | 800x480 | **Likely yes** (untested) | Same new-gen board architecture as Q2. Uses TJC HMI on stock firmware but has Linux framebuffer capability with FreeDi/OpenQIDI. |
| Max 4 | Linux framebuffer (5" capacitive) | 800x480 | **Likely yes** (untested) | Same display and board architecture as Plus 4. |
| X-Max 3 | TJC HMI (serial) | 800x480 | **No** | Requires screen replacement (HDMI/DSI touchscreen). |
| X-Plus 3 | TJC HMI (serial) | 800x480 | **No** | Requires screen replacement. Same display firmware as X-Max 3. |
| Q1 Pro | TJC HMI (serial) | 480x272 | **No** | Requires screen replacement. TJC model TJC4827X243_011. |
| X-Smart 3 | TJC HMI (serial) | 480x272 | **No** | Requires screen replacement. |

## Installation

### Prerequisites

- A QIDI printer with a compatible display (see table above)
- SSH access to the printer
- For older models (X-Max 3, X-Plus 3): **[FreeDi](https://github.com/Phil1988/FreeDi) installed first** -- FreeDi replaces QIDI's stock OS with Armbian and mainline Klipper/Moonraker.
- For newer models (Q2, Plus 4, Max 4): Stock firmware runs standard Moonraker and can work directly. Community firmware like [53Aries/Q2-Firmware](https://github.com/53Aries/Q2-Firmware) or [OpenQIDI](https://openqidi.com/) may also be used.

### Using the Pi/aarch64 Binary

QIDI's aarch64 processors are the same architecture as the Raspberry Pi 4 and Pi 5. The standard Pi build of HelixScreen runs natively on QIDI hardware with no modifications.

```bash
# Build on a build server (or use a pre-built release)
make pi-docker

# Copy the binary to your QIDI printer
scp build-pi/bin/helix-screen root@<qidi-ip>:/usr/local/bin/

# SSH into the printer and run
ssh root@<qidi-ip>
helix-screen
```

For verbose output during first-time setup, add `-vv` for DEBUG-level logging:

```bash
helix-screen -vv
```

### Display Backend

HelixScreen auto-detects the best available display backend in this order: DRM, fbdev, SDL. QIDI hardware should work with either DRM or fbdev depending on the OS setup. No display configuration is needed -- HelixScreen picks the right backend automatically.

### Touch Input

HelixScreen uses libinput for touch input and should auto-detect `/dev/input/eventX` devices on QIDI hardware. If touch input doesn't work, check that input devices are present and accessible:

```bash
ls /dev/input/event*
```

Ensure the user running HelixScreen has read permissions on the event device. Running as root (common on QIDI printers) avoids permission issues.

## Auto-Detection

HelixScreen auto-detects QIDI printers using several heuristics:

- Hostname patterns
- Chamber heater presence
- MCU identification patterns
- Build volume dimensions
- QIDI-specific G-code macros (`M141`, `M191`, `CLEAR_NOZZLE`)

No manual printer configuration is needed in most cases. HelixScreen identifies your QIDI model and applies the correct settings automatically.

## Print Start Tracking

HelixScreen uses the `qidi` print start profile to track progress through your printer's start sequence. The profile recognizes QIDI's typical startup phases:

1. Homing
2. Bed heating
3. Nozzle cleaning (`CLEAR_NOZZLE`)
4. Z tilt adjust
5. Bed mesh calibration
6. Nozzle heating
7. Chamber heating
8. Print begins

The progress bar updates as each phase completes, so you can see exactly where your printer is in its startup routine.

## WiFi

### Q2 WiFi Hardware

The Q2 uses a USB WiFi dongle from Tenda Technologies with a **Realtek RTL8188GU** chip (2.4 GHz only, 802.11n). The in-tree `rtl8xxxu` kernel module does not work reliably with this chip. The community-recommended fix is the out-of-tree driver from [wandercn/RTL8188GU](https://github.com/wandercn/RTL8188GU), installed via DKMS with the module name `RTL8188GUXX`.

### WiFi Management

The stock Q2 firmware manages WiFi through the closed-source `QD_Q2/bin/client` binary, which writes `wpa_supplicant` configuration directly. When HelixScreen replaces the stock UI, WiFi management must come from HelixScreen instead.

HelixScreen's **wpa_supplicant backend** works on the Q2:
- Connects to `wpa_supplicant` via its control socket (not `wpa_cli`)
- Scans for networks, connects, and saves configuration via `SAVE_CONFIG`
- WiFi credentials persist across reboots via wpa_supplicant's native config file

If the Q2 has NetworkManager installed (e.g., via Armbian/OpenQIDI reflash), HelixScreen will auto-detect and use the NetworkManager backend instead.

### Q2 WiFi Troubleshooting

If WiFi doesn't work in HelixScreen on a Q2:

1. **Check the driver is loaded:** `lsmod | grep -i rtl` — look for `RTL8188GUXX` or `rtl8xxxu`
2. **Check wpa_supplicant is running:** `systemctl status wpa_supplicant` or `ps aux | grep wpa`
3. **Check the interface exists:** `ip link show wlan0`
4. **Check the wpa_supplicant socket:** `ls /var/run/wpa_supplicant/` — HelixScreen needs this socket to communicate
5. **If using stock firmware:** The stock `client` binary may hold the wpa_supplicant socket. Stop it first: `killall client`

## Known Limitations

- **Most older QIDI models have TJC HMI serial displays** -- The X-Max 3, X-Plus 3, Q1 Pro, and X-Smart 3 all use TJC (Nextion-compatible) displays connected via serial UART. HelixScreen cannot drive these. A physical screen replacement (HDMI or DSI touchscreen) is required.
- **Q2 resolution is very small** -- The Q2's 480x272 display uses the MICRO layout. Some UI elements may be cramped but the layout is functional.
- **Q2 has limited RAM** -- ~498 MB total. HelixScreen must be memory-conscious on this device.
- **Plus 4 and Max 4 untested** -- Detection heuristics and display rendering for these models are based on specs. Community testers welcome.
- **No chamber heater control UI** -- QIDI printers have heated chambers, but HelixScreen doesn't yet have a dedicated chamber temperature control panel.
- **Manual deployment required** -- There is no KIAUH or package manager integration for QIDI yet. Binary must be deployed manually.

## Q2 Hardware Details

Gathered from firmware analysis and user reports:

- **SoC:** Quad-core ARM Cortex-A35 @ 1.1 GHz (aarch64), ~498 MB RAM, 32 GB eMMC
- **OS:** Debian Bullseye (glibc 2.31, kernel 5.10.160, systemd)
- **Main MCU:** STM32F407 (`QIDI_MAIN_V2`) via USB
- **Toolhead MCU:** STM32F103 via UART (`/dev/ttyS4`)
- **Display:** 4.3" 480x272 IPS capacitive (Goodix touch)
- **WiFi:** USB dongle, Realtek RTL8188GU (Tenda), 2.4 GHz only
- **SSH:** user `mks`, password `makerbase`
- **Klipper stack:** Standard Klipper + Moonraker (port 7125) + Fluidd, managed via systemd
- **Stock UI:** Closed-source binary at `/home/mks/QD_Q2/bin/client`, pinned to CPU core 0 via `taskset`

Firmware source reference: [53Aries/Q2-Firmware](https://github.com/53Aries/Q2-Firmware)

## Community Testing

We have a confirmed install report on the Q2 (2026-04). Display and touch work. WiFi is the main gap — the wpa_supplicant backend should handle it but needs testing with the RTL8188GU driver.

We still need testers for the **Plus 4** and **Max 4**. If you can help:

1. Build or download the aarch64 binary
2. Deploy it to your QIDI printer
3. Report back: Does it start? Does the display render? Does touch work? Is your printer detected correctly? Does WiFi work?
4. File issues at the HelixScreen GitHub repository

## Related Projects

- **[FreeDi](https://github.com/Phil1988/FreeDi)** -- Replaces QIDI's stock OS with Armbian and mainline Klipper. Recommended base OS for running HelixScreen on QIDI hardware.
- **[GuppyScreen](https://github.com/ballaswag/guppyscreen)** -- Another LVGL-based touchscreen display for Klipper printers.
- **[KlipperScreen](https://github.com/KlipperScreen/KlipperScreen)** -- Python/GTK-based display interface (typically requires an external monitor).
