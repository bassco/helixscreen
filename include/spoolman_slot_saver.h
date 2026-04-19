// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"
#include "spoolman_types.h"

#include <cmath>
#include <functional>

class MoonrakerAPI;

namespace helix {

/**
 * @brief Describes what changed between original and edited SlotInfo
 *
 * Filament-level changes (brand, material, color) require finding or creating
 * a matching Spoolman filament definition. Spool-level changes (weight) only
 * require updating the spool record.
 */
struct ChangeSet {
    bool filament_level = false; ///< vendor, material, or color changed
    bool spool_level = false;    ///< remaining weight changed

    /// Check if any change was detected
    [[nodiscard]] bool any() const {
        return filament_level || spool_level;
    }
};

/**
 * @brief Outcome of a save operation, including any IDs assigned during a
 *        new-spool creation or filament repoint.
 *
 * When `created_new_spool` or `repointed_filament` is true, the modal should
 * persist the new IDs back to the slot via backend->set_slot_info().
 */
struct SaveResult {
    bool success = false;
    bool created_new_spool = false;   ///< set when a new spool was POSTed
    bool repointed_filament = false;  ///< set when PATCH spool changed filament_id
    int new_spool_id = 0;             ///< spool_id assigned on create (0 if unchanged)
    int new_filament_id = 0;          ///< filament_id after find-or-create (0 if unchanged)
    int new_vendor_id = 0;            ///< vendor_id after find-or-create (0 if unchanged)
};

/**
 * @brief Handles saving slot edits back to Spoolman
 *
 * Orchestrates filament and spool updates:
 * 1. Detects what changed between original and edited SlotInfo
 * 2. For filament-level changes: PATCHes the existing filament definition
 * 3. Updates spool weight if changed
 */
class SpoolmanSlotSaver {
  public:
    using CompletionCallback = std::function<void(const SaveResult&)>;

    /// Weight comparison threshold for float equality
    static constexpr float WEIGHT_THRESHOLD = 0.1f;

    /**
     * @brief Construct a SpoolmanSlotSaver
     * @param api MoonrakerAPI instance for Spoolman API calls
     */
    explicit SpoolmanSlotSaver(MoonrakerAPI* api);

    /**
     * @brief Compare two SlotInfo structs and detect what changed
     *
     * @param original The slot state before editing
     * @param edited The slot state after editing
     * @return ChangeSet describing filament-level and spool-level changes
     */
    static ChangeSet detect_changes(const SlotInfo& original, const SlotInfo& edited);

    /**
     * @brief Save slot edits to Spoolman via the API
     *
     * Handles the full async orchestration:
     * - No spoolman_id or no changes: immediate success callback
     * - Only weight changed: update spool weight
     * - Filament changed: PATCH existing filament definition
     * - Both changed: PATCH filament first, then update weight
     *
     * @param original The slot state before editing
     * @param edited The slot state after editing
     * @param on_complete Called with true on success, false on failure
     */
    void save(const SlotInfo& original, const SlotInfo& edited, CompletionCallback on_complete);

  private:
    MoonrakerAPI* api_;

    /**
     * @brief Convert uint32_t RGB to hex string like "FF0000" (no # prefix)
     */
    static std::string color_to_hex(uint32_t rgb);

    /**
     * @brief Update spool weight via API
     */
    void update_weight(int spool_id, float weight_g, CompletionCallback on_complete);

    /**
     * @brief PATCH existing filament definition with changed fields
     */
    void update_filament(int filament_id, const SlotInfo& edited, CompletionCallback on_complete);
};

} // namespace helix

// Bring SpoolmanSlotSaver into global scope for convenience (matches project convention)
using helix::ChangeSet;
using helix::SaveResult;
using helix::SpoolmanSlotSaver;
