# Spoolman Server Configuration & Shared Moonraker Config Manager

## Problem
HelixScreen currently auto-detects Spoolman only if it's already configured in Moonraker's `moonraker.conf`. Users who have Spoolman running but haven't configured it in Moonraker have no way to set it up from the touchscreen. Additionally, the timelapse installer currently modifies `moonraker.conf` directly, which is fragile.

## Solution
1. Add Spoolman server setup UI to the existing spoolman settings panel
2. Create a shared `MoonrakerConfigManager` that manages a `helixscreen.conf` include file for all HelixScreen-managed Moonraker config sections
3. Refactor timelapse installer to use the same shared config manager

## Design Decisions
- **UI location**: In the existing `spoolman_settings.xml` panel -- a "Server Setup" section appears when Spoolman isn't detected
- **Input format**: IP/hostname + port as two separate fields (port defaults to 7912, Spoolman's default)
- **Config approach**: Write to a separate `helixscreen.conf` file, add `[include helixscreen.conf]` to `moonraker.conf` -- cleaner separation, follows ecosystem conventions (like KAMP, Shake&Tune)
- **Lifecycle**: Full support for setup, change, and removal of Spoolman config
- **Probing**: Direct HTTP probe from HelixScreen to `http://{ip}:{port}/api/v1/health` before writing any config
- **When configured**: Read-only display of current server URL with Change/Remove buttons

## Shared Moonraker Config Manager

A new utility class `MoonrakerConfigManager` that owns reading/writing `helixscreen.conf` on the printer's config directory via Moonraker's file transfer API.

### Responsibilities
- Download `helixscreen.conf` from printer (or start with empty string if it doesn't exist)
- Add/remove/check for INI-style sections (e.g. `[spoolman]`, `[timelapse]`)
- Upload the modified file back via Moonraker file transfer API
- Ensure `[include helixscreen.conf]` exists in `moonraker.conf` (add if missing)
- Restart Moonraker via existing `restart_moonraker()` API

### Section Management API (pure functions for testability)
```cpp
// Check if a section exists
static bool has_section(const std::string& content, const std::string& section_name);

// Add a section with key-value pairs (appends to content)
static std::string add_section(const std::string& content, const std::string& section_name,
                               const std::vector<std::pair<std::string, std::string>>& entries,
                               const std::string& comment = "");

// Remove a section (and its associated comment line if present)
static std::string remove_section(const std::string& content, const std::string& section_name);

// Ensure [include helixscreen.conf] line exists in moonraker.conf content
static bool has_include_line(const std::string& moonraker_content);
static std::string add_include_line(const std::string& moonraker_content);
```

### Async Workflow API
```cpp
// High-level: ensure helixscreen.conf has the given section, adding [include] to moonraker.conf if needed
void ensure_section(const std::string& section_name,
                    const std::vector<std::pair<std::string, std::string>>& entries,
                    const std::string& comment,
                    SuccessCallback on_success, ErrorCallback on_error);

// High-level: remove a section from helixscreen.conf
void remove_section(const std::string& section_name,
                    SuccessCallback on_success, ErrorCallback on_error);
```

## Spoolman Setup Flow

### When Spoolman is NOT detected
The spoolman settings panel shows a "Server Setup" section at the top:
- IP/hostname text input field
- Port text input field (pre-filled with `7912`)
- "Connect" button

### On "Connect" tap
1. Validate inputs (non-empty IP, valid port number 1-65535)
2. Probe `http://{ip}:{port}/api/v1/health` directly via libhv HTTP client
3. If reachable:
   - Show brief "Spoolman found!" status
   - Use `MoonrakerConfigManager::ensure_section()` to add `[spoolman]` section with `server: http://{ip}:{port}` to `helixscreen.conf`
   - Manager handles `[include helixscreen.conf]` in `moonraker.conf` automatically
   - Restart Moonraker
   - Wait for reconnect, verify `server.spoolman.status` reports connected
   - Toast success, switch to "connected" view
4. If probe fails:
   - Inline error: "Could not reach Spoolman at {ip}:{port}"
   - Keep input fields visible for retry/correction

### When Spoolman IS detected
Show read-only display:
- "Connected to http://{ip}:{port}" status text
- **Change** button -> shows IP/port input fields pre-filled with current values
- **Remove** button -> confirmation dialog ("This will remove Spoolman integration from Moonraker. Continue?"), then removes `[spoolman]` section from `helixscreen.conf` and restarts Moonraker

### Getting current Spoolman URL
The Spoolman server URL for display when connected can be obtained from:
- Parsing `helixscreen.conf` if we wrote it
- Or from the Moonraker `server.spoolman.status` response if it includes the URL
- Fallback: just show "Connected to Spoolman" without the URL if we can't determine it

## Timelapse Installer Refactor

### Current behavior
`TimelapseInstallOverlay::download_and_modify_config()`:
- Downloads `moonraker.conf`
- Checks for `[timelapse]` section via `has_timelapse_section()`
- Appends `[timelapse]` and `[update_manager timelapse]` directly to `moonraker.conf`

### Refactored behavior
- Use `MoonrakerConfigManager::ensure_section()` to add `[timelapse]` and `[update_manager timelapse]` sections to `helixscreen.conf`
- Manager handles `[include helixscreen.conf]` in `moonraker.conf`
- Same restart + verify flow as before

### Migration
If an existing `moonraker.conf` already has a `[timelapse]` section (from a previous HelixScreen install), we leave it alone -- Moonraker handles sections from multiple files gracefully, and the user may have customized it. We only write to `helixscreen.conf` for new installs.

## Testing

### Unit tests for MoonrakerConfigManager
- `has_section()`: detects existing sections, ignores comments, handles edge cases
- `add_section()`: appends correctly, preserves existing content, idempotent
- `remove_section()`: removes section and comment, preserves other sections
- `has_include_line()` / `add_include_line()`: include line detection and insertion
- Section name edge cases (spaces, special characters in Moonraker section names like `[update_manager timelapse]`)

### Unit tests for Spoolman setup
- Config generation: correct `[spoolman]` section format
- Input validation: empty IP, invalid port, port out of range
- URL construction from IP + port

### Updated timelapse tests
- Existing `test_timelapse_install.cpp` tests updated to reflect `helixscreen.conf` approach
- `has_timelapse_section` and `append_timelapse_config` may move to config manager or remain as wrappers

## Files Affected

| File | Change |
|------|--------|
| **New:** `include/moonraker_config_manager.h` | Shared config manager header |
| **New:** `src/system/moonraker_config_manager.cpp` | Shared config manager implementation |
| **New:** `src/ui/ui_spoolman_setup.h` / `.cpp` | Setup UI logic (probe, configure, change, remove) |
| `ui_xml/spoolman_settings.xml` | Add server setup / connected status section |
| `src/ui/ui_overlay_timelapse_install.cpp` | Refactor to use MoonrakerConfigManager |
| `include/ui_overlay_timelapse_install.h` | Remove config helper methods (moved to manager) |
| `tests/unit/test_timelapse_install.cpp` | Update for new approach |
| **New:** `tests/unit/test_moonraker_config_manager.cpp` | Config manager tests |
| **New:** `tests/unit/test_spoolman_setup.cpp` | Spoolman setup tests |

## Out of Scope
- Auto-discovery of Spoolman via mDNS/network scanning
- Spoolman installation (just configuration of an existing instance)
- Fallback probe via Moonraker proxy (can be added later if direct probe proves insufficient)
