# AsyncLifetimeGuard: Unified Async Callback Safety

**Date:** 2026-03-26
**Status:** Design
**Triggered by:** Crash #549 (SIGSEGV in ChangeHostModal::on_test_success deferred callback)

---

## Problem

Background-thread callbacks (WebSocket responses, HTTP fetches, timers) often need to update UI on the main thread via `queue_update()`. If the modal/overlay/panel is dismissed before the deferred work runs, the callback accesses freed memory → SIGSEGV.

The codebase has **5 different ad-hoc patterns** solving this same problem across ~15 modals and overlays:

| Pattern | Example | Issue |
|---------|---------|-------|
| `shared_ptr<bool> callback_guard_` | AmsEditModal | Manual, easy to forget |
| `shared_ptr<atomic<bool>> alive_` | TimelapseVideosOverlay | Manual, slightly different API |
| `shared_ptr<atomic<uint64_t>> generation_` | ChangeHostModal | Correct but hand-rolled |
| `weak_ptr<bool>` with `.lock()` | AmsEditModal (Spoolman) | Yet another variation |
| No protection at all | InfoQrModal, PinEntryModal | Crashes waiting to happen |

Every new modal rediscovers the problem and adds its own variation. The ChangeHostModal had four layers of protection and still crashed because the generation check wasn't carried through to the deferred lambda.

## Goal

1. **One canonical pattern** — `AsyncLifetimeGuard` — that replaces all five ad-hoc mechanisms.
2. **Automatic invalidation** — base classes (`Modal`, `OverlayBase`) invalidate on dismiss; subclasses can't forget.
3. **Simple API** — `lifetime_.defer(...)` for the common case, `lifetime_.token()` for complex flows.
4. **Documentation** — ARCHITECTURE.md and MODAL_SYSTEM.md updated with the canonical pattern, removing references to deprecated approaches.

## Non-Goals

- Replacing `ObserverGuard` / `SubjectLifetime` — those handle observer lifecycle, not callback safety.
- Replacing `ScopedFreeze` — that handles drain+destroy race windows.
- Replacing deferred self-deletion patterns (InfoQrModal's `async_call(delete)`) — that's ownership, not callback safety. Though modals using `defer()` may simplify those too.
- Changing the threading model or UpdateQueue.

---

## Design

### Core: `LifetimeToken` (value type, captured in lambdas)

```cpp
// include/async_lifetime_guard.h

class LifetimeToken {
  public:
    /// Returns true if the generation has advanced past the snapshot
    bool expired() const { return !gen_ || gen_->load() != snapshot_; }

    /// Implicit bool conversion for concise checks
    explicit operator bool() const { return !expired(); }

  private:
    friend class AsyncLifetimeGuard;
    LifetimeToken(std::shared_ptr<std::atomic<uint64_t>> gen, uint64_t snapshot)
        : gen_(std::move(gen)), snapshot_(snapshot) {}

    std::shared_ptr<std::atomic<uint64_t>> gen_;
    uint64_t snapshot_;
};
```

**Properties:**
- Copyable, lightweight (shared_ptr + uint64_t).
- Safe to check after the owning object is destroyed (holds shared_ptr to the generation counter, not a pointer to the owner).
- Thread-safe (`atomic<uint64_t>` load).

### Core: `AsyncLifetimeGuard` (owned by the protected object)

```cpp
class AsyncLifetimeGuard {
  public:
    AsyncLifetimeGuard() = default;
    ~AsyncLifetimeGuard() { invalidate(); }

    // Non-copyable, non-movable (embedded in owning object)
    AsyncLifetimeGuard(const AsyncLifetimeGuard&) = delete;
    AsyncLifetimeGuard& operator=(const AsyncLifetimeGuard&) = delete;

    /// Get a token capturing the current generation.
    /// Token expires when invalidate() is called.
    LifetimeToken token() const {
        return LifetimeToken(gen_, gen_->load());
    }

    /// Expire all outstanding tokens. Called automatically by Modal::hide()
    /// and OverlayBase::cleanup(). Subclasses may call manually to cancel
    /// in-flight work without dismissing (e.g., re-test scenario).
    void invalidate() { gen_->fetch_add(1); }

    /// Queue work on the main thread, auto-guarded by the current generation.
    /// If the guard is invalidated before the callback runs, the callback is
    /// silently skipped. Safe to call from any thread.
    ///
    /// Captures the shared generation pointer (not 'this'), so the lambda
    /// is safe even if the owning object is destroyed before it fires.
    template<typename F>
    void defer(F&& fn) {
        auto gen = gen_;
        uint64_t snapshot = gen->load();
        helix::ui::queue_update([gen, snapshot, f = std::forward<F>(fn)]() {
            if (gen->load() != snapshot) {
                spdlog::debug("[AsyncLifetimeGuard] Skipping stale deferred callback");
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
                spdlog::debug("[AsyncLifetimeGuard] Skipping stale deferred callback ({})", tag);
                return;
            }
            f();
        });
    }

  private:
    std::shared_ptr<std::atomic<uint64_t>> gen_ =
        std::make_shared<std::atomic<uint64_t>>(0);
};
```

### Integration: Modal Base Class

```cpp
// In ui_modal.h
class Modal {
  protected:
    /// Async callback safety. Automatically invalidated on hide().
    /// Subclasses use lifetime_.defer(...) or lifetime_.token() for
    /// bg-thread callbacks that need to touch UI.
    AsyncLifetimeGuard lifetime_;

  public:
    void hide() {
        if (!dialog_) return;
        lifetime_.invalidate();  // Cancel all pending deferred work
        on_hide();               // Subclass cleanup (observers, state)
        // ... existing hide logic (animation, backdrop removal) ...
    }
};
```

### Integration: OverlayBase

```cpp
// In overlay_base.h
class OverlayBase : public IPanelLifecycle {
  protected:
    AsyncLifetimeGuard lifetime_;

  public:
    void cleanup() override {
        lifetime_.invalidate();
        // ... existing cleanup ...
    }
};
```

### Usage: Common Case (90% of uses)

```cpp
// Before (ChangeHostModal — 4 layers of ad-hoc protection):
auto generation = test_generation_;
uint64_t this_generation = ++(*test_generation_);
ChangeHostModal* self = this;
lv_obj_t* guard_widget = dialog();
client->connect(ws_url.c_str(),
    [self, this_generation, guard_widget, generation]() {
        if (generation->load() != this_generation) return;
        helix::ui::async_call(guard_widget,
            [](void* ctx) {
                auto* self = static_cast<ChangeHostModal*>(ctx);
                if (!self->is_visible()) return;
                lv_subject_set_int(&self->validated_subject_, 1);
            }, self);
    }, ...);

// After:
auto token = lifetime_.token();
client->connect(ws_url.c_str(),
    [this, token]() {
        if (token.expired()) return;
        lifetime_.defer([this]() {
            set_status("icon_check_circle", "success", "Connection successful!");
            lv_subject_set_int(&testing_subject_, 0);
            lv_subject_set_int(&validated_subject_, 1);
        });
    }, ...);
```

### Usage: Re-test / Cancel-and-Retry Scenario

```cpp
void ChangeHostModal::handle_test_connection() {
    // Cancel any in-flight test callbacks
    lifetime_.invalidate();

    // New callbacks get fresh tokens
    auto token = lifetime_.token();
    client->connect(url,
        [this, token]() {
            if (token.expired()) return;
            lifetime_.defer([this]() { on_test_success_ui(); });
        }, ...);
}
// When modal is dismissed, base class calls lifetime_.invalidate() again
// in hide() — both the re-test and the dismiss case are covered.
```

### Usage: Raw Token for Complex Flows

For cases where `defer()` doesn't fit (non-UpdateQueue callbacks, multi-step chains):

```cpp
auto token = lifetime_.token();
state_machine.on_complete([this, token]() {
    if (token.expired()) return;
    // Direct main-thread work (already on main thread)
    update_ui();
});
```

---

## Migration Plan

### Files to Modify

**Infrastructure (new):**
- `include/async_lifetime_guard.h` — new file, `AsyncLifetimeGuard` + `LifetimeToken`

**Base classes:**
- `include/ui_modal.h` — add `lifetime_` member, call `invalidate()` in `hide()`
- `include/overlay_base.h` — add `lifetime_` member, call `invalidate()` in `cleanup()`

**Modals to migrate (remove ad-hoc guards):**

| File | Current Pattern | Migration |
|------|----------------|-----------|
| `ui_change_host_modal.cpp/h` | `shared_ptr<atomic<uint64_t>> test_generation_` | Remove `test_generation_`, use `lifetime_` |
| `ui_ams_edit_modal.cpp/h` | `shared_ptr<bool> callback_guard_` + `weak_ptr` | Remove `callback_guard_`, use `lifetime_` |
| `ui_job_queue_modal.cpp/h` | `shared_ptr<bool> alive_guard_` | Remove `alive_guard_`, use `lifetime_` |
| `ui_spoolman_edit_modal.cpp/h` | `shared_ptr<bool> callback_guard_` | Remove `callback_guard_`, use `lifetime_` |
| `ui_debug_bundle_modal.cpp/h` | `shared_ptr<bool> alive_` | Remove `alive_`, use `lifetime_` |

**Overlays to migrate:**

| File | Current Pattern | Migration |
|------|----------------|-----------|
| `ui_overlay_timelapse_install.cpp/h` | `shared_ptr<bool> alive_guard_` | Remove, use `lifetime_` |
| `ui_overlay_timelapse_videos.cpp/h` | `shared_ptr<atomic<bool>> alive_` + `nav_generation_` | Remove both, use `lifetime_` |
| `ui_overlay_qr_scanner.cpp/h` | `weak_ptr<bool> alive_` | Remove, use `lifetime_` |

**Panels to migrate (non-modal/overlay, but same pattern):**

| File | Current Pattern | Migration |
|------|----------------|-----------|
| `ui_panel_input_shaper.cpp/h` | `shared_ptr<atomic<bool>> alive_` + `calibration_gen_` | Remove, use `lifetime_` member directly |
| `ui_panel_belt_tension.cpp/h` | `shared_ptr<atomic<bool>> alive_` | Remove, use `lifetime_` |
| `ui_print_select_detail_view.cpp/h` | `shared_ptr<atomic<bool>> alive_` | Remove, use `lifetime_` |

**Modals with no current protection (add lifetime_ usage):**

| File | Current State | Migration |
|------|--------------|-----------|
| `ui_info_qr_modal.cpp/h` | Unprotected deferred self-deletion | Gets protection for free from base class |
| `ui_pin_entry_modal.cpp/h` | Static guard only, `lv_async_call` self-deletion | Gets protection for free |
| `ui_crash_report_modal.cpp/h` | Unprotected deferred self-deletion | Gets protection for free |

**Documentation:**
- `docs/devel/ARCHITECTURE.md` — add AsyncLifetimeGuard section, remove/deprecate references to ad-hoc patterns
- `docs/devel/MODAL_SYSTEM.md` — add async callback safety section with canonical pattern
- `CLAUDE.md` — update threading/lifecycle section to reference `AsyncLifetimeGuard`

**Tests:**
- `tests/unit/test_async_lifetime_guard.cpp` — new unit tests for token expiry, defer skip, generation cycling, thread safety

---

## What Gets Removed

After migration, these patterns should no longer appear in the codebase:

- `shared_ptr<bool> callback_guard_` / `alive_guard_` / `alive_`
- `shared_ptr<atomic<bool>> alive_`
- `shared_ptr<atomic<uint64_t>> test_generation_` (except `AsyncLifetimeGuard` itself)
- `weak_ptr<bool>` for callback safety (still valid for other weak-reference uses)
- `async_call(guard_widget, cb, data)` for modal/overlay callback safety (the widget-safe `async_call` stays in UpdateQueue for non-modal uses, but modals/overlays should prefer `lifetime_.defer()`)
- Manual `*alive_ = false` / `callback_guard_.reset()` in `on_hide()` / `cleanup()`

## What Stays

- `ObserverGuard` / `SubjectLifetime` — observer lifecycle, orthogonal concern
- `ScopedFreeze` — drain+destroy race window, orthogonal concern
- `async_call(cb, data)` (non-widget variant) — general-purpose deferred call, still useful outside modals
- `queue_widget_update(widget, cb)` — widget-specific updates (e.g., LED swatch color), different use case
- Static `active_instance_` patterns for XML event callback dispatch — ownership tracking, not callback safety

---

## Testing Strategy

### Unit Tests (`test_async_lifetime_guard.cpp`)

1. **Token expiry**: token created, `invalidate()` called, `expired()` returns true
2. **Token valid**: token created, no invalidation, `expired()` returns false
3. **Multiple tokens**: multiple tokens from same guard, all expire on single `invalidate()`
4. **Generation cycling**: invalidate, get new token, old token expired, new token valid
5. **Defer skips stale**: `defer()` callback, `invalidate()`, drain queue, callback did not run
6. **Defer runs when valid**: `defer()` callback, drain queue (no invalidation), callback ran
7. **Defer safe after destruction**: create guard, `defer()`, destroy guard, drain queue — no crash (shared_ptr keeps gen alive)
8. **Thread safety**: multiple threads calling `token()` and `invalidate()` concurrently — no data races

### Integration Validation

For each migrated modal/overlay, verify the existing test suite still passes. No new integration tests needed — the existing tests cover the modal behavior; we're just standardizing the safety mechanism.

---

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| `defer()` captures `[this]` — still a raw pointer | `defer()` only runs if generation matches; base class invalidates before any teardown. If `this` is freed between `invalidate()` and queue drain, `ScopedFreeze` (already used in destructors) prevents the callback from firing. |
| Panels don't have a base class with auto-invalidation | Panels that use `AsyncLifetimeGuard` must call `invalidate()` manually in their destructor. The guard's own destructor also calls `invalidate()`, but the destructor runs after member lambdas may have been cleared. Explicit invalidation in panel cleanup is safer. |
| `defer()` uses `queue_update` which can be frozen | Frozen queue discards callbacks — same behavior as invalidation. This is correct: if we're in a freeze (destroying widgets), we don't want deferred callbacks running. |
| Migration is large (15+ files) | Each file is independent — can be migrated and tested in isolation. No cross-file dependencies in the migration. |
