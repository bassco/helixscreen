// SPDX-License-Identifier: GPL-3.0-or-later

#include "spoolman_slot_saver.h"

#include "moonraker_api.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "hv/json.hpp"

namespace helix {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

SpoolmanSlotSaver::SpoolmanSlotSaver(MoonrakerAPI* api) : api_(api) {}

bool SpoolmanSlotSaver::is_filament_complete(const SlotInfo& slot) {
    return !slot.brand.empty() && !slot.material.empty() &&
           slot.color_rgb != AMS_DEFAULT_SLOT_COLOR;
}

ChangeSet SpoolmanSlotSaver::detect_changes(const SlotInfo& original, const SlotInfo& edited) {
    ChangeSet changes;

    // Filament-level: brand, material, color_rgb
    if (original.brand != edited.brand || original.material != edited.material ||
        original.color_rgb != edited.color_rgb) {
        changes.filament_level = true;
    }

    // Spool-level: remaining_weight_g (float comparison with threshold) or spoolman_id
    if (std::abs(original.remaining_weight_g - edited.remaining_weight_g) > WEIGHT_THRESHOLD ||
        original.spoolman_id != edited.spoolman_id) {
        changes.spool_level = true;
    }

    return changes;
}

std::string SpoolmanSlotSaver::color_to_hex(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06X", rgb & 0xFFFFFF);
    return std::string(buf);
}

void SpoolmanSlotSaver::save(const SlotInfo& original, const SlotInfo& edited,
                             CompletionCallback on_complete) {
    // No-op for non-Spoolman slots
    if (!edited.spoolman_id) {
        spdlog::debug("[SpoolmanSlotSaver] No spoolman_id, skipping save");
        if (on_complete)
            on_complete(SaveResult{.success = true});
        return;
    }

    auto changes = detect_changes(original, edited);

    // No changes detected
    if (!changes.any()) {
        spdlog::debug("[SpoolmanSlotSaver] No changes detected for spool {}", edited.spoolman_id);
        if (on_complete)
            on_complete(SaveResult{.success = true});
        return;
    }

    const int spool_id = edited.spoolman_id;

    // Only spool-level (weight) change
    if (!changes.filament_level && changes.spool_level) {
        spdlog::info("[SpoolmanSlotSaver] Updating weight for spool {} to {:.1f}g", spool_id,
                     edited.remaining_weight_g);
        update_weight(spool_id, edited.remaining_weight_g, on_complete);
        return;
    }

    // Filament-level change (possibly also weight)
    if (changes.filament_level) {
        spdlog::info("[SpoolmanSlotSaver] Filament-level change for spool {} "
                     "(brand={}, material={}, color={:#08x})",
                     spool_id, edited.brand, edited.material, edited.color_rgb);

        if (!is_filament_complete(edited)) {
            spdlog::info("[SpoolmanSlotSaver] Filament fields incomplete for spool {} — "
                         "skipping Spoolman filament write",
                         spool_id);
            if (changes.spool_level) {
                update_weight(spool_id, edited.remaining_weight_g, on_complete);
                return;
            }
            if (on_complete)
                on_complete(SaveResult{.success = true});
            return;
        }

        const std::string color_hex = color_to_hex(edited.color_rgb);
        const float weight = edited.remaining_weight_g;
        const int original_filament_id = original.spoolman_filament_id;
        const bool weight_changed = changes.spool_level;

        find_or_create_vendor(
            edited.brand,
            [this, edited, color_hex, spool_id, weight, original_filament_id, weight_changed,
             on_complete](int vendor_id) {
                find_or_create_filament(
                    vendor_id, edited.material, color_hex,
                    [this, spool_id, weight, vendor_id, original_filament_id, weight_changed,
                     on_complete](int filament_id) {
                        // If we resolved to the SAME filament, skip repoint
                        // (but still do weight if that also changed).
                        if (filament_id == original_filament_id) {
                            spdlog::debug("[SpoolmanSlotSaver] Resolved to same filament_id={}, "
                                          "skipping repoint",
                                          filament_id);
                            if (weight_changed) {
                                update_weight(
                                    spool_id, weight,
                                    [vendor_id, filament_id, on_complete](const SaveResult& r) {
                                        SaveResult out = r;
                                        out.new_vendor_id = vendor_id;
                                        out.new_filament_id = filament_id;
                                        if (on_complete)
                                            on_complete(out);
                                    });
                                return;
                            }
                            SaveResult out;
                            out.success = true;
                            out.new_vendor_id = vendor_id;
                            out.new_filament_id = filament_id;
                            if (on_complete)
                                on_complete(out);
                            return;
                        }

                        // Different filament — repoint the spool at it.
                        repoint_spool(
                            spool_id, filament_id,
                            [this, spool_id, weight, vendor_id, filament_id, weight_changed,
                             on_complete]() {
                                if (weight_changed) {
                                    update_weight(
                                        spool_id, weight,
                                        [vendor_id, filament_id,
                                         on_complete](const SaveResult& r) {
                                            SaveResult out = r;
                                            out.repointed_filament = true;
                                            out.new_vendor_id = vendor_id;
                                            out.new_filament_id = filament_id;
                                            if (on_complete)
                                                on_complete(out);
                                        });
                                    return;
                                }
                                SaveResult out;
                                out.success = true;
                                out.repointed_filament = true;
                                out.new_vendor_id = vendor_id;
                                out.new_filament_id = filament_id;
                                if (on_complete)
                                    on_complete(out);
                            },
                            [on_complete](const MoonrakerError& err) {
                                spdlog::error("[SpoolmanSlotSaver] repoint_spool failed: {}",
                                              err.message);
                                if (on_complete)
                                    on_complete(SaveResult{.success = false});
                            });
                    },
                    [on_complete](const MoonrakerError& err) {
                        spdlog::error("[SpoolmanSlotSaver] find_or_create_filament failed: {}",
                                      err.message);
                        if (on_complete)
                            on_complete(SaveResult{.success = false});
                    });
            },
            [on_complete](const MoonrakerError& err) {
                spdlog::error("[SpoolmanSlotSaver] find_or_create_vendor failed: {}", err.message);
                if (on_complete)
                    on_complete(SaveResult{.success = false});
            });
        return;
    }
}

void SpoolmanSlotSaver::update_weight(int spool_id, float weight_g,
                                      CompletionCallback on_complete) {
    api_->spoolman().update_spoolman_spool_weight(
        spool_id, static_cast<double>(weight_g),
        [on_complete]() {
            spdlog::debug("[SpoolmanSlotSaver] Weight update succeeded");
            if (on_complete)
                on_complete(SaveResult{.success = true});
        },
        [on_complete](const MoonrakerError& err) {
            spdlog::error("[SpoolmanSlotSaver] Weight update failed: {}", err.message);
            if (on_complete)
                on_complete(SaveResult{.success = false});
        });
}

void SpoolmanSlotSaver::find_or_create_vendor(const std::string& vendor_name,
                                              VendorCallback on_found, ErrorCallback on_error) {
    const std::string needle = to_lower(vendor_name);
    api_->spoolman().get_spoolman_vendors(
        [this, vendor_name, needle, on_found, on_error](const std::vector<VendorInfo>& vendors) {
            for (const auto& v : vendors) {
                if (to_lower(v.name) == needle) {
                    spdlog::debug("[SpoolmanSlotSaver] Reusing vendor '{}' -> id={}", v.name, v.id);
                    if (on_found)
                        on_found(v.id);
                    return;
                }
            }
            nlohmann::json payload;
            payload["name"] = vendor_name;
            spdlog::info("[SpoolmanSlotSaver] Creating vendor '{}'", vendor_name);
            api_->spoolman().create_spoolman_vendor(
                payload,
                [on_found](const VendorInfo& info) {
                    if (on_found)
                        on_found(info.id);
                },
                on_error);
        },
        on_error);
}

std::string SpoolmanSlotSaver::normalize_color_hex(const std::string& in) {
    std::string s = in;
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() != 6)
        return "";
    for (char& c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return "";
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

void SpoolmanSlotSaver::find_or_create_filament(int vendor_id, const std::string& material,
                                                const std::string& color_hex,
                                                FilamentCallback on_found,
                                                ErrorCallback on_error) {
    const std::string needle_color = normalize_color_hex(color_hex);
    if (needle_color.empty()) {
        spdlog::warn("[SpoolmanSlotSaver] Invalid color hex '{}', aborting find_or_create_filament",
                     color_hex);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid color hex: " + color_hex;
            on_error(err);
        }
        return;
    }
    api_->spoolman().get_spoolman_filaments(
        vendor_id,
        [this, vendor_id, material, needle_color,
         on_found, on_error](const std::vector<FilamentInfo>& filaments) {
            for (const auto& f : filaments) {
                if (f.material == material &&
                    normalize_color_hex(f.color_hex) == needle_color) {
                    spdlog::debug("[SpoolmanSlotSaver] Reusing filament id={} "
                                  "(vendor={}, material={}, color={})",
                                  f.id, vendor_id, material, needle_color);
                    if (on_found)
                        on_found(f.id);
                    return;
                }
            }
            nlohmann::json payload;
            payload["vendor_id"] = vendor_id;
            payload["material"] = material;
            payload["color_hex"] = needle_color;
            payload["name"] = material;
            spdlog::info("[SpoolmanSlotSaver] Creating filament "
                         "(vendor={}, material={}, color={})",
                         vendor_id, material, needle_color);
            api_->spoolman().create_spoolman_filament(
                payload,
                [on_found](const FilamentInfo& info) {
                    if (on_found)
                        on_found(info.id);
                },
                on_error);
        },
        on_error);
}

void SpoolmanSlotSaver::repoint_spool(int spool_id, int new_filament_id,
                                      VoidCallback on_success, ErrorCallback on_error) {
    nlohmann::json patch;
    patch["filament_id"] = new_filament_id;
    spdlog::info("[SpoolmanSlotSaver] Repointing spool {} -> filament {}", spool_id,
                 new_filament_id);
    api_->spoolman().update_spoolman_spool(spool_id, patch, on_success, on_error);
}

} // namespace helix
