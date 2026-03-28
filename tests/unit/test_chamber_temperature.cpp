// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "lvgl.h"
#include "printer_capabilities_state.h"
#include "printer_discovery.h"
#include "printer_temperature_state.h"
#include "settings_manager.h"
#include "temperature_sensor_manager.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterCapabilitiesState;
using helix::PrinterDiscovery;
using helix::PrinterTemperatureState;

// 1. PrinterDiscovery stores chamber sensor name
TEST_CASE("PrinterDiscovery stores chamber sensor name", "[discovery][chamber]") {
    PrinterDiscovery discovery;
    nlohmann::json objects = {"temperature_sensor chamber", "extruder", "heater_bed"};
    discovery.parse_objects(objects);

    REQUIRE(discovery.has_chamber_sensor());
    REQUIRE(discovery.chamber_sensor_name() == "temperature_sensor chamber");
}

// 2. PrinterTemperatureState updates chamber temp from status
TEST_CASE("PrinterTemperatureState updates chamber temp from status", "[temperature][chamber]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false); // No XML registration in tests
    temp_state.set_chamber_sensor_name("temperature_sensor chamber");

    nlohmann::json status = {{"temperature_sensor chamber", {{"temperature", 45.3}}}};
    temp_state.update_from_status(status);

    REQUIRE(lv_subject_get_int(temp_state.get_chamber_temp_subject()) == 453); // centidegrees
}

// 3. PrinterCapabilitiesState sets chamber sensor capability
TEST_CASE("PrinterCapabilitiesState sets chamber sensor capability", "[capabilities][chamber]") {
    LVGLTestFixture fixture;

    PrinterCapabilitiesState caps;
    caps.init_subjects(false);

    // Verify initial state before set_hardware()
    REQUIRE(lv_subject_get_int(caps.get_printer_has_chamber_sensor_subject()) == 0);

    PrinterDiscovery hardware;
    nlohmann::json objects = {"temperature_sensor chamber"};
    hardware.parse_objects(objects);

    CapabilityOverrides overrides;
    caps.set_hardware(hardware, overrides);

    REQUIRE(lv_subject_get_int(caps.get_printer_has_chamber_sensor_subject()) == 1);
}

// 4. No chamber sensor - capability is 0
TEST_CASE("PrinterCapabilitiesState reports no chamber sensor when absent",
          "[capabilities][chamber]") {
    LVGLTestFixture fixture;

    PrinterCapabilitiesState caps;
    caps.init_subjects(false);

    // Verify initial state before set_hardware()
    REQUIRE(lv_subject_get_int(caps.get_printer_has_chamber_sensor_subject()) == 0);

    PrinterDiscovery hardware;
    nlohmann::json objects = {"extruder", "heater_bed"}; // No chamber
    hardware.parse_objects(objects);

    CapabilityOverrides overrides;
    caps.set_hardware(hardware, overrides);

    REQUIRE(lv_subject_get_int(caps.get_printer_has_chamber_sensor_subject()) == 0);
}

// 5. PrinterTemperatureState ignores chamber when sensor not configured
TEST_CASE("PrinterTemperatureState ignores chamber when sensor not configured",
          "[temperature][chamber]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);
    // Note: set_chamber_sensor_name() NOT called

    nlohmann::json status = {{"temperature_sensor chamber", {{"temperature", 45.3}}}};
    temp_state.update_from_status(status);

    // Should remain at initial value (0)
    REQUIRE(lv_subject_get_int(temp_state.get_chamber_temp_subject()) == 0);
}

// 6. Chamber assignment settings default to "auto"
TEST_CASE("Chamber assignment settings default to auto", "[settings][chamber]") {
    LVGLTestFixture fixture;

    auto& settings = helix::SettingsManager::instance();
    settings.init_subjects();

    REQUIRE(settings.get_chamber_heater_assignment() == "auto");
    REQUIRE(settings.get_chamber_sensor_assignment() == "auto");
}

// 7. Chamber assignment settings persist values
TEST_CASE("Chamber assignment settings persist values", "[settings][chamber]") {
    LVGLTestFixture fixture;

    auto& settings = helix::SettingsManager::instance();
    settings.init_subjects();

    settings.set_chamber_heater_assignment("heater_generic my_chamber");
    REQUIRE(settings.get_chamber_heater_assignment() == "heater_generic my_chamber");

    settings.set_chamber_sensor_assignment("temperature_sensor enclosure_bme");
    REQUIRE(settings.get_chamber_sensor_assignment() == "temperature_sensor enclosure_bme");

    settings.set_chamber_heater_assignment("none");
    REQUIRE(settings.get_chamber_heater_assignment() == "none");

    settings.set_chamber_sensor_assignment("auto");
    REQUIRE(settings.get_chamber_sensor_assignment() == "auto");
}

// 8. Manual chamber sensor override takes precedence over auto-detection
TEST_CASE("Manual chamber sensor override", "[chamber][override]") {
    LVGLTestFixture fixture;

    auto& settings = helix::SettingsManager::instance();
    settings.init_subjects();

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);

    PrinterDiscovery discovery;
    nlohmann::json objects = {
        "temperature_sensor chamber",
        "temperature_sensor enclosure_bme",
        "extruder",
        "heater_bed"};
    discovery.parse_objects(objects);

    settings.set_chamber_sensor_assignment("temperature_sensor enclosure_bme");

    std::string resolved_sensor = settings.get_chamber_sensor_assignment();
    if (resolved_sensor == "auto") {
        resolved_sensor = discovery.chamber_sensor_name();
    } else if (resolved_sensor == "none") {
        resolved_sensor = "";
    }
    temp_state.set_chamber_sensor_name(resolved_sensor);

    nlohmann::json status = {{"temperature_sensor enclosure_bme", {{"temperature", 33.7}}}};
    temp_state.update_from_status(status);

    REQUIRE(lv_subject_get_int(temp_state.get_chamber_temp_subject()) == 337);

    settings.set_chamber_sensor_assignment("auto");
}

// 9. "none" disables chamber sensor even when auto would detect
TEST_CASE("Chamber sensor 'none' disables detection", "[chamber][override]") {
    LVGLTestFixture fixture;

    auto& settings = helix::SettingsManager::instance();
    settings.init_subjects();

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);

    PrinterDiscovery discovery;
    nlohmann::json objects = {"temperature_sensor chamber", "extruder", "heater_bed"};
    discovery.parse_objects(objects);

    settings.set_chamber_sensor_assignment("none");

    std::string resolved_sensor = settings.get_chamber_sensor_assignment();
    if (resolved_sensor == "auto") {
        resolved_sensor = discovery.chamber_sensor_name();
    } else if (resolved_sensor == "none") {
        resolved_sensor = "";
    }
    temp_state.set_chamber_sensor_name(resolved_sensor);

    nlohmann::json status = {{"temperature_sensor chamber", {{"temperature", 45.3}}}};
    temp_state.update_from_status(status);

    REQUIRE(lv_subject_get_int(temp_state.get_chamber_temp_subject()) == 0);

    settings.set_chamber_sensor_assignment("auto");
}

// 10. Manual chamber assignment updates role badge
TEST_CASE("Manual chamber assignment updates sensor role", "[chamber][role]") {
    LVGLTestFixture fixture;

    auto& mgr = helix::sensors::TemperatureSensorManager::instance();
    mgr.init_subjects();

    std::vector<std::string> objects = {
        "temperature_sensor chamber_temp",
        "temperature_sensor enclosure_bme",
        "temperature_sensor mcu_temp"};
    mgr.discover(objects);

    auto sensors = mgr.get_sensors_sorted();
    auto it = std::find_if(sensors.begin(), sensors.end(),
        [](const auto& s) { return s.klipper_name == "temperature_sensor enclosure_bme"; });
    REQUIRE(it != sensors.end());
    REQUIRE(it->role == helix::sensors::TemperatureSensorRole::AUXILIARY);

    mgr.apply_chamber_sensor_override("temperature_sensor enclosure_bme");

    sensors = mgr.get_sensors_sorted();
    it = std::find_if(sensors.begin(), sensors.end(),
        [](const auto& s) { return s.klipper_name == "temperature_sensor enclosure_bme"; });
    REQUIRE(it != sensors.end());
    REQUIRE(it->role == helix::sensors::TemperatureSensorRole::CHAMBER);

    auto old_chamber = std::find_if(sensors.begin(), sensors.end(),
        [](const auto& s) { return s.klipper_name == "temperature_sensor chamber_temp"; });
    REQUIRE(old_chamber != sensors.end());
    REQUIRE(old_chamber->role != helix::sensors::TemperatureSensorRole::CHAMBER);
}

// 11. Full round trip: setting → override → temperature update
TEST_CASE("Chamber assignment full round trip", "[chamber][integration]") {
    LVGLTestFixture fixture;

    auto& settings = helix::SettingsManager::instance();
    settings.init_subjects();

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);

    PrinterDiscovery discovery;
    nlohmann::json objects = {
        "temperature_sensor mcu_temp",
        "temperature_sensor enclosure_bme",
        "heater_generic heated_enclosure",
        "extruder",
        "heater_bed"};
    discovery.parse_objects(objects);

    // No "chamber" in any name — auto-detect finds nothing
    REQUIRE_FALSE(discovery.has_chamber_sensor());
    REQUIRE_FALSE(discovery.has_chamber_heater());

    // User manually assigns
    settings.set_chamber_sensor_assignment("temperature_sensor enclosure_bme");
    settings.set_chamber_heater_assignment("heater_generic heated_enclosure");

    // Resolve (same logic as PrinterState::set_hardware)
    std::string sensor = settings.get_chamber_sensor_assignment();
    if (sensor == "auto") sensor = discovery.chamber_sensor_name();
    else if (sensor == "none") sensor = "";

    std::string heater = settings.get_chamber_heater_assignment();
    if (heater == "auto") heater = discovery.chamber_heater_name();
    else if (heater == "none") heater = "";

    temp_state.set_chamber_sensor_name(sensor);
    temp_state.set_chamber_heater_name(heater);

    // Verify heater temp + target work
    nlohmann::json status = {
        {"heater_generic heated_enclosure", {{"temperature", 55.2}, {"target", 60.0}}},
        {"temperature_sensor enclosure_bme", {{"temperature", 48.1}}}};
    temp_state.update_from_status(status);

    // Heater is preferred when both are set
    REQUIRE(lv_subject_get_int(temp_state.get_chamber_temp_subject()) == 552);
    REQUIRE(lv_subject_get_int(temp_state.get_chamber_target_subject()) == 600);

    // Clean up
    settings.set_chamber_sensor_assignment("auto");
    settings.set_chamber_heater_assignment("auto");
}
