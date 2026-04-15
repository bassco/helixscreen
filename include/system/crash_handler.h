// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file crash_handler.h
 * @brief Async-signal-safe crash handler for telemetry
 *
 * Installs signal handlers for SIGSEGV, SIGABRT, SIGBUS, SIGFPE.
 * On crash, writes a minimal crash file to disk using only
 * async-signal-safe functions (open, write, close, _exit).
 * NO heap allocation, NO mutex, NO spdlog in the signal handler.
 *
 * On next startup, TelemetryManager reads the crash file and
 * enqueues it as a telemetry event.
 *
 * Crash file format (line-oriented text, easy to parse):
 * @code
 * signal:11
 * name:SIGSEGV
 * version:0.9.6
 * timestamp:1707350400
 * uptime:3600
 * bt:0x0040abcd
 * bt:0x0040ef01
 * @endcode
 */

#include <string>

#include "hv/json.hpp"

namespace crash_handler {

/**
 * @brief Install crash signal handlers
 *
 * Registers handlers for SIGSEGV, SIGABRT, SIGBUS, SIGFPE via sigaction().
 * The path is copied into a static buffer so the signal handler can use it
 * without heap allocation.
 *
 * @param crash_file_path Path where crash data will be written on crash
 */
void install(const std::string& crash_file_path);

/**
 * @brief Uninstall crash signal handlers (restore defaults)
 *
 * Restores the default signal disposition for all handled signals.
 */
void uninstall();

/**
 * @brief Check if a crash file exists from a previous crash
 * @param crash_file_path Path to check
 * @return true if a crash file was found
 */
bool has_crash_file(const std::string& crash_file_path);

/**
 * @brief Read and parse a crash file into structured data
 *
 * Parses the line-oriented crash file and returns a JSON object
 * suitable for TelemetryManager's event queue. Returns null JSON
 * on parse failure.
 *
 * @param crash_file_path Path to the crash file
 * @return JSON object with crash event data, or null on failure
 */
nlohmann::json read_crash_file(const std::string& crash_file_path);

/**
 * @brief Delete the crash file after it has been processed
 * @param crash_file_path Path to the crash file to remove
 */
void remove_crash_file(const std::string& crash_file_path);

/**
 * @brief Write a synthetic crash file for testing the crash reporter UI
 *
 * Creates a realistic-looking crash.txt at the given path with a fake
 * SIGSEGV, current version, and sample backtrace addresses.
 *
 * @param crash_file_path Path where the mock crash file will be written
 */
void write_mock_crash_file(const std::string& crash_file_path);

/**
 * @brief Register a pointer to the current callback tag
 *
 * The UpdateQueue stores the tag of the currently executing callback in a
 * volatile pointer. Registering it here lets the crash signal handler read
 * and write it to crash.txt without depending on ui_update_queue.h.
 *
 * @param tag_ptr Pointer to the volatile const char* that holds the current tag
 */
void register_callback_tag_ptr(volatile const char* const* tag_ptr);

/**
 * @brief Breadcrumb ring buffer for crash-time activity context
 *
 * Records short, structured events into a fixed-size ring. On crash, the last
 * ~64 entries are dumped to crash.txt as `crumb:<ms> <category> <subject>`
 * lines. Producers can be called from any thread and from signal handlers
 * (though not intended to be the latter).
 *
 * Storage is entirely static (no heap). Writers are lock-free; readers
 * tolerate torn writes by requiring a complete timestamp.
 *
 * Use for high-signal transitions: panel navigation, modal show/hide, XML
 * component instantiation, style add/remove. Avoid for hot-path events.
 */
namespace breadcrumb {

/**
 * @brief Record an activity breadcrumb
 *
 * @param category Short tag (<= 7 chars): "nav", "modal", "xml", "style",
 *                 "frame", "evt". Truncated if longer.
 * @param subject  Object/panel/component name (<= 59 chars). Truncated.
 *
 * Both pointers may be nullptr (treated as empty). Safe to call from any
 * context including signal handlers — lock-free, no heap, no syscalls.
 */
void note(const char* category, const char* subject) noexcept;

/**
 * @brief Record an activity breadcrumb with a numeric detail
 *
 * Appends " <detail>" to the subject. Signal-safe.
 */
void note(const char* category, const char* subject, long detail) noexcept;

} // namespace breadcrumb

} // namespace crash_handler
