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
 * @brief Intentionally SIGSEGV through a deep call chain, for verifying the
 *        signal handler's unwind path on real hardware.
 *
 * Call chain (distinct function names, `__attribute__((noinline))`):
 *   trigger_test_crash() -> crash_level_1() -> crash_level_2() ->
 *   crash_level_3() -> crash_level_4() -> *(volatile int*)0 = 1;
 *
 * Use via `HELIX_CRASH_TEST=1` env var. The generated crash.txt should
 * resolve a live FP-walk chain that includes all five symbols. Absence of
 * any of them means the walker broke somewhere. Only intended for
 * signal-handler development — never wire this into production paths.
 */
[[noreturn]] void trigger_test_crash();

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
 * @brief Register a ring of recently-completed callback tags
 *
 * Lets the signal handler emit `queue_prev:` / `queue_prev2:` / ... lines
 * naming the most recently completed callbacks when the crash happens
 * AFTER a queued callback finished. Useful for pinning heap corruption
 * that detonates on the next main-thread malloc, or cascades a few
 * callbacks later.
 *
 * The ring is walked newest→oldest starting at `(*next - 1) % capacity`.
 * Producer writes slot `*next % capacity`, then stores `*next + 1` back.
 * All three parameters are read by the signal handler, so their storage
 * must outlive any possible crash (typically static).
 *
 * @param ring     Pointer to the tag-pointer array (string literals)
 * @param capacity Number of slots in the ring (power-of-two recommended)
 * @param next     Pointer to the write-index counter (monotonically increasing)
 */
void register_previous_tag_ring(volatile const char* const* ring,
                                unsigned int capacity,
                                volatile const unsigned int* next);

/**
 * @brief Record the LVGL event currently being dispatched
 *
 * Maintains a volatile record of the innermost lv_obj_t under dispatch.
 * Patched into LVGL's event_send_core() via patches/lvgl_event_crash_hook
 * so every event dispatch (including internal ones like REFR_EXT_DRAW_SIZE
 * and LAYOUT_CHANGED) updates this slot. On crash, the signal handler
 * dumps the last-seen target as event_target and event_code lines.
 *
 * This gives crashes in LVGL layout/draw code the pointer + event code of
 * the widget that was being processed — enough to cross-reference with
 * breadcrumbs (e.g. last XML component instantiated) and name the widget.
 *
 * Signal-safe: two volatile writes, no locks, no allocations.
 */
void set_current_event(const void* target, const void* original_target,
                       unsigned int code) noexcept;

/**
 * @brief Refresh the cached heap snapshot
 *
 * Call from the main loop every ~10s. Captures /proc/self/statm RSS,
 * glibc mallinfo (when available), and lv_mem_monitor() into a static
 * buffer. On crash, the signal handler dumps the most recent snapshot
 * without calling any of these non-async-signal-safe functions.
 *
 * Cheap enough (~1 µs) to call every frame if desired, but 10s is plenty.
 */
void refresh_heap_snapshot() noexcept;

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
 * Both pointers may be nullptr (treated as empty). Lock-free, no heap, no
 * syscalls.
 *
 * THREADING CONTRACT: single-producer (LVGL/UI thread only). Do NOT call
 * from signal handlers or from background threads. Two concurrent producers
 * that hash to the same slot (possible on ring wrap) would interleave
 * byte-level writes into `category`/`subject`; readers would see a
 * consistent `ts_ms` but garbled text. All real producers today run on
 * the LVGL thread (navigation, modals, frame tick, boot).
 */
void note(const char* category, const char* subject) noexcept;

/**
 * @brief Record an activity breadcrumb with a numeric detail
 *
 * Appends " <detail>" to the subject. Same single-producer / LVGL-thread
 * contract as the two-arg overload.
 */
void note(const char* category, const char* subject, long detail) noexcept;

/**
 * @brief Dump the ring buffer to a file descriptor as crumb: lines
 *
 * Writes the oldest-to-newest breadcrumbs as `crumb:<ms> <cat> <subj>\n`
 * lines using only async-signal-safe primitives. Used by the crash signal
 * handler; exposed here so tests can verify ring state by dumping to a
 * pipe and parsing the output.
 */
void dump_to_fd(int fd) noexcept;

} // namespace breadcrumb

} // namespace crash_handler

// C-ABI bridge for LVGL (C source) to record the current event target. Calls
// crash_handler::set_current_event() — same semantics, usable from C.
extern "C" void helix_crash_note_event(const void* target,
                                       const void* original_target,
                                       unsigned int code);
