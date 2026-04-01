# Snapmaker U1 Support Improvements

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve Snapmaker U1 support with stronger detection heuristics, richer DB metadata, preset system wiring (K1/K2/U1), installer platform detection, CI release integration, and documentation.

**Architecture:** The printer database entry gets additional heuristics and metadata fields. The preset system is wired from detection result → Config → wizard skip logic, affecting K1, K2, and U1. The installer gains U1 platform detection dispatching to existing autostart scripts. CI release workflow adds U1 to the build matrix.

**Tech Stack:** JSON (printer DB), C++ (detector/config), POSIX shell (installer), GitHub Actions YAML (CI), Markdown (docs)

**Spec:** `docs/superpowers/specs/2026-04-01-snapmaker-u1-support-improvements-design.md`

---

## Task 1: Update Snapmaker U1 Printer Database Entry

**Files:**
- Modify: `config/printer_database.json` (lines 4088-4159, the `snapmaker_u1` entry)

- [ ] **Step 1: Add metadata fields to the U1 entry**

Add `toolhead_style`, `probe_type`, `preset`, and `z_offset_calibration_strategy` fields. The entry currently has `id`, `name`, `manufacturer`, `image`, `print_start_profile`, and `heuristics`. Add the new fields between `image` and `print_start_profile`:

```json
{
  "id": "snapmaker_u1",
  "name": "Snapmaker U1",
  "manufacturer": "Snapmaker",
  "image": "snapmaker-u1.png",
  "toolhead_style": "snapmaker_u1",
  "probe_type": "eddy_current",
  "preset": "snapmaker_u1",
  "z_offset_calibration_strategy": "probe_calibrate",
  "print_start_profile": "snapmaker_u1",
  "heuristics": [ ... ]
}
```

- [ ] **Step 2: Add new heuristics to the U1 entry**

Append these 8 new heuristics after the existing 9 (before the closing `]` of the heuristics array):

```json
{
  "type": "tool_count",
  "field": "heaters",
  "pattern": "tool_count_4",
  "confidence": 75,
  "reason": "Exactly 4 extruders (U1 toolchanger)"
},
{
  "type": "build_volume_range",
  "field": "bed_mesh",
  "min_x": 255,
  "max_x": 285,
  "min_y": 255,
  "max_y": 285,
  "confidence": 50,
  "reason": "Build volume ~270mm (Snapmaker U1 bed plate)"
},
{
  "type": "fan_match",
  "field": "fans",
  "pattern": "e1_fan",
  "confidence": 70,
  "reason": "Per-tool fan e1_fan (Snapmaker U1 naming)"
},
{
  "type": "fan_match",
  "field": "fans",
  "pattern": "e2_fan",
  "confidence": 70,
  "reason": "Per-tool fan e2_fan (Snapmaker U1 naming)"
},
{
  "type": "fan_match",
  "field": "fans",
  "pattern": "e3_fan",
  "confidence": 70,
  "reason": "Per-tool fan e3_fan (Snapmaker U1 naming)"
},
{
  "type": "object_exists",
  "field": "printer_objects",
  "pattern": "purifier",
  "confidence": 75,
  "reason": "Built-in air purifier (Snapmaker U1 feature)"
},
{
  "type": "macro_match",
  "field": "macros",
  "pattern": "EXTRUDER_OFFSET_ACTION_PROBE_CALIBRATE_ALL",
  "confidence": 85,
  "reason": "U1-specific multi-tool probe calibration macro"
},
{
  "type": "cpu_arch_match",
  "field": "cpu_arch",
  "pattern": "aarch64",
  "confidence": 10,
  "reason": "ARM64 architecture (low confidence, stacks with other signals)"
}
```

- [ ] **Step 3: Verify JSON is valid**

Run: `python3 -c "import json; json.load(open('config/printer_database.json'))"`
Expected: No output (success)

- [ ] **Step 4: Commit**

```bash
git add config/printer_database.json
git commit -m "feat(snapmaker): enrich U1 printer database entry with heuristics and metadata"
```

---

## Task 2: Add Preset Field to PrinterDetectionResult

**Files:**
- Modify: `include/printer_detector.h` (lines 21-35, `PrinterDetectionResult` struct)
- Modify: `src/printer/printer_detector.cpp` (detection result population)
- Test: `tests/unit/test_printer_detector.cpp`

- [ ] **Step 1: Write failing test for preset in detection result**

In `tests/unit/test_printer_detector.cpp`, add a test after the existing detection tests. Find the `PrinterDetectorFixture` class and add a hardware helper, then add the test case:

Add hardware helper inside the fixture class:

```cpp
PrinterHardwareData snapmaker_u1_hardware() {
    return PrinterHardwareData{
        .heaters = {"extruder", "extruder1", "extruder2", "extruder3", "heater_bed"},
        .fans = {"fan", "fan_generic e1_fan", "fan_generic e2_fan", "fan_generic e3_fan"},
        .hostname = "snapmaker-u1",
        .printer_objects = {"fm175xx_reader", "gcode_macro FILAMENT_DT_UPDATE",
                            "gcode_macro FILAMENT_DT_QUERY", "tool", "camera",
                            "tmc2240 stepper_x", "purifier",
                            "gcode_macro EXTRUDER_OFFSET_ACTION_PROBE_CALIBRATE_ALL"},
        .kinematics = "cartesian",
        .cpu_arch = "aarch64",
        .build_volume = {.x_min = 0, .x_max = 270, .y_min = 0, .y_max = 270, .z_max = 400}};
}
```

Add test case:

```cpp
TEST_CASE_METHOD(PrinterDetectorFixture,
                 "PrinterDetector: Detect Snapmaker U1 with preset field",
                 "[printer][snapmaker]") {
    auto hardware = snapmaker_u1_hardware();
    auto result = PrinterDetector::detect(hardware);

    REQUIRE(result.detected());
    REQUIRE(result.type_name == "Snapmaker U1");
    REQUIRE(result.confidence >= 95);
    REQUIRE(result.preset == "snapmaker_u1");
}

TEST_CASE_METHOD(PrinterDetectorFixture,
                 "PrinterDetector: K1 detection includes preset field",
                 "[printer][preset]") {
    auto hardware = creality_k1_hardware();
    auto result = PrinterDetector::detect(hardware);

    REQUIRE(result.detected());
    REQUIRE(result.type_name == "Creality K1");
    REQUIRE(result.preset == "k1");
}

TEST_CASE_METHOD(PrinterDetectorFixture,
                 "PrinterDetector: Generic printer has empty preset",
                 "[printer][preset]") {
    auto hardware = generic_klipper_hardware();
    auto result = PrinterDetector::detect(hardware);

    // Generic printers without a preset field should have empty preset
    CHECK(result.preset.empty());
}
```

Note: If `creality_k1_hardware()` or `generic_klipper_hardware()` helpers don't already exist in the fixture, create them following the same pattern as `flashforge_ad5m_pro_hardware()` — populate with characteristic objects (corexy, chamber_fan, STM32H723, etc. for K1; minimal set for generic).

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[snapmaker]" "[preset]" -v`
Expected: FAIL — `PrinterDetectionResult` has no `preset` member

- [ ] **Step 3: Add preset field to PrinterDetectionResult**

In `include/printer_detector.h`, add the `preset` field to the struct (around line 28):

```cpp
struct PrinterDetectionResult {
    std::string type_name;
    int confidence;
    std::string reason;
    int match_count = 1;
    int best_single_confidence = 0;
    std::string preset;  ///< Platform preset name from DB (e.g., "k1", "snapmaker_u1"), empty if none
};
```

- [ ] **Step 4: Populate preset from DB entry in detection code**

In `src/printer/printer_detector.cpp`, find the function that builds the final `PrinterDetectionResult` after scoring. This is in `execute_printer_heuristics()` or the caller that selects the best match. After `result.type_name = printer["name"]` (or equivalent), add:

```cpp
if (best_printer.contains("preset") && best_printer["preset"].is_string()) {
    result.preset = best_printer["preset"].get<std::string>();
}
```

Search for where `type_name` is assigned from the JSON to find the exact location. The pattern will be reading from the matched printer JSON object.

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[snapmaker]" "[preset]" -v`
Expected: All 3 new tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/printer_detector.h src/printer/printer_detector.cpp tests/unit/test_printer_detector.cpp
git commit -m "feat(detector): include preset field in PrinterDetectionResult"
```

---

## Task 3: Wire Preset from Detection to Config

**Files:**
- Modify: `include/config.h` (add `set_preset()`)
- Modify: `src/system/config.cpp` (implement `set_preset()`)
- Modify: `src/printer/printer_detector.cpp` (in `auto_detect_and_save()`, write preset)

- [ ] **Step 1: Add set_preset() to Config**

In `include/config.h`, find `has_preset()` and `get_preset()` declarations (around line 221-225). Add after them:

```cpp
/// Set the preset name (written during auto-detection from printer database)
void set_preset(const std::string& preset_name);
```

In `src/system/config.cpp`, find `get_preset()` (line 1222-1227). Add after it:

```cpp
void Config::set_preset(const std::string& preset_name) {
    if (preset_name.empty()) {
        return;
    }
    data["preset"] = preset_name;
    spdlog::info("[Config] Preset set to '{}'", preset_name);
}
```

- [ ] **Step 2: Wire preset in auto_detect_and_save()**

In `src/printer/printer_detector.cpp`, find the `auto_detect_and_save()` function. After the line that saves the detected printer type to config (look for something like `config->set(...)` with `type_name`), add:

```cpp
if (!result.preset.empty()) {
    config->set_preset(result.preset);
    spdlog::info("[PrinterDetector] Auto-detected preset '{}' for printer '{}'",
                 result.preset, result.type_name);
}
```

- [ ] **Step 3: Build and verify compilation**

Run: `make -j`
Expected: Clean build, no errors

- [ ] **Step 4: Commit**

```bash
git add include/config.h src/system/config.cpp src/printer/printer_detector.cpp
git commit -m "feat(config): wire preset from printer detection to config for wizard skip"
```

---

## Task 4: Add U1 to Installer Platform Detection

**Files:**
- Modify: `scripts/lib/installer/platform.sh`

- [ ] **Step 1: Add U1 detection function**

In `scripts/lib/installer/platform.sh`, add a detection block for U1 **before** the Pi/ARM SBC detection (before the block that checks for aarch64 + Debian). The U1 is aarch64 Debian, so it must be checked first to avoid being caught by the generic Pi detection.

Find the section after K1 detection (around line 91) and before Pi detection (around line 93). Insert:

```bash
    # Snapmaker U1 (aarch64 + extended firmware markers)
    # Must check BEFORE generic Pi/ARM SBC since U1 is also aarch64 Debian
    if [ "$arch" = "aarch64" ]; then
        local u1_markers=0
        [ -d "/home/lava" ] && u1_markers=$((u1_markers + 1))
        [ -d "/home/lava/printer_data" ] && u1_markers=$((u1_markers + 1))
        [ -x "/usr/bin/unisrv" ] && u1_markers=$((u1_markers + 1))
        [ -d "/oem" ] && u1_markers=$((u1_markers + 1))
        # Check for RK3562 SoC in device-tree
        if [ -f /proc/device-tree/compatible ] && grep -q "rockchip,rk3562" /proc/device-tree/compatible 2>/dev/null; then
            u1_markers=$((u1_markers + 1))
        fi
        if [ "$u1_markers" -ge 2 ]; then
            echo "snapmaker-u1"
            return 0
        fi
    fi
```

- [ ] **Step 2: Add U1 install paths in set_install_paths()**

In `scripts/lib/installer/platform.sh`, find the `set_install_paths()` function (around line 390). Add a case for `snapmaker-u1` following the existing pattern. The U1 installs to `/userdata/helixscreen/`:

```bash
    snapmaker-u1)
        INSTALL_DIR="/userdata/helixscreen"
        KLIPPER_USER="root"
        INIT_SYSTEM="sysv"
        INIT_SCRIPT_DEST="/etc/init.d/S99helixscreen"
        ;;
```

- [ ] **Step 3: Add U1 service setup in service.sh**

In `scripts/lib/installer/service.sh`, find the SysV init section. The U1 uses its own autostart mechanism via `snapmaker-u1-setup-autostart.sh` rather than a generic init script. Add U1-specific handling in `install_service()`:

```bash
install_service() {
    local platform=$1

    if [ "$platform" = "snapmaker-u1" ]; then
        install_service_snapmaker_u1
        return
    fi

    if [ "$INIT_SYSTEM" = "systemd" ]; then
        install_service_systemd
    else
        install_service_sysv
    fi
}
```

Add the function:

```bash
install_service_snapmaker_u1() {
    log_info "Configuring Snapmaker U1 autostart..."

    # Run the dedicated U1 autostart setup script
    if [ -f "${INSTALL_DIR}/scripts/snapmaker-u1-setup-autostart.sh" ]; then
        bash "${INSTALL_DIR}/scripts/snapmaker-u1-setup-autostart.sh" "${INSTALL_DIR}"
    else
        log_warn "Snapmaker U1 autostart script not found at ${INSTALL_DIR}/scripts/snapmaker-u1-setup-autostart.sh"
        log_warn "You may need to configure autostart manually"
    fi
}
```

- [ ] **Step 4: Add U1 to competing UI stop list**

In `scripts/lib/installer/competing_uis.sh`, find where stock UIs are stopped. Add `unisrv` (Snapmaker's stock UI):

```bash
    # Snapmaker U1 stock UI
    if pgrep -x "unisrv" >/dev/null 2>&1; then
        log_info "Stopping Snapmaker stock UI (unisrv)..."
        killall unisrv 2>/dev/null || true
    fi
```

- [ ] **Step 5: Add U1 platform to release download mapping**

In `scripts/lib/installer/release.sh`, find where platform names are mapped to release artifact filenames. Add the U1 mapping:

```bash
    snapmaker-u1) RELEASE_PLATFORM="snapmaker-u1" ;;
```

- [ ] **Step 6: Bundle the installer**

Run: `bash scripts/bundle-installer.sh -o scripts/install.sh`
Expected: `scripts/install.sh` regenerated successfully

- [ ] **Step 7: Commit**

```bash
git add scripts/lib/installer/platform.sh scripts/lib/installer/service.sh scripts/lib/installer/competing_uis.sh scripts/lib/installer/release.sh scripts/install.sh
git commit -m "feat(installer): add Snapmaker U1 platform detection and install support"
```

---

## Task 5: Include snapmaker-u1-setup-autostart.sh in Release Package

**Files:**
- Modify: `mk/cross.mk` (release packaging for snapmaker-u1)

- [ ] **Step 1: Verify release target includes autostart script**

Check the `release-snapmaker-u1` target in `mk/cross.mk`. It should package `scripts/snapmaker-u1-setup-autostart.sh` into the release tarball. If it doesn't, add it to the file list.

Search for `release-snapmaker-u1` in `mk/cross.mk` and check what files are included. The autostart script needs to be at `${INSTALL_DIR}/scripts/snapmaker-u1-setup-autostart.sh` in the release package (matching what the installer service.sh function expects).

- [ ] **Step 2: Add snapmaker-u1 to release-all and package-all**

In `mk/cross.mk`, find `release-all` (around line 2459):

```makefile
release-all: release-pi release-pi32 release-ad5m release-cc1 release-k1 release-ad5x release-k1-dynamic release-k2 release-snapmaker-u1 release-x86
```

Find `package-all` (around line 2481):

```makefile
package-all: package-ad5m package-cc1 package-pi package-pi32 package-k1 package-ad5x package-k1-dynamic package-k2 package-snapmaker-u1 package-x86
```

- [ ] **Step 3: Commit**

```bash
git add mk/cross.mk
git commit -m "feat(build): add snapmaker-u1 to release-all and package-all targets"
```

---

## Task 6: Add Snapmaker U1 to CI Release Workflow

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Add snapmaker-u1 to build matrix**

In `.github/workflows/release.yml`, find the `build-platforms` job strategy matrix (around line 48-82). Add `snapmaker-u1` to the platform array and add its include block.

Add to the platform array:

```yaml
platform: [pi, pi32, ad5m, cc1, k1, ad5x, k2, snapmaker-u1, x86]
```

Add to the include section (follow existing pattern — read the exact fields used by other entries like `k2` or `ad5m`):

```yaml
        - platform: snapmaker-u1
          display_name: "Snapmaker U1"
```

Check what other fields the existing entries use (`docker_image`, `build_target`, `build_dir`, `cache_key`, etc.) and match the pattern. The Docker image is `helixscreen-snapmaker-u1` and Dockerfile is `docker/Dockerfile.snapmaker-u1`.

- [ ] **Step 2: Add snapmaker-u1 to R2 upload and manifest**

Search for where platform artifacts are uploaded to R2 and where the manifest is generated. Ensure `snapmaker-u1` is included. This may happen automatically if the matrix drives the upload, or may need explicit listing.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add Snapmaker U1 to release build matrix"
```

---

## Task 7: Update Installation Documentation

**Files:**
- Modify: `docs/user/INSTALL.md`

- [ ] **Step 1: Add Snapmaker U1 section to INSTALL.md**

Find the installation guide sections (each platform has its own section). Add a new section for Snapmaker U1 in the appropriate location (alphabetically or grouped with other embedded platforms). Follow the formatting style of existing sections.

```markdown
### Snapmaker U1

**Requirements:**
- Snapmaker U1 with [Extended Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) installed
- SSH access enabled (default: `root@<ip>` or `lava@<ip>`, password: `snapmaker`)

**Quick Install:**

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh
```

The installer automatically detects the Snapmaker U1 and installs to `/userdata/helixscreen/`. It configures autostart so HelixScreen launches instead of the stock UI on boot.

**Manual Install:**

1. Download the latest release:
   ```bash
   wget https://releases.helixscreen.org/stable/helix-screen-snapmaker-u1.tar.gz
   ```

2. Extract to the writable partition:
   ```bash
   mkdir -p /userdata/helixscreen
   tar xzf helix-screen-snapmaker-u1.tar.gz -C /userdata/helixscreen
   ```

3. Configure autostart:
   ```bash
   bash /userdata/helixscreen/scripts/snapmaker-u1-setup-autostart.sh /userdata/helixscreen
   ```

4. Reboot or start manually:
   ```bash
   killall unisrv 2>/dev/null
   /userdata/helixscreen/bin/helix-launcher.sh &
   ```

**Reverting:**

To restore the stock Snapmaker UI, remove HelixScreen:
```bash
rm -rf /userdata/helixscreen
reboot
```

The stock UI (`unisrv`) will resume automatically when HelixScreen is not found.

**Notes:**
- Extended firmware is required — stock firmware does not provide SSH access
- Display resolution may need manual configuration (see [Snapmaker U1 Support](../devel/printers/SNAPMAKER_U1_SUPPORT.md))
- The stock UI can be restored at any time by removing HelixScreen
```

- [ ] **Step 2: Update platform support table**

Find the platform support table/matrix at the top of INSTALL.md (or wherever platforms are listed). Add Snapmaker U1 to the table.

- [ ] **Step 3: Commit**

```bash
git add docs/user/INSTALL.md
git commit -m "docs: add Snapmaker U1 installation instructions"
```

---

## Task 8: Update Developer Documentation

**Files:**
- Modify: `docs/devel/printers/SNAPMAKER_U1_SUPPORT.md`

- [ ] **Step 1: Update SNAPMAKER_U1_SUPPORT.md**

Update the "Current Status" section to reflect the improvements made:

- Detection: 17 heuristics (was 9), with metadata fields (probe_type, toolhead_style, preset, z_offset_calibration_strategy)
- Preset: wizard auto-skips hardware steps when U1 detected
- Installer: `curl | sh` one-liner works, auto-detects U1 platform
- CI: included in release pipeline, builds on every tagged release
- Move display resolution from "Blocker" to "Known Limitation"

Update the "Build & Deploy" section to include the `curl | sh` install method alongside the manual `make deploy-snapmaker-u1` method.

- [ ] **Step 2: Update the "NOT in CI/release pipeline" note**

Remove or update the note at line ~124-125 that says:
> The snapmaker-u1 target is deliberately excluded from `release-all` and `package-all`.

Replace with:
> The snapmaker-u1 target is included in `release-all`, `package-all`, and the GitHub Actions release workflow. Binaries are built on every tagged release.

- [ ] **Step 3: Commit**

```bash
git add docs/devel/printers/SNAPMAKER_U1_SUPPORT.md
git commit -m "docs: update Snapmaker U1 developer documentation with new capabilities"
```

---

## Task 9: Shell Test for U1 Platform Detection

**Files:**
- Check existing: `tests/shell/` or `tests/bats/` for installer test patterns
- Create or modify: installer platform detection tests

- [ ] **Step 1: Find existing installer tests**

Check `tests/shell/` or search for `.bats` test files for the installer. The release workflow runs `make test-shell` which likely executes bats tests.

- [ ] **Step 2: Add U1 platform detection test**

Follow the existing test pattern to add a test that verifies U1 platform detection. The test should mock the filesystem markers (`/home/lava/`, `/oem/`, etc.) and verify `detect_platform` returns `snapmaker-u1`.

If the test infrastructure supports it, add cases for:
- U1 with 2+ markers → detects as `snapmaker-u1`
- Generic aarch64 Debian without U1 markers → detects as `pi` (not U1)

- [ ] **Step 3: Run shell tests**

Run: `make test-shell`
Expected: All tests pass including new U1 detection tests

- [ ] **Step 4: Commit**

```bash
git add tests/shell/
git commit -m "test: add shell tests for Snapmaker U1 platform detection"
```

---

## Summary

| Task | What | Files |
|------|------|-------|
| 1 | DB heuristics + metadata | `config/printer_database.json` |
| 2 | Preset in detection result | `include/printer_detector.h`, `src/printer/printer_detector.cpp`, tests |
| 3 | Wire preset → Config → wizard | `include/config.h`, `src/system/config.cpp`, `src/printer/printer_detector.cpp` |
| 4 | Installer platform detection | `scripts/lib/installer/{platform,service,competing_uis,release}.sh`, `scripts/install.sh` |
| 5 | Release package + Makefile | `mk/cross.mk` |
| 6 | CI release workflow | `.github/workflows/release.yml` |
| 7 | User install docs | `docs/user/INSTALL.md` |
| 8 | Developer docs | `docs/devel/printers/SNAPMAKER_U1_SUPPORT.md` |
| 9 | Shell tests | `tests/shell/` |
