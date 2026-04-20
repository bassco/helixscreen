// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_qidi.h"

#include "ams_error.h"

#include <spdlog/spdlog.h>

// Stub backend for the QIDI Box filament changer. Every operation logs a
// "not yet implemented" warning and returns a sensible no-op. See the
// class comment in ams_backend_qidi.h for protocol-reference pointers.
//
// TODO(qidi-box): replace with real implementation once a PLUS4 / Q2 / MAX4
// test device is available. Protocol lives in the qidi-community
// "customisable_qidibox_firmware" project (Plus4-Wiki repo).
//
// TODO(qidi-box): drop a `qidi_box_64.png` (and matching .svg / `_512.png`
// if other backends carry them) into assets/images/ams/ to match the logo
// convention used by afc_64.png, box_turtle_64.png, happy_hare_64.png, etc.
// The QIDI wordmark / box silhouette is fine — no in-app scaling required.

AmsBackendQidi::AmsBackendQidi(MoonrakerAPI* api, helix::MoonrakerClient* client)
    : AmsSubscriptionBackend(api, client) {
    // Populate system_info_ so get_system_info() returns a self-consistent
    // empty-but-initialised snapshot even before any status update arrives.
    system_info_.type = AmsType::QIDI_BOX;
    system_info_.type_name = "QIDI Box"; // i18n: do not translate - product name
    system_info_.total_slots = NUM_SLOTS;
    system_info_.supports_bypass = false;
    system_info_.supports_tool_mapping = true;
    system_info_.supports_endless_spool = false;
    system_info_.supports_purge = false;
    system_info_.tip_method = TipMethod::CUT;

    // Single unit with NUM_SLOTS empty slots, PARALLEL-less HUB topology.
    AmsUnit unit;
    unit.unit_index = 0;
    unit.name = "QIDI Box";
    unit.display_name = "QIDI Box";
    unit.slot_count = NUM_SLOTS;
    unit.first_slot_global_index = 0;
    unit.connected = false; // flip once protocol is implemented
    unit.topology = PathTopology::HUB;

    for (int i = 0; i < NUM_SLOTS; ++i) {
        SlotInfo slot;
        slot.slot_index = i;
        slot.global_index = i;
        slot.status = SlotStatus::UNKNOWN;
        slot.mapped_tool = i;
        unit.slots.push_back(slot);
    }

    system_info_.units.push_back(std::move(unit));

    spdlog::debug("{} Stub backend constructed ({} slots, no protocol implemented)",
                  backend_log_tag(), NUM_SLOTS);
}

AmsBackendQidi::~AmsBackendQidi() = default;

// --- Lifecycle hooks ---

void AmsBackendQidi::on_started() {
    spdlog::warn("{} {} not yet implemented — backend is a stub pending live hardware",
                 backend_log_tag(), __func__);
    // Intentionally no subscription work: we have nothing to subscribe to yet.
}

void AmsBackendQidi::handle_status_update(const nlohmann::json& /*notification*/) {
    // No protocol wired up — ignore status notifications. Left as a no-op
    // (rather than a log) to avoid spamming the log on every unrelated
    // Moonraker notify_status_update the subscription machinery may deliver.
}

// --- State queries ---

AmsSystemInfo AmsBackendQidi::get_system_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_;
}

SlotInfo AmsBackendQidi::get_slot_info(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_index < 0 || slot_index >= NUM_SLOTS) {
        return SlotInfo{};
    }
    const auto* slot = system_info_.get_slot_global(slot_index);
    return slot ? *slot : SlotInfo{};
}

bool AmsBackendQidi::is_bypass_active() const {
    return false;
}

// --- Path visualisation ---

PathSegment AmsBackendQidi::get_filament_segment() const {
    return PathSegment::NONE;
}

PathSegment AmsBackendQidi::get_slot_filament_segment(int /*slot_index*/) const {
    return PathSegment::NONE;
}

PathSegment AmsBackendQidi::infer_error_segment() const {
    return PathSegment::NONE;
}

// --- Filament operations ---

AmsError AmsBackendQidi::load_filament(int /*slot_index*/) {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box load_filament");
}

AmsError AmsBackendQidi::unload_filament(int /*slot_index*/) {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box unload_filament");
}

AmsError AmsBackendQidi::select_slot(int /*slot_index*/) {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box select_slot");
}

AmsError AmsBackendQidi::change_tool(int /*tool_number*/) {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box change_tool");
}

// --- Recovery ---

AmsError AmsBackendQidi::recover() {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box recover");
}

AmsError AmsBackendQidi::reset() {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box reset");
}

AmsError AmsBackendQidi::cancel() {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box cancel");
}

// --- Configuration ---

AmsError AmsBackendQidi::set_slot_info(int /*slot_index*/, const SlotInfo& /*info*/,
                                       bool /*persist*/) {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box set_slot_info");
}

AmsError AmsBackendQidi::set_tool_mapping(int /*tool_number*/, int /*slot_index*/) {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box set_tool_mapping");
}

void AmsBackendQidi::clear_slot_override(int /*slot_index*/) {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
}

// --- Bypass ---

AmsError AmsBackendQidi::enable_bypass() {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box enable_bypass");
}

AmsError AmsBackendQidi::disable_bypass() {
    spdlog::warn("{} {} not yet implemented", backend_log_tag(), __func__);
    return AmsErrorHelper::not_supported("QIDI Box disable_bypass");
}
