// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file main.cpp
 * @brief Application entry point
 *
 * This file is intentionally minimal. All application logic is implemented
 * in the Application class (src/application/application.cpp).
 *
 * @see Application
 */

#include "application.h"
#include "helix_version.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fcntl.h>
#include <unistd.h>

// SDL2 redefines main → SDL_main via this header.
// On Android, the SDL Java activity loads libmain.so and calls SDL_main().
// Without this include, the symbol is missing and the app crashes on launch.
#ifdef HELIX_PLATFORM_ANDROID
#include <SDL.h>
#endif

// Log to stderr using only async-signal-safe-ish functions.
// spdlog may not be initialized yet or may be in a broken state.
static void log_fatal(const char* msg) {
    fprintf(stderr, "[FATAL] %s\n", msg);
    fflush(stderr);
}

// Write a minimal crash.txt for telemetry when an exception is caught.
// Uses the same key:value format as crash_handler's signal handler so
// CrashReporter can parse it on next startup.
static void write_exception_crash_file(const char* what) {
    static const char crash_path[] = "config/crash.txt";
    int fd = open(crash_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }

    // Build a minimal crash record. Use dprintf for simplicity since
    // we are NOT in a signal handler — heap/stdio are safe here.
    time_t now = time(nullptr);
    dprintf(fd, "signal:0\n");
    dprintf(fd, "name:EXCEPTION\n");
    dprintf(fd, "version:%s\n", HELIX_VERSION);
    dprintf(fd, "timestamp:%ld\n", static_cast<long>(now));
    dprintf(fd, "uptime:0\n");
    if (what) {
        dprintf(fd, "exception:%s\n", what);
    }
    close(fd);
}

// Called by std::terminate() — covers uncaught exceptions, joinable thread
// destruction, and other fatal C++ runtime errors. Logs what we can before
// the default terminate handler calls abort() (which triggers crash_handler).
static void terminate_handler() {
    // Guard against re-entrance (e.g. exception::what() throws)
    static bool entered = false;
    if (entered) {
        abort();
    }
    entered = true;

    // Check if there's a current exception we can inspect
    const char* what = nullptr;
    if (auto eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            what = e.what();
            fprintf(stderr, "[FATAL] Uncaught exception: %s\n", what);
            fflush(stderr);
        } catch (...) {
            log_fatal("Uncaught non-std::exception");
            what = "non-std::exception";
        }
    } else {
        log_fatal("std::terminate() called without active exception "
                  "(joinable thread destroyed? noexcept violation?)");
        what = "std::terminate without active exception";
    }

    // Write crash file BEFORE abort — abort triggers the signal handler which
    // would overwrite it without the exception message.
    write_exception_crash_file(what);
    _exit(1);
}

int main(int argc, char** argv) {
    std::set_terminate(terminate_handler);

    try {
        Application app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        fprintf(stderr, "[FATAL] Unhandled exception in Application: %s\n", e.what());
        fflush(stderr);
        write_exception_crash_file(e.what());
        return 1;
    } catch (...) {
        log_fatal("Unhandled non-std::exception in Application");
        write_exception_crash_file("non-std::exception");
        return 1;
    }
}
