// SPDX-License-Identifier: GPL-3.0-or-later
#include "active_material_provider.h"

#include "ams_state.h"
#include "app_constants.h"

namespace helix {

ActiveMaterial build_active_material(const SlotInfo& slot) {
    ActiveMaterial result;
    result.color_rgb = slot.color_rgb;
    result.brand = slot.brand;
    result.material_name = slot.material;
    result.spoolman_id = slot.spoolman_id;
    result.spoolman_filament_id = slot.spoolman_filament_id;
    result.spoolman_vendor_id = slot.spoolman_vendor_id;

    // Build display name: "Brand Material" or just "Material"
    if (!slot.brand.empty() && !slot.material.empty()) {
        result.display_name = slot.brand + " " + slot.material;
    } else if (!slot.material.empty()) {
        result.display_name = slot.material;
    } else {
        result.display_name = "Unknown";
    }

    // Resolve material_info from filament database (with user overrides)
    auto db_mat = filament::find_material(slot.material);

    if (db_mat.has_value()) {
        result.material_info = *db_mat;

        // If slot has explicit temps, override DB values
        if (slot.nozzle_temp_min > 0) {
            result.material_info.nozzle_min = slot.nozzle_temp_min;
        }
        if (slot.nozzle_temp_max > 0) {
            result.material_info.nozzle_max = slot.nozzle_temp_max;
        }
        if (slot.bed_temp > 0) {
            result.material_info.bed_temp = slot.bed_temp;
        }
    } else {
        // Unknown material — build synthetic MaterialInfo from slot temps or defaults
        int fallback_temp = AppConstants::Ams::DEFAULT_LOAD_PREHEAT_TEMP;
        result.material_info = filament::MaterialInfo{
            .name = "",
            .nozzle_min = slot.nozzle_temp_min > 0 ? slot.nozzle_temp_min : fallback_temp,
            .nozzle_max = slot.nozzle_temp_max > 0
                              ? slot.nozzle_temp_max
                              : (slot.nozzle_temp_min > 0 ? slot.nozzle_temp_min : fallback_temp),
            .bed_temp = slot.bed_temp > 0 ? slot.bed_temp : 60,
            .category = "Unknown",
            .dry_temp_c = 0,
            .dry_time_min = 0,
            .density_g_cm3 = 1.24f,
            .chamber_temp_c = 0,
            .compat_group = "UNKNOWN",
        };
    }

    return result;
}

std::optional<ActiveMaterial> get_active_material() {
    auto& ams = AmsState::instance();

    // Priority 1: AMS backend active slot
    AmsBackend* backend = ams.get_backend();
    if (backend && backend->is_filament_loaded()) {
        int current = backend->get_current_slot();
        if (current >= 0) {
            SlotInfo slot = backend->get_slot_info(current);
            if (!slot.material.empty() || slot.nozzle_temp_min > 0) {
                return build_active_material(slot);
            }
        }
    }

    // Priority 2: External spool
    auto ext_spool = ams.get_external_spool_info();
    if (ext_spool.has_value()) {
        return build_active_material(*ext_spool);
    }

    return std::nullopt;
}

} // namespace helix
