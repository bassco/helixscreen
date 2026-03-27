# AsyncLifetimeGuard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace 5 ad-hoc async callback safety patterns across ~15 modals/overlays/panels with a single unified `AsyncLifetimeGuard` utility, automatically integrated into `Modal` and `OverlayBase` base classes.

**Architecture:** A composable `AsyncLifetimeGuard` class using generation-counter tokens provides `defer()` (auto-guarded queue_update) and `token()` (manual checking). Base classes call `invalidate()` automatically on dismiss. All existing ad-hoc `alive_guard_`, `callback_guard_`, `test_generation_`, `alive_` patterns are removed.

**Tech Stack:** C++17, LVGL 9.5, Catch2 (tests), spdlog (logging)

**Spec:** `docs/devel/plans/2026-03-26-async-lifetime-guard-design.md`

---

### Task 1: Create AsyncLifetimeGuard Header + Unit Tests

**Files:**
- Create: `include/async_lifetime_guard.h`
- Create: `tests/unit/test_async_lifetime_guard.cpp`

- [ ] **Step 1: Write failing tests**

Create `tests/unit/test_async_lifetime_guard.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"

#include "async_lifetime_guard.h"
#include "ui_update_queue.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace helix;

TEST_CASE("LifetimeToken: valid when no invalidation", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    auto token = guard.token();
    REQUIRE_FALSE(token.expired());
    REQUIRE(static_cast<bool>(token));
}

TEST_CASE("LifetimeToken: expired after invalidate", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    auto token = guard.token();
    guard.invalidate();
    REQUIRE(token.expired());
    REQUIRE_FALSE(static_cast<bool>(token));
}

TEST_CASE("LifetimeToken: multiple tokens all expire on single invalidate", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    auto t1 = guard.token();
    auto t2 = guard.token();
    auto t3 = guard.token();
    guard.invalidate();
    REQUIRE(t1.expired());
    REQUIRE(t2.expired());
    REQUIRE(t3.expired());
}

TEST_CASE("LifetimeToken: generation cycling - old expired, new valid", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    auto old_token = guard.token();
    guard.invalidate();
    auto new_token = guard.token();

    REQUIRE(old_token.expired());
    REQUIRE_FALSE(new_token.expired());
}

TEST_CASE("LifetimeToken: double invalidate still works", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    auto t1 = guard.token();
    guard.invalidate();
    guard.invalidate();
    auto t2 = guard.token();

    REQUIRE(t1.expired());
    REQUIRE_FALSE(t2.expired());
}

TEST_CASE_METHOD(LVGLTestFixture, "AsyncLifetimeGuard: defer runs when valid", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    bool ran = false;
    guard.defer([&ran]() { ran = true; });

    helix::ui::UpdateQueue::instance().drain();
    REQUIRE(ran);
}

TEST_CASE_METHOD(LVGLTestFixture, "AsyncLifetimeGuard: defer skips when invalidated", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    bool ran = false;
    guard.defer([&ran]() { ran = true; });
    guard.invalidate();

    helix::ui::UpdateQueue::instance().drain();
    REQUIRE_FALSE(ran);
}

TEST_CASE_METHOD(LVGLTestFixture, "AsyncLifetimeGuard: defer with tag skips when invalidated", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    bool ran = false;
    guard.defer("test_tag", [&ran]() { ran = true; });
    guard.invalidate();

    helix::ui::UpdateQueue::instance().drain();
    REQUIRE_FALSE(ran);
}

TEST_CASE_METHOD(LVGLTestFixture, "AsyncLifetimeGuard: defer safe after guard destroyed", "[lifetime_guard]") {
    bool ran = false;
    {
        AsyncLifetimeGuard guard;
        guard.defer([&ran]() { ran = true; });
        // guard destroyed here — invalidate() called by destructor
    }

    helix::ui::UpdateQueue::instance().drain();
    // Callback should NOT run (destructor invalidated), and should NOT crash
    REQUIRE_FALSE(ran);
}

TEST_CASE_METHOD(LVGLTestFixture, "AsyncLifetimeGuard: defer after invalidate uses new generation", "[lifetime_guard]") {
    AsyncLifetimeGuard guard;
    bool first_ran = false;
    bool second_ran = false;

    guard.defer([&first_ran]() { first_ran = true; });
    guard.invalidate();
    guard.defer([&second_ran]() { second_ran = true; });

    helix::ui::UpdateQueue::instance().drain();
    REQUIRE_FALSE(first_ran);
    REQUIRE(second_ran);
}

TEST_CASE("AsyncLifetimeGuard: thread safety - concurrent token and invalidate", "[lifetime_guard][slow]") {
    AsyncLifetimeGuard guard;
    std::atomic<int> expired_count{0};
    std::atomic<int> valid_count{0};
    constexpr int iterations = 10000;

    std::thread checker([&]() {
        for (int i = 0; i < iterations; ++i) {
            auto token = guard.token();
            if (token.expired()) {
                expired_count.fetch_add(1);
            } else {
                valid_count.fetch_add(1);
            }
        }
    });

    std::thread invalidator([&]() {
        for (int i = 0; i < iterations; ++i) {
            guard.invalidate();
        }
    });

    checker.join();
    invalidator.join();

    // No crashes or data races is the assertion.
    // Both expired and valid tokens should have been observed.
    REQUIRE((expired_count.load() + valid_count.load()) == iterations);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[lifetime_guard]" -v`
Expected: Compilation error — `async_lifetime_guard.h` does not exist yet.

- [ ] **Step 3: Create the AsyncLifetimeGuard header**

Create `include/async_lifetime_guard.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <utility>

namespace helix {

/**
 * @brief Token capturing a generation snapshot from an AsyncLifetimeGuard.
 *
 * Lightweight, copyable value type designed to be captured in lambdas.
 * Safe to check after the owning object is destroyed — holds a shared_ptr
 * to the generation counter, not a pointer to the owner.
 *
 * Thread-safe: uses atomic load on the generation counter.
 */
class LifetimeToken {
  public:
    /// Returns true if the generation has advanced past the snapshot
    bool expired() const { return !gen_ || gen_->load() != snapshot_; }

    /// Explicit bool conversion — true if NOT expired
    explicit operator bool() const { return !expired(); }

  private:
    friend class AsyncLifetimeGuard;
    LifetimeToken(std::shared_ptr<std::atomic<uint64_t>> gen, uint64_t snapshot)
        : gen_(std::move(gen)), snapshot_(snapshot) {}

    std::shared_ptr<std::atomic<uint64_t>> gen_;
    uint64_t snapshot_;
};

/**
 * @brief Generation-counter-based async callback safety guard.
 *
 * Embed as a member in any class that receives callbacks from background
 * threads (WebSocket, HTTP, timers). Call invalidate() when the object is
 * dismissed/deactivated to expire all outstanding tokens.
 *
 * Integrated into Modal and OverlayBase — those base classes call
 * invalidate() automatically on hide()/cleanup(). Standalone panels
 * should call invalidate() in their destructor or cleanup method.
 *
 * ## Usage
 *
 * ### Common case: defer() (90% of uses)
 * @code
 * auto token = lifetime_.token();
 * api->fetch([this, token]() {
 *     if (token.expired()) return;
 *     lifetime_.defer([this]() {
 *         // Safe — runs on main thread, only if still valid
 *         update_ui();
 *     });
 * });
 * @endcode
 *
 * ### Raw token for non-queue callbacks
 * @code
 * auto token = lifetime_.token();
 * state_machine.on_complete([this, token]() {
 *     if (token.expired()) return;
 *     update_ui();  // Already on main thread
 * });
 * @endcode
 *
 * ### Cancel-and-retry (re-test scenario)
 * @code
 * lifetime_.invalidate();  // Cancel in-flight callbacks
 * auto token = lifetime_.token();
 * api->test([this, token]() { ... });
 * @endcode
 *
 * @note Non-copyable, non-movable. Destructor calls invalidate().
 */
class AsyncLifetimeGuard {
  public:
    AsyncLifetimeGuard() = default;
    ~AsyncLifetimeGuard() { invalidate(); }

    // Non-copyable, non-movable (embedded in owning object)
    AsyncLifetimeGuard(const AsyncLifetimeGuard&) = delete;
    AsyncLifetimeGuard& operator=(const AsyncLifetimeGuard&) = delete;
    AsyncLifetimeGuard(AsyncLifetimeGuard&&) = delete;
    AsyncLifetimeGuard& operator=(AsyncLifetimeGuard&&) = delete;

    /// Get a token capturing the current generation.
    /// Token expires when invalidate() is called.
    LifetimeToken token() const { return LifetimeToken(gen_, gen_->load()); }

    /// Expire all outstanding tokens. Called automatically by Modal::hide()
    /// and OverlayBase::cleanup(). Subclasses may call manually to cancel
    /// in-flight work without dismissing (e.g., re-test scenario).
    void invalidate() { gen_->fetch_add(1); }

    /// Queue work on the main thread, auto-guarded by the current generation.
    /// If invalidated before the callback runs, the callback is silently
    /// skipped. Safe to call from any thread.
    ///
    /// Captures the shared generation pointer (not 'this'), so the lambda
    /// is safe even if the owning object is destroyed before it fires.
    template<typename F>
    void defer(F&& fn) {
        auto gen = gen_;
        uint64_t snapshot = gen->load();
        helix::ui::queue_update([gen, snapshot, f = std::forward<F>(fn)]() {
            if (gen->load() != snapshot) {
                return;
            }
            f();
        });
    }

    /// Queue work with a descriptive tag for crash diagnostics.
    template<typename F>
    void defer(const char* tag, F&& fn) {
        auto gen = gen_;
        uint64_t snapshot = gen->load();
        helix::ui::queue_update(tag, [gen, snapshot, f = std::forward<F>(fn)]() {
            if (gen->load() != snapshot) {
                return;
            }
            f();
        });
    }

  private:
    std::shared_ptr<std::atomic<uint64_t>> gen_ =
        std::make_shared<std::atomic<uint64_t>>(0);
};

} // namespace helix
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[lifetime_guard]" -v`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/async_lifetime_guard.h tests/unit/test_async_lifetime_guard.cpp
git commit -m "feat: add AsyncLifetimeGuard for unified async callback safety"
```

---

### Task 2: Integrate into Modal Base Class

**Files:**
- Modify: `include/ui_modal.h` — add `lifetime_` member, include header
- Modify: `src/ui/ui_modal.cpp` — call `lifetime_.invalidate()` in `hide()`

- [ ] **Step 1: Add lifetime_ to Modal header**

In `include/ui_modal.h`, add the include near the top (with the other includes):

```cpp
#include "async_lifetime_guard.h"
```

Add `lifetime_` as a protected member, near the existing member declarations (after `parent_`):

```cpp
    /// Async callback safety. Automatically invalidated on hide().
    /// Subclasses use lifetime_.defer(...) or lifetime_.token() for
    /// bg-thread callbacks that need to touch UI.
    helix::AsyncLifetimeGuard lifetime_;
```

- [ ] **Step 2: Call invalidate() in Modal::hide()**

In `src/ui/ui_modal.cpp`, in the instance `Modal::hide()` method (the one starting around line 637), add `lifetime_.invalidate()` as the first action after the early-return checks, **before** `on_hide()`:

Find the line `spdlog::info("[{}] Hiding modal", get_name());` and add after it, before the `clear_user_data_recursive` block:

```cpp
    // Cancel all pending deferred callbacks from background threads
    lifetime_.invalidate();
```

- [ ] **Step 3: Build and run full test suite**

Run: `make -j && make test && make test-run`
Expected: Everything compiles and passes. No behavioral change — `lifetime_` is just added, not used by any subclass yet.

- [ ] **Step 4: Commit**

```bash
git add include/ui_modal.h src/ui/ui_modal.cpp
git commit -m "feat(modal): integrate AsyncLifetimeGuard into Modal base class"
```

---

### Task 3: Integrate into OverlayBase

**Files:**
- Modify: `include/overlay_base.h` — add `lifetime_` member, include header
- Modify: `src/ui/overlay_base.cpp` — call `lifetime_.invalidate()` in `cleanup()` and `on_deactivate()`

- [ ] **Step 1: Add lifetime_ to OverlayBase header**

In `include/overlay_base.h`, add include:

```cpp
#include "async_lifetime_guard.h"
```

Add `lifetime_` as a protected member near the existing member variables:

```cpp
    /// Async callback safety. Automatically invalidated on cleanup()/on_deactivate().
    /// Subclasses use lifetime_.defer(...) or lifetime_.token() for
    /// bg-thread callbacks that need to touch UI.
    helix::AsyncLifetimeGuard lifetime_;
```

- [ ] **Step 2: Call invalidate() in OverlayBase::cleanup() and on_deactivate()**

In `src/ui/overlay_base.cpp`, add `lifetime_.invalidate()` at the start of `cleanup()` (before setting `cleanup_called_`):

```cpp
void OverlayBase::cleanup() {
    spdlog::trace("[OverlayBase] cleanup() - {}", get_name());
    lifetime_.invalidate();
    cleanup_called_ = true;
    visible_ = false;
}
```

And at the start of `on_deactivate()`:

```cpp
void OverlayBase::on_deactivate() {
    spdlog::trace("[OverlayBase] on_deactivate() - {}", get_name());
    lifetime_.invalidate();
    visible_ = false;
}
```

- [ ] **Step 3: Build and run full test suite**

Run: `make -j && make test && make test-run`
Expected: Everything compiles and passes.

- [ ] **Step 4: Commit**

```bash
git add include/overlay_base.h src/ui/overlay_base.cpp
git commit -m "feat(overlay): integrate AsyncLifetimeGuard into OverlayBase"
```

---

### Task 4: Migrate ChangeHostModal

**Files:**
- Modify: `include/ui_change_host_modal.h`
- Modify: `src/ui/ui_change_host_modal.cpp`

**Remove these members from the header:**
- `std::shared_ptr<std::atomic<uint64_t>> test_generation_` (line ~83-84)
- `std::mutex saved_values_mutex_` (line ~85)
- `std::string saved_ip_` (line ~86)
- `std::string saved_port_` (line ~87)

**Remove these includes from the header** (if no longer needed after removing the above):
- `<atomic>` — check if still needed by other members
- `<mutex>` — only if `saved_values_mutex_` was the only mutex

- [ ] **Step 1: Update the header**

In `include/ui_change_host_modal.h`:

Remove the stale callback protection section (lines ~80-87):
```cpp
    // === Stale callback protection ===
    // Shared so bg thread lambdas can safely check generation without
    // dereferencing 'this' (which may be destroyed).
    std::shared_ptr<std::atomic<uint64_t>> test_generation_ =
        std::make_shared<std::atomic<uint64_t>>(0);
    std::mutex saved_values_mutex_;
    std::string saved_ip_;
    std::string saved_port_;
```

Keep `saved_ip_` and `saved_port_` only if they're used outside of the generation-guard pattern. Check: they're stored in `handle_test_connection()` and read in `handle_save()` — but `handle_save()` reads from subjects directly. So they can be removed.

Remove includes no longer needed: `<atomic>`, `<mutex>`.

- [ ] **Step 2: Rewrite handle_test_connection()**

In `src/ui/ui_change_host_modal.cpp`, replace the `handle_test_connection()` method. The key changes:
- Replace `++(*test_generation_)` with `lifetime_.invalidate()`
- Replace generation captures with `lifetime_.token()`
- Replace `async_call(guard_widget, ...)` with `lifetime_.defer(...)`
- Remove `saved_values_mutex_` usage

```cpp
void ChangeHostModal::handle_test_connection() {
    const char* ip = lv_subject_get_string(&host_ip_subject_);
    std::string port_clean = sanitize_port(lv_subject_get_string(&host_port_subject_));

    spdlog::debug("[ChangeHostModal] Test connection: {}:{}", ip ? ip : "", port_clean);

    // Reset validation state
    lv_subject_set_int(&validated_subject_, 0);

    // Validate inputs
    if (!ip || strlen(ip) == 0) {
        set_status(nullptr, nullptr, "Please enter a host address");
        return;
    }

    if (!is_valid_ip_or_hostname(ip)) {
        set_status("icon_xmark_circle", "danger", "Invalid IP address or hostname");
        return;
    }

    if (!is_valid_port(port_clean)) {
        set_status("icon_xmark_circle", "danger", "Invalid port (must be 1-65535)");
        return;
    }

    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        set_status("icon_xmark_circle", "danger", "Client not available");
        return;
    }

    EmergencyStopOverlay::instance().suppress_recovery_dialog(RecoverySuppression::NORMAL);
    client->disconnect();

    // Cancel any in-flight test callbacks, get fresh token
    lifetime_.invalidate();
    auto token = lifetime_.token();

    // Set UI to testing state
    lv_subject_set_int(&testing_subject_, 1);
    set_status("icon_question_circle", "text_muted", "Testing connection...");

    client->set_connection_timeout(5000);

    std::string ws_url = "ws://" + std::string(ip) + ":" + port_clean + "/websocket";

    int result = client->connect(
        ws_url.c_str(),
        [this, token]() {
            if (token.expired()) {
                spdlog::debug("[ChangeHostModal] Ignoring stale success callback");
                return;
            }
            on_test_success();
        },
        [this, token]() {
            if (token.expired()) {
                spdlog::debug("[ChangeHostModal] Ignoring stale failure callback");
                return;
            }
            on_test_failure();
        });

    client->setReconnect(nullptr);

    if (result != 0) {
        spdlog::error("[ChangeHostModal] Failed to initiate test connection: {}", result);
        set_status("icon_xmark_circle", "danger", "Error starting connection test");
        lv_subject_set_int(&testing_subject_, 0);
    }
}
```

- [ ] **Step 3: Rewrite on_test_success() and on_test_failure()**

Remove the `lv_obj_t* guard_widget` parameter — no longer needed.

```cpp
void ChangeHostModal::on_test_success() {
    spdlog::info("[ChangeHostModal] Test connection successful");

    lifetime_.defer([this]() {
        if (!is_visible())
            return;

        set_status("icon_check_circle", "success", "Connection successful!");
        lv_subject_set_int(&testing_subject_, 0);
        lv_subject_set_int(&validated_subject_, 1);

        spdlog::info("[ChangeHostModal] Test passed, Save button enabled");
    });
}

void ChangeHostModal::on_test_failure() {
    spdlog::warn("[ChangeHostModal] Test connection failed");

    lifetime_.defer([this]() {
        if (!is_visible())
            return;

        set_status("icon_xmark_circle", "danger", "Connection failed");
        lv_subject_set_int(&testing_subject_, 0);

        spdlog::debug("[ChangeHostModal] Test failed, keeping Save disabled");
    });
}
```

Update the header declarations to remove the `lv_obj_t* guard_widget` parameter:

```cpp
    void on_test_success();
    void on_test_failure();
```

- [ ] **Step 4: Simplify on_hide() and handle_cancel()**

In `on_hide()`, remove the manual `++(*test_generation_)` — base class handles invalidation:

```cpp
void ChangeHostModal::on_hide() {
    // Base class already called lifetime_.invalidate()

    // Clear active instance
    active_instance_ = nullptr;

    // Remove observers NOW rather than relying on auto-removal when dialog
    // widget is deleted after exit animation.
    host_ip_observer_.reset();
    host_port_observer_.reset();

    spdlog::debug("[ChangeHostModal] on_hide");
}
```

In `handle_cancel()`, remove `++(*test_generation_)` — base class handles it via `hide()`:

```cpp
void ChangeHostModal::handle_cancel() {
    spdlog::debug("[ChangeHostModal] Cancel clicked");

    hide();

    if (completion_callback_) {
        completion_callback_(false);
    }
}
```

- [ ] **Step 5: Remove unused includes from cpp**

If `<mutex>` was only used for `saved_values_mutex_`, remove it from `ui_change_host_modal.cpp`.

- [ ] **Step 6: Build and run tests**

Run: `make -j && make test && make test-run`
Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/ui_change_host_modal.h src/ui/ui_change_host_modal.cpp
git commit -m "refactor(modal): migrate ChangeHostModal to AsyncLifetimeGuard"
```

---

### Task 5: Migrate AmsEditModal

**Files:**
- Modify: `include/ui_ams_edit_modal.h`
- Modify: `src/ui/ui_ams_edit_modal.cpp`

**Remove:** `std::shared_ptr<bool> callback_guard_` (line ~139 in header)

- [ ] **Step 1: Remove callback_guard_ from header**

In `include/ui_ams_edit_modal.h`, remove:
```cpp
    std::shared_ptr<bool> callback_guard_;
```

- [ ] **Step 2: Replace callback_guard_ usage in cpp**

In `src/ui/ui_ams_edit_modal.cpp`:

In `on_show()`, remove:
```cpp
    callback_guard_ = std::make_shared<bool>(true);
```

In `on_hide()`, remove:
```cpp
    callback_guard_.reset();
```

For every async callback that captures `callback_guard_` as a `weak_ptr`, replace with `lifetime_.token()`. The pattern is:

**Before:**
```cpp
std::weak_ptr<bool> guard = callback_guard_;
api->fetch([this, guard]() {
    if (guard.expired()) return;
    helix::ui::queue_update([this, guard]() {
        if (guard.expired()) return;
        // do work
    });
});
```

**After:**
```cpp
auto token = lifetime_.token();
api->fetch([this, token]() {
    if (token.expired()) return;
    lifetime_.defer([this]() {
        // do work
    });
});
```

Search the cpp file for all occurrences of `callback_guard_` and `guard.expired()` and replace each with the token pattern.

- [ ] **Step 3: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 4: Commit**

```bash
git add include/ui_ams_edit_modal.h src/ui/ui_ams_edit_modal.cpp
git commit -m "refactor(modal): migrate AmsEditModal to AsyncLifetimeGuard"
```

---

### Task 6: Migrate JobQueueModal

**Files:**
- Modify: `include/ui_job_queue_modal.h`
- Modify: `src/ui/ui_job_queue_modal.cpp`

**Remove:** `std::shared_ptr<bool> alive_guard_` (line ~55 in header)

- [ ] **Step 1: Remove alive_guard_ from header**

In `include/ui_job_queue_modal.h`, remove:
```cpp
    std::shared_ptr<bool> alive_guard_ = std::make_shared<bool>(true);
```

- [ ] **Step 2: Replace alive_guard_ usage in cpp**

In `src/ui/ui_job_queue_modal.cpp`:

In destructor, remove:
```cpp
    if (alive_guard_) *alive_guard_ = false;
```

In `on_hide()`, remove:
```cpp
    if (alive_guard_) *alive_guard_ = false;
    alive_guard_ = std::make_shared<bool>(true);
```

For async callbacks, replace the alive_guard_ pattern:

**Before (e.g., in toggle_queue):**
```cpp
auto guard = alive_guard_;
api->call([this, guard]() {
    helix::ui::queue_update([this, guard]() {
        if (!*guard) return;
        // do work
    });
});
```

**After:**
```cpp
auto token = lifetime_.token();
api->call([this, token]() {
    if (token.expired()) return;
    lifetime_.defer([this]() {
        // do work
    });
});
```

Also replace the `RebuildCtx` struct pattern (used with `lv_async_call`):

**Before:**
```cpp
struct RebuildCtx {
    std::weak_ptr<bool> alive;
    JobQueueModal* modal;
};
auto* ctx = new RebuildCtx{alive_guard_, this};
lv_async_call([](void* data) {
    auto* ctx = static_cast<RebuildCtx*>(data);
    auto guard = ctx->alive.lock();
    if (!guard || !*guard) { delete ctx; return; }
    ctx->modal->rebuild_list();
    delete ctx;
}, ctx);
```

**After:**
```cpp
lifetime_.defer([this]() {
    rebuild_list();
});
```

Search for all occurrences of `alive_guard_`, `guard`, `*guard`, and the `RebuildCtx` struct and replace each.

- [ ] **Step 3: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 4: Commit**

```bash
git add include/ui_job_queue_modal.h src/ui/ui_job_queue_modal.cpp
git commit -m "refactor(modal): migrate JobQueueModal to AsyncLifetimeGuard"
```

---

### Task 7: Migrate SpoolmanEditModal

**Files:**
- Modify: `include/ui_spoolman_edit_modal.h`
- Modify: `src/ui/ui_spoolman_edit_modal.cpp`

**Remove:** `std::shared_ptr<bool> callback_guard_` (line ~81 in header)

- [ ] **Step 1: Remove callback_guard_ from header**

Remove: `std::shared_ptr<bool> callback_guard_;`

- [ ] **Step 2: Replace callback_guard_ usage in cpp**

In `on_show()`, remove: `callback_guard_ = std::make_shared<bool>(true);`
In `on_hide()`, remove: `callback_guard_.reset();`

Replace all `weak_ptr<bool> guard = callback_guard_` + `guard.expired()` checks with `lifetime_.token()` + `token.expired()` + `lifetime_.defer(...)`.

- [ ] **Step 3: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 4: Commit**

```bash
git add include/ui_spoolman_edit_modal.h src/ui/ui_spoolman_edit_modal.cpp
git commit -m "refactor(modal): migrate SpoolmanEditModal to AsyncLifetimeGuard"
```

---

### Task 8: Migrate DebugBundleModal

**Files:**
- Modify: `include/ui_debug_bundle_modal.h`
- Modify: `src/ui/ui_debug_bundle_modal.cpp`

**Remove:** `std::shared_ptr<bool> alive_` (line ~58 in header)

- [ ] **Step 1: Remove alive_ from header**

Remove: `std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);`

- [ ] **Step 2: Replace alive_ usage in cpp**

In destructor, remove: `*alive_ = false;`

Replace any `alive_` guard checks in callbacks with `lifetime_.token()` / `lifetime_.defer()`.

- [ ] **Step 3: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 4: Commit**

```bash
git add include/ui_debug_bundle_modal.h src/ui/ui_debug_bundle_modal.cpp
git commit -m "refactor(modal): migrate DebugBundleModal to AsyncLifetimeGuard"
```

---

### Task 9: Migrate TimelapseInstallOverlay

**Files:**
- Modify: `include/ui_overlay_timelapse_install.h`
- Modify: `src/ui/ui_overlay_timelapse_install.cpp`

**Remove:** `std::shared_ptr<bool> alive_guard_` (line ~113 in header)

- [ ] **Step 1: Remove alive_guard_ from header**

Remove: `std::shared_ptr<bool> alive_guard_ = std::make_shared<bool>(true);`

- [ ] **Step 2: Replace alive_guard_ usage in cpp**

In `cleanup()`, remove:
```cpp
    if (alive_guard_) *alive_guard_ = false;
    alive_guard_ = std::make_shared<bool>(true);
```

Replace all `alive_guard_` captures and checks in async callbacks (API calls for install/progress/complete) with `lifetime_.token()` + `lifetime_.defer()`.

- [ ] **Step 3: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 4: Commit**

```bash
git add include/ui_overlay_timelapse_install.h src/ui/ui_overlay_timelapse_install.cpp
git commit -m "refactor(overlay): migrate TimelapseInstallOverlay to AsyncLifetimeGuard"
```

---

### Task 10: Migrate TimelapseVideosOverlay

**Files:**
- Modify: `include/ui_overlay_timelapse_videos.h`
- Modify: `src/ui/ui_overlay_timelapse_videos.cpp`

**Remove:**
- `std::shared_ptr<std::atomic<bool>> alive_` (line ~74)
- `std::atomic<uint32_t> nav_generation_` (line ~75)
- `std::atomic<uint32_t> thumb_generation_` (line ~76)

- [ ] **Step 1: Remove ad-hoc guard members from header**

Remove all three members listed above.

- [ ] **Step 2: Replace usage in cpp**

In constructor, remove alive_ initialization.

In `on_activate()`, replace `nav_generation_.fetch_add(1)` with `lifetime_.invalidate()` (if it's being used to cancel stale nav callbacks).

In `on_deactivate()`, remove `nav_generation_.fetch_add(1)` — base class handles invalidation.

In `cleanup()`, remove `alive_->store(false)` — base class handles invalidation.

Replace all `alive_` captures and `alive->load()` checks with `lifetime_.token()` + `token.expired()` + `lifetime_.defer()`.

For `nav_generation_` and `thumb_generation_`, replace with `lifetime_.token()` — the generation counter in `AsyncLifetimeGuard` serves the same purpose. If these generation counters are used for **separate** concerns (nav vs thumbnails), and you need them to invalidate independently, keep `lifetime_` for the main lifecycle and add a second `AsyncLifetimeGuard nav_lifetime_` for navigation-specific invalidation. Check whether both are invalidated together (in which case one guard suffices) or independently (need two).

- [ ] **Step 3: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 4: Commit**

```bash
git add include/ui_overlay_timelapse_videos.h src/ui/ui_overlay_timelapse_videos.cpp
git commit -m "refactor(overlay): migrate TimelapseVideosOverlay to AsyncLifetimeGuard"
```

---

### Task 11: Migrate QrScannerOverlay

**Files:**
- Modify: `include/ui_overlay_qr_scanner.h`
- Modify: `src/ui/ui_overlay_qr_scanner.cpp`

**Remove:** `std::shared_ptr<bool> alive_` (line ~137 in header)

- [ ] **Step 1: Remove alive_ from header**

Remove: `std::shared_ptr<bool> alive_ = std::make_shared<bool>(false);`

- [ ] **Step 2: Replace alive_ usage in cpp**

In `on_activate()`, remove: `*alive_ = true;`
In `on_deactivate()`, remove: `*alive_ = false;`

Replace all `weak_ptr<bool>(alive_)` captures and lock/check patterns with `lifetime_.token()` + `token.expired()`.

For timer callbacks that use the alive guard, use `lifetime_.token()`:

**Before:**
```cpp
auto w = std::weak_ptr<bool>(alive_);
lv_timer_create([](lv_timer_t* t) {
    auto* data = static_cast<TimerData*>(lv_timer_get_user_data(t));
    if (auto alive = data->alive.lock(); alive && *alive) {
        // scan
    }
}, interval, timer_data);
```

**After:** Capture a `LifetimeToken` in the timer data struct instead of `weak_ptr<bool>`:
```cpp
struct TimerData {
    helix::LifetimeToken token;
    QrScannerOverlay* self;
};
auto* data = new TimerData{lifetime_.token(), this};
lv_timer_create([](lv_timer_t* t) {
    auto* data = static_cast<TimerData*>(lv_timer_get_user_data(t));
    if (data->token.expired()) return;
    // scan
}, interval, data);
```

- [ ] **Step 3: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 4: Commit**

```bash
git add include/ui_overlay_qr_scanner.h src/ui/ui_overlay_qr_scanner.cpp
git commit -m "refactor(overlay): migrate QrScannerOverlay to AsyncLifetimeGuard"
```

---

### Task 12: Migrate Panels (InputShaper, BeltTension, PrintSelectDetailView)

**Files:**
- Modify: `include/ui_panel_input_shaper.h` + `src/ui/ui_panel_input_shaper.cpp`
- Modify: `include/ui_panel_belt_tension.h` + `src/ui/ui_panel_belt_tension.cpp`
- Modify: `include/ui_print_select_detail_view.h` + `src/ui/ui_print_select_detail_view.cpp`

These panels don't inherit from Modal or OverlayBase, so they need to:
1. Add `helix::AsyncLifetimeGuard lifetime_;` as a private member
2. Remove their ad-hoc `alive_` members
3. Replace usage in async callbacks
4. The guard's destructor auto-calls `invalidate()`, plus the existing destructor code that sets `alive_->store(false)` is replaced

- [ ] **Step 1: Migrate InputShaperPanel**

In header, remove: `std::shared_ptr<std::atomic<bool>> alive_` (line ~356)
Add: `helix::AsyncLifetimeGuard lifetime_;` and `#include "async_lifetime_guard.h"`

In cpp destructor, remove: `alive_->store(false);`
Replace all `alive_` captures + `alive_->load()` checks in async calibration callbacks with `lifetime_.token()` + `token.expired()` + `lifetime_.defer()`.

If the panel also has `calibration_gen_` for cancel-and-retry, check whether `lifetime_.invalidate()` can replace it. If they serve different purposes (panel lifetime vs calibration session), keep `lifetime_` for general callbacks and use a separate `AsyncLifetimeGuard calibration_lifetime_` for calibration-specific invalidation.

- [ ] **Step 2: Migrate BeltTensionPanel**

In header, remove: `std::shared_ptr<std::atomic<bool>> alive_` (line ~174)
Add: `helix::AsyncLifetimeGuard lifetime_;` and include.

In cpp destructor, remove: `alive_->store(false);`
Replace all `alive_` captures + `alive_->load()` checks with token pattern.

- [ ] **Step 3: Migrate PrintSelectDetailView**

In header, remove: `std::shared_ptr<std::atomic<bool>> alive_` (line ~368)
Add: `helix::AsyncLifetimeGuard lifetime_;` and include.

In cpp destructor, remove: `alive_->store(false);`
Replace all `alive_` captures + `alive_->load()` checks with token pattern.

- [ ] **Step 4: Build and run tests**

Run: `make -j && make test && make test-run`

- [ ] **Step 5: Commit**

```bash
git add include/ui_panel_input_shaper.h src/ui/ui_panel_input_shaper.cpp \
        include/ui_panel_belt_tension.h src/ui/ui_panel_belt_tension.cpp \
        include/ui_print_select_detail_view.h src/ui/ui_print_select_detail_view.cpp
git commit -m "refactor(panel): migrate InputShaper, BeltTension, PrintSelectDetail to AsyncLifetimeGuard"
```

---

### Task 13: Verify No Remaining Ad-Hoc Patterns

- [ ] **Step 1: Search for remaining ad-hoc guard patterns**

Run these grep commands to find any remaining instances:

```bash
# Should find ZERO matches in src/ and include/ (except async_lifetime_guard.h itself)
grep -rn "callback_guard_\|alive_guard_" --include="*.h" --include="*.cpp" src/ include/
grep -rn "shared_ptr<bool>" --include="*.h" --include="*.cpp" src/ include/ | grep -v "SubjectLifetime\|test_\|catch_"
grep -rn "shared_ptr<std::atomic<bool>>" --include="*.h" --include="*.cpp" src/ include/
```

Expected: No matches related to async callback safety. `shared_ptr<bool>` may appear in other contexts (SubjectLifetime) — that's fine.

- [ ] **Step 2: Search for remaining guard check patterns in callbacks**

```bash
grep -rn "guard.expired()\|!.*guard\b\|alive.*load()\|alive.*store" --include="*.cpp" src/ui/
```

Expected: No matches from migrated files.

- [ ] **Step 3: Commit (if any stragglers found and fixed)**

---

### Task 14: Update Documentation

**Files:**
- Modify: `docs/devel/ARCHITECTURE.md`
- Modify: `docs/devel/MODAL_SYSTEM.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Add AsyncLifetimeGuard section to ARCHITECTURE.md**

Find the "No `safe_delete()` Inside UpdateQueue Callbacks" section (around line 1132) and add a new section after it:

```markdown
### Async Callback Safety: AsyncLifetimeGuard

Background-thread callbacks (WebSocket, HTTP, timers) that need to update UI must be guarded against the owning modal/overlay/panel being dismissed before the callback fires.

**Use `AsyncLifetimeGuard`** — a generation-counter-based utility that automatically cancels stale callbacks:

```cpp
// In your class:
helix::AsyncLifetimeGuard lifetime_;  // Already provided by Modal and OverlayBase base classes

// In a background-thread callback:
auto token = lifetime_.token();
api->fetch([this, token]() {
    if (token.expired()) return;          // Owner was dismissed
    lifetime_.defer([this]() {            // Queue to main thread, auto-guarded
        update_ui();
    });
});

// Cancel-and-retry (e.g., re-test connection):
lifetime_.invalidate();                   // Expire all outstanding tokens
auto token = lifetime_.token();           // Fresh token for new operation
```

**Key properties:**
- `Modal::hide()` and `OverlayBase::cleanup()`/`on_deactivate()` call `invalidate()` automatically
- `defer()` queues work via `queue_update()`, skipping it if invalidated before execution
- `token()` returns a `LifetimeToken` for manual checking in non-queue callbacks
- Safe after owner destruction: tokens hold a `shared_ptr` to the generation counter, not to the owner
- Subclasses may call `invalidate()` manually for cancel-and-retry scenarios

**Do NOT use these deprecated patterns:**
- ~~`shared_ptr<bool> callback_guard_`~~ / ~~`alive_guard_`~~
- ~~`shared_ptr<atomic<bool>> alive_`~~
- ~~`shared_ptr<atomic<uint64_t>> test_generation_`~~
- ~~`weak_ptr<bool>` for callback safety~~
- ~~`async_call(guard_widget, cb, data)` for modal/overlay callback guards~~
```

- [ ] **Step 2: Add async callback safety section to MODAL_SYSTEM.md**

Add a section titled "Async Callback Safety" that covers:
- The problem (bg thread outlives modal)
- The solution (lifetime_ is built into Modal)
- Code example using `lifetime_.defer()`
- Note that `on_hide()` does NOT need to call `invalidate()` — base class does it

- [ ] **Step 3: Update CLAUDE.md Threading & Lifecycle section**

In the root `CLAUDE.md`, in the "Threading & Lifecycle" section, add a brief reference:

```markdown
**Async callback safety:** Use `lifetime_.defer(...)` (built into `Modal` and `OverlayBase`) to queue
bg-thread callbacks safely. See `include/async_lifetime_guard.h` for API. Do NOT use ad-hoc
`alive_guard_`, `callback_guard_`, or `shared_ptr<bool>` patterns.
```

- [ ] **Step 4: Commit**

```bash
git add docs/devel/ARCHITECTURE.md docs/devel/MODAL_SYSTEM.md CLAUDE.md
git commit -m "docs: add AsyncLifetimeGuard best practices, deprecate ad-hoc patterns"
```

---

### Task 15: Final Validation

- [ ] **Step 1: Full build**

Run: `make -j`
Expected: Clean compile, no warnings.

- [ ] **Step 2: Full test suite**

Run: `make test && make test-run`
Expected: All tests pass, including new `[lifetime_guard]` tests.

- [ ] **Step 3: Run with mock printer**

Run: `./build/bin/helix-screen --test -vv`
Navigate to panels that use modals (settings → change host, AMS edit, etc.) and verify no crashes or log errors.

- [ ] **Step 4: Verify grep audit clean**

Re-run the grep commands from Task 13 to confirm zero remaining ad-hoc patterns.
