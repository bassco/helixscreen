# Snapmaker U1 Toolchanger Research

**Date**: 2026-02-02 (updated 2026-03-31)
**Status**: Source code released - implementation path clear

## Overview

The Snapmaker U1 is a 4-toolhead color 3D printer using the "SnapSwap" system. Each toolhead has its own nozzle pre-loaded with filament, enabling fast tool changes (~5 seconds) without purging.

### Key Specs
- **Toolheads**: 4 independent heads with dedicated extruders
- **Park positions**: Spaced at ~67.5mm intervals along Y=332.2
- **Max speed**: 500mm/s
- **Tool swap rating**: 1,000,000 swaps (system), 250,000 per toolhead (pogo pins)
- **Firmware**: Modified Klipper + Moonraker (open source as of March 2026)
- **Web UI**: Fluidd (unmodified)

---

## Firmware Architecture

### What They Use
Snapmaker runs a **custom fork** of Klipper and Moonraker, now open source:
- **Klipper fork**: https://github.com/Snapmaker/u1-klipper
- **Moonraker fork**: https://github.com/Snapmaker/u1-moonraker
- The `lava/` directory in u1-klipper contains Snapmaker-specific configs and customizations
- Klipper uses `ACTIVATE_EXTRUDER` internally for tool switching, with custom macros in the `lava/` dir
- T0-T3 gcode commands for tool selection
- Fluidd web interface is stock/unmodified

### What They DON'T Use
The U1 does **NOT** use the standard [viesturz/klipper-toolchanger](https://github.com/viesturz/klipper-toolchanger) module. Instead, they implement toolchanging via:
- Native Klipper multi-extruder (`[extruder]`, `[extruder1]`, `[extruder2]`, `[extruder3]`)
- Custom macros for tool parking/switching
- Analog park detectors for tool positioning
- Inductance coils for nozzle height calibration

---

## Klipper Configuration (Community Reverse-Engineered)

From [JNP-1/Snapmaker-U1-Config](https://github.com/JNP-1/Snapmaker-U1-Config):

### Tool Definitions
```ini
[extruder]      # Tool 0 - park position: 35.0, 332.2
[extruder1]     # Tool 1 - park position: 102.7, 332.2
[extruder2]     # Tool 2 - park position: 170.2, 332.2
[extruder3]     # Tool 3 - park position: 237.7, 332.2
```

### Per-Tool Components
Each tool has:
- Stepper motor (step/dir/enable pins)
- Heater + temperature sensor
- Nozzle fan (`[heater_fan eX_nozzle_fan]`)
- Filament sensor (`[filament_motion_sensor eX_filament]`)
- Park detector (analog sensing for tool engagement)
- Inductance coil (nozzle height calibration)

### Tool Switching
- Uses custom macros with `xy_park_position` coordinates
- `switch_accel` set to 25000 for rapid tool changes
- No `[toolchanger]` or `[tool T*]` config sections

---

## Moonraker API Comparison

### What viesturz/klipper-toolchanger Exposes (our current support)

**`toolchanger` object:**
- `status`: 'uninitialized', 'ready', 'changing', 'error'
- `tool`: Current tool name (e.g., "T0") or empty
- `tool_number`: Current tool number or -1
- `tool_numbers`: Array of available tool numbers
- `tool_names`: Array of tool names

**`tool T*` objects:**
- `active`: Boolean - is this tool selected?
- `mounted`: Boolean - is this tool on the carriage?
- `extruder`: Associated extruder name
- `fan`: Associated fan name
- `gcode_x_offset`, `gcode_y_offset`, `gcode_z_offset`: Tool offsets

### What Snapmaker U1 Likely Exposes

**Standard multi-extruder objects:**
- `extruder`, `extruder1`, `extruder2`, `extruder3`
  - `temperature`, `target`, `pressure_advance`, etc.

**Possibly custom objects (unknown until open source):**
- Tool parking state?
- Active tool indicator?
- Tool detection status?

---

## Moonraker API Surface (from open source)

Now that the source is available, we can examine exactly what objects/fields the U1 exposes by reading their Klipper fork. Key areas to check:

- **Custom Klipper objects**: Does their Klipper expose any custom objects beyond standard multi-extruder? Check `lava/` directory for any `[snap_*]` or custom module registrations.
- **Tool management macros**: What macros are available for tool management? The `lava/` dir should contain the T0-T3 macro definitions, parking logic, and `ACTIVATE_EXTRUDER` wrappers.
- **Custom Moonraker endpoints**: Does the u1-moonraker fork add any custom API endpoints beyond stock Moonraker? Check for additional route registrations or custom components.

---

## HelixScreen Compatibility Gap

### Current Detection Logic (`PrinterDiscovery`)
```cpp
// We look for:
has_object("toolchanger")           // NOT present on U1
has_objects_matching("tool *")      // NOT present on U1
```

### Why U1 Won't Be Detected
The U1 presents itself as a standard 4-extruder printer, not a toolchanger:
- No `toolchanger` Klipper object
- No `tool T*` objects
- Just `extruder`, `extruder1`, `extruder2`, `extruder3`

### Current Behavior
U1 would show up with:
- `AmsType::NONE` (no MMU/toolchanger detected)
- Multi-extruder temperature display would work
- No tool change UI, no slot visualization

---

## Options for U1 Support

### Option 1: Analyze Open Source Klipper Fork (Recommended, now actionable)
**Source**: https://github.com/Snapmaker/u1-klipper / https://github.com/Snapmaker/u1-moonraker
**Key task**: Examine the `lava/` directory in u1-klipper to understand the full Moonraker API surface exposed by their modifications.
**Pros**: Real source code available, can determine exact API surface
**Cons**: May still be non-standard, but at least we know exactly what we're dealing with

### Option 2: Detect U1 Specifically
Look for U1-specific markers:
- Exactly 4 extruders with specific park positions?
- U1-specific config objects?
- Machine identifier in `printer_info`?

**Pros**: Could work with current firmware
**Cons**: Fragile, relies on unofficial reverse-engineering

### Option 3: Extended Firmware Support
[paxx12's extended firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) adds features. Could potentially:
- Add proper viesturz/klipper-toolchanger support
- Or expose U1 toolhead state in a standard way

**Pros**: Community-driven, could push for compatibility
**Cons**: Voids warranty, not everyone will use it

### Option 4: Generic Multi-Extruder Toolchanger Detection
Treat any printer with multiple `[extruder*]` + parking macros as a toolchanger variant.

**Pros**: Would catch U1 and similar machines
**Cons**: False positives (not all multi-extruder is toolchanger)

---

## Community Resources

- **Forum**: [Snapmaker U1 Toolchanger Category](https://forum.snapmaker.com/c/snapmaker-products/87)
- **Custom Firmware**: [paxx12/SnapmakerU1-Extended-Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware)
- **Config Example**: [JNP-1/Snapmaker-U1-Config](https://github.com/JNP-1/Snapmaker-U1-Config)
- **Discord**: Snapmaker Discord `#u1-printer` channel

---

## Conclusion

The Snapmaker U1 is a toolchanger that **doesn't present itself as one** via Moonraker. It uses native Klipper multi-extruder primitives instead of the viesturz/klipper-toolchanger module that HelixScreen expects.

**Path forward**: The source code is now available. The next step is to analyze the released u1-klipper and u1-moonraker repositories (particularly the `lava/` directory) to understand the exact API surface. From there, implement either a U1-specific detection bridge or a generic multi-extruder toolchanger mode that handles printers using `ACTIVATE_EXTRUDER` + T-commands rather than the viesturz `[toolchanger]` module.
