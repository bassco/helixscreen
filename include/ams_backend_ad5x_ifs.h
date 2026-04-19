// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#if HELIX_HAS_IFS

#include "ams_subscription_backend.h"
#include "filament_slot_override.h"
#include "filament_slot_override_store.h"
#include "slot_registry.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Ad5xIfsTestAccess;

/// AMS backend for FlashForge Adventurer 5X IFS (Intelligent Filament Switching).
///
/// IFS is a 4-lane filament switching system controlled by a separate STM32 MCU,
/// driven through ZMOD's zmod_ifs.py Klipper module.
///
/// === Stock zMod vs plugin variants (IMPORTANT for Moonraker visibility) ===
///
/// Stock zMod owns two Klipper objects, `zmod_ifs` and `zmod_color`, that hold
/// the authoritative per-channel state:
///   - zmod_ifs.ifs_data.get_port(port)         -> per-channel HUB presence switch
///   - zmod_ifs.get_ifs_sensor(port)            -> per-channel motion/stall sensor
///                                                 (located INSIDE the IFS, just
///                                                 after the hub — NOT at the toolhead)
///   - zmod_ifs.get_extruder_sensor()           -> toolhead filament switch
///   - zmod_ifs.get_prutok_type_from_config(p)  -> per-channel material string
///   - zmod_color.get_current_channel()         -> active channel (1-based)
///   - zmod_color.get_printer_data_detail()     -> hasMatlStation, indepMatlInfo, ...
///
/// These are `printer.lookup_object()`-only Python APIs. They are NOT exposed via
/// `get_status()`, so Moonraker (and therefore HelixScreen) cannot subscribe to
/// them directly. Stock zMod only gives Moonraker:
///   - filament_motion_sensor ifs_motion_sensor   (single boolean, post-hub)
///   - filament_switch_sensor head_switch_sensor  (toolhead)
///   - Adventurer5M.json                          (polled via Moonraker file API)
///
/// The lessWaste / bambufy plugins close this gap. They are effectively a
/// Moonraker exporter for zmod_ifs/zmod_color, publishing:
///   - filament_switch_sensor _ifs_port_sensor_{1-4}  per-port HUB presence
///     (wraps zmod_ifs.ifs_data.get_port)
///   - save_variables with <prefix>_colors, _types, _tools, _current_tool,
///     _external   (prefix = "less_waste" or "bambufy"; schema identical)
///   - _IFS_VARS gcode macro for atomic writes of the above
///
/// Plugin delta over stock zMod (via Moonraker):
///   (1) per-channel HUB presence as 4 separate booleans
///   (2) live tool->port mapping (16 slots)
///   (3) active tool index with push notifications
///   (4) bypass/external flag
///   (5) atomic, subscribable color+material updates
/// Everything else — including the toolhead switch — is shared with stock zMod.
///
/// === Sensor -> PathSegment mapping ===
///
///   head_switch_sensor        -> TOOLHEAD / NOZZLE (at toolhead)
///   _ifs_port_sensor_{1..4}   -> HUB               (per-channel, plugin only)
///   ifs_motion_sensor         -> OUTPUT            (post-hub, NOT toolhead;
///                                                   single boolean on stock zMod)
///
/// NOTE: `parse_head_sensor()` currently conflates `ifs_motion_sensor` with the
/// toolhead switch. That is a known simplification — motion at the hub does not
/// mean filament has reached the nozzle. Fixing this requires splitting a
/// hub_output presence from head_filament and updating
/// `system_info_.filament_loaded` + `detect_load_unload_completion()` accordingly.
///
/// Ports are 1-based (1-4), slots are 0-based (0-3).
/// slot_to_port = slot + 1, port_to_slot = port - 1.
class AmsBackendAd5xIfs : public AmsSubscriptionBackend {
  public:
    AmsBackendAd5xIfs(MoonrakerAPI* api, helix::MoonrakerClient* client);
    ~AmsBackendAd5xIfs() override;

    static constexpr int NUM_PORTS = 4;
    static constexpr int TOOL_MAP_SIZE = 16;
    static constexpr int UNMAPPED_PORT = 5;

    // --- AmsBackend interface ---
    [[nodiscard]] AmsType get_type() const override {
        return AmsType::AD5X_IFS;
    }
    [[nodiscard]] PathTopology get_topology() const override {
        return PathTopology::LINEAR;
    }
    [[nodiscard]] PathSegment get_filament_segment() const override;
    [[nodiscard]] PathSegment get_slot_filament_segment(int slot_index) const override;
    [[nodiscard]] PathSegment infer_error_segment() const override;

    [[nodiscard]] AmsSystemInfo get_system_info() const override;
    [[nodiscard]] SlotInfo get_slot_info(int slot_index) const override;
    [[nodiscard]] bool is_bypass_active() const override;

    AmsError load_filament(int slot_index) override;
    AmsError unload_filament(int slot_index = -1) override;
    AmsError select_slot(int slot_index) override;
    AmsError change_tool(int tool_number) override;

    AmsError recover() override;
    AmsError reset() override;
    AmsError cancel() override;

    AmsError set_slot_info(int slot_index, const SlotInfo& info, bool persist = true) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    AmsError enable_bypass() override;
    AmsError disable_bypass() override;

    // IFS firmware persists color + material type but NOT spoolman_id,
    // so ToolState must handle spool assignment persistence via Moonraker DB.
    [[nodiscard]] bool has_firmware_spool_persistence() const override {
        return false;
    }

    // AD5X IFS firmware (ZMOD) validates material against a fixed whitelist
    // and rejects anything outside it with "Invalid material type: X. Valid: ...".
    // The UI dropdown is filtered to this list and outgoing values are normalized
    // via normalize_material() before being sent to firmware.
    [[nodiscard]] std::optional<std::vector<std::string>> get_supported_materials() const override;

    // Firmware-specific aliases for the shared normalize_material() pipeline.
    // AD5X treats SILK as distinct from PLA, but the shared filament DB
    // groups silk variants under compat_group "PLA" (most printers don't
    // make that distinction), so without these aliases "Silk PLA" would
    // collapse to "PLA" instead of "SILK".
    [[nodiscard]] std::vector<std::pair<std::string, std::string>>
    get_material_aliases() const override;

    // Result of parsing a GET_ZCOLOR SILENT=1 response. Public so tests can
    // construct instances via Ad5xIfsTestAccess.
    struct ZColorSlot {
        std::string material;
        std::string hex; // Empty for old-format zmod responses (no /HEX).
    };

    struct ZColorSilentResult {
        bool is_prompt_fallback = false; // Response was an action:prompt dialog
        bool is_old_format = false;      // Slot lines had no /HEX segment
        bool ifs_active = false;         // "IFS: True" in summary line
        bool saw_valid_response = false; // Matched at least one summary or slot line
        std::optional<int> current_channel;
        std::optional<int> extruder_slot; // 0-based, absent when "None"
        std::array<std::optional<ZColorSlot>, NUM_PORTS> slots;
    };

  protected:
    void on_started() override;
    void on_stopping() override;
    void handle_status_update(const nlohmann::json& notification) override;
    const char* backend_log_tag() const override {
        return "[AMS AD5X-IFS]";
    }

  private:
    friend class Ad5xIfsTestAccess;

    void parse_save_variables(const nlohmann::json& vars);
    void parse_port_sensor(int port_1based, bool detected);
    void parse_head_sensor(bool detected);
    void update_slot_from_state(int slot_index);
    // Layer any configured FilamentSlotOverride for `slot_index` over `slot`,
    // mutating `slot` in place. Override wins for every non-default field;
    // default values (empty string, 0, -1.0 weights) fall through to the parsed
    // firmware data untouched. Called from update_slot_from_state so every
    // parse path (save_variables, Adventurer5M.json, GET_ZCOLOR SILENT=1) picks
    // up the override before the SlotInfo is exposed via events.
    void apply_overrides(SlotInfo& slot, int slot_index);
    // Hardware-event detection: if the firmware-reported color for `slot_index`
    // changes vs. the previously observed value, assume the user physically
    // swapped the spool without HelixScreen's involvement and clear the stored
    // override so the new spool's metadata starts blank instead of carrying
    // stale brand/spool_name/spoolman_id from the previous user.
    //
    // `slot` is `entry->info` — when a clear fires, override-exclusive fields
    // (brand/spool_name/spoolman_*/weights/color_name) are zeroed on `slot` so
    // the very next get_slot_info() call reflects the cleared state. (The
    // subsequent apply_overrides() sees no override and is a no-op, so without
    // this reset the stale values would linger until the struct is rebuilt.)
    //
    // Called from update_slot_from_state BEFORE apply_overrides, so the check
    // sees firmware-truth (not the override-masked value). First observation
    // on a given slot is a baseline and NEVER triggers a clear.
    //
    // `firmware_color == 0` is treated as "no color reading" (empty slot,
    // unread, transient) and is ignored — it must not update the baseline.
    void check_hardware_event_clear(int slot_index, uint32_t firmware_color, SlotInfo& slot);
    void parse_adventurer_json(const std::string& content);
    void read_adventurer_json();
    void register_zcolor_listener();
    void register_klippy_ready_listener();
    void unregister_moonraker_listeners();
    void schedule_json_reread();

    // GET_ZCOLOR SILENT=1 primary-truth query. zmod's Adventurer5M.json
    // is a stale last-known-colors cache; SILENT=1 emits one line per
    // physically loaded slot (filtered by live per-port sensors) plus a
    // summary line. See project_ifs_data_sources.md for rationale.
    void query_zcolor_silent();
    void schedule_zcolor_query();
    void finalize_zcolor_response();
    void apply_zcolor_result(const ZColorSilentResult& result);
    static ZColorSilentResult parse_zcolor_silent(const std::vector<std::string>& lines);

    std::string build_color_list_value() const;
    std::string build_type_list_value() const;
    std::string build_tool_map_value() const;
    AmsError write_ifs_var(const std::string& key, const std::string& value);
    AmsError write_adventurer_json(int slot_index);
    void detect_load_unload_completion(bool head_detected);

    int find_first_tool_for_port(int port_1based) const;

  public:
    // Selects the IFS unload G-code for a given UI request.
    //
    // When unloading the slot already loaded to the toolhead, the per-port
    // command (REMOVE_PRUTOK_IFS PRUTOK=N) errors with "No filament N in IFS"
    // on native ZMOD setups whose IFS state disagrees with our color-latched
    // presence. IFS_REMOVE_PRUTOK targets the currently loaded filament and
    // matches both the user's intent and firmware state.
    static std::string select_unload_command(int slot_index, int current_slot, bool head_filament);

  private:
    bool validate_slot_index(int slot_index) const;
    void check_action_timeout();

    // Cached state from save_variables
    // Variable prefix: "less_waste" (lessWaste/zmod) or "bambufy" — auto-detected from
    // whichever save_variables are present on the printer.
    std::string var_prefix_ = "less_waste";
    std::array<std::string, NUM_PORTS> colors_;    // Hex strings: "FF0000"
    std::array<std::string, NUM_PORTS> materials_; // Material names: "PLA"
    std::array<int, TOOL_MAP_SIZE> tool_map_;      // tool_map_[tool] = port (1-4, 5=unmapped)
    std::array<bool, NUM_PORTS> port_presence_;    // Per-port filament sensor state
    int active_tool_ = -1;                         // Current tool (-1 = none)
    bool external_mode_ = false;                   // Bypass/external spool mode
    bool head_filament_ = false;                   // Head sensor state
    std::array<bool, NUM_PORTS> dirty_{}; // Per-slot dirty flag to prevent stale overwrites

    helix::printer::SlotRegistry slots_;

    // Native ZMOD IFS has no per-port sensors — infer port presence from active
    // tool + head sensor state so the UI doesn't show all slots as EMPTY.
    bool has_per_port_sensors_ = false;

    // True if _IFS_VARS macro is available (lessWaste or bambufy plugin).
    // False for native ZMOD, which stores color/type in Adventurer5M.json
    // (read/written via Moonraker HTTP file API).
    bool has_ifs_vars_ = false;

    // Latch: set true in on_started when the gcode_macro _ifs_vars query
    // confirms the macro is not registered. save_variables can contain stale
    // less_waste_* / bambufy_* entries (partial install, plugin removed) —
    // without this latch, every subsequent save_variables notify re-enables
    // has_ifs_vars_, producing "Unknown command: _IFS_VARS" errors.
    bool ifs_macro_confirmed_missing_ = false;
    std::atomic<bool> reread_pending_{false};

    // GET_ZCOLOR SILENT=1 query state.
    // zcolor_silent_supported_ starts optimistic; a prompt-style response
    // flips it false for the session (not retried).
    std::atomic<bool> zcolor_query_active_{false};
    std::atomic<bool> zcolor_query_pending_{false};
    std::atomic<bool> zcolor_silent_supported_{true};
    std::mutex zcolor_buffer_mutex_;
    std::vector<std::string> zcolor_response_buffer_;

    // Action timeout tracking
    static constexpr int ACTION_TIMEOUT_SECONDS = 90;
    std::chrono::steady_clock::time_point action_start_time_;

    // User-provided per-slot metadata (brand, spool name, spoolman IDs, remaining
    // weight, etc.) layered over firmware-reported state.
    //
    // Write paths (both hold mutex_):
    //   - on_started(): initial bulk load from Moonraker DB lane_data.
    //     Swap happens under mutex_ so a concurrent status notification can
    //     never see a torn map.
    //   - set_slot_info(persist=true): user edit staged into overrides_
    //     BEFORE update_slot_from_state() is called, so apply_overrides on
    //     the very same call applies the new values rather than the old
    //     pre-edit override.
    //
    // Read: in apply_overrides() during the parse path, which always runs
    // under mutex_ (via update_slot_from_state).
    std::unique_ptr<helix::ams::FilamentSlotOverrideStore> override_store_;
    std::unordered_map<int, helix::ams::FilamentSlotOverride> overrides_;

    // Per-slot previous firmware color (NOT the override-masked value).
    // Used to detect hardware-event "user swapped physical spool" and clear
    // the override so stale brand/spool_name/spoolman_id from the previous
    // physical spool don't bleed onto the new one. Empty = first observation
    // (baseline, never triggers a clear). firmware_color == 0 is ignored as
    // "no reading" and does not update the baseline.
    //
    // Access is always under mutex_ (only written/read from
    // update_slot_from_state -> check_hardware_event_clear, whose callers
    // hold the lock).
    std::unordered_map<int, uint32_t> last_firmware_color_;

    // Note: uses inherited lifetime_ from AmsSubscriptionBackend (not shadowed).
};

#endif // HELIX_HAS_IFS
