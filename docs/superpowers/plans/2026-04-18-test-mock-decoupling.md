# Test/Mock Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decouple the test and mock layers from the real app: eliminate shared static state in test fixtures (per-test `PrinterState`), add compile-time drift protection for the six mock boundaries, and extract interfaces for the two Moonraker classes that currently lack them.

**Architecture:** Five phases, each independently shippable. Phase 1 fixes test isolation via LVGL's per-component XML scope primitive. Phase 2 formalizes drift protection for the four backends that are already abstract. Phases 3–4 extract `IMoonrakerAPI` and `IMoonrakerClient`. Phase 5 cleans up. Production CLI (`--test` mode) behavior is preserved end-to-end.

**Tech Stack:** C++17 · LVGL 9.5 · Catch2 v2.x · libhv WebSocket · Makefile build · spdlog

**Spec:** [`docs/superpowers/specs/2026-04-18-test-mock-decoupling-design.md`](../specs/2026-04-18-test-mock-decoupling-design.md)

---

## Worktree Setup (do this first, before any task)

The user has explicitly required a worktree for this work.

- [ ] **Step 0: Create worktree**

```bash
scripts/setup-worktree.sh feature/test-mock-decoupling
cd .worktrees/feature-test-mock-decoupling
```

Work in the worktree for the remainder of this plan. Build with `make -j`, test with `make test-run`.

---

## Phase 1 — Test Isolation Plumbing

Goal: eliminate `XMLTestFixture::s_state`/`s_client`/`s_api` statics. Each test gets a fresh `PrinterState` whose subjects are registered into a per-test LVGL XML scope that is torn down at test end.

### Task 1.1: Add scope-aware subject registration helper

**Files:**
- Create: `include/helix/xml/scoped_subject_registry.h`
- Create: `src/util/scoped_subject_registry.cpp`

- [ ] **Step 1: Write the failing test**

File: `tests/unit/test_scoped_subject_registry.cpp`

```cpp
#include "catch_amalgamated.hpp"
#include "helix/xml/scoped_subject_registry.h"
#include "lvgl/lvgl.h"
#include "lvgl_test_fixture.h"

TEST_CASE_METHOD(LVGLTestFixture, "scoped_subject_registry defaults to global scope",
                 "[xml][scope]") {
    lv_subject_t s;
    lv_subject_init_int(&s, 42);

    // With no active scope, registration goes to global
    helix::xml::register_subject_in_current_scope("test_default_scope", &s);

    REQUIRE(lv_xml_get_subject(nullptr, "test_default_scope") == &s);

    lv_subject_deinit(&s);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "scoped_subject_registry uses active scope when pushed", "[xml][scope]") {
    lv_xml_component_scope_t* scope = lv_xml_register_component_from_data(
        "test_scope_stub", "<component><view/></component>");
    REQUIRE(scope != nullptr);

    lv_subject_t s;
    lv_subject_init_int(&s, 7);

    {
        helix::xml::ScopedSubjectRegistryOverride push(scope);
        helix::xml::register_subject_in_current_scope("scoped_int", &s);
    }

    REQUIRE(lv_xml_get_subject(scope, "scoped_int") == &s);
    REQUIRE(lv_xml_get_subject(nullptr, "scoped_int") == nullptr);

    lv_subject_deinit(&s);
    lv_xml_unregister_component("test_scope_stub");
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
make test && ./build/bin/helix-tests "[xml][scope]" -v
```

Expected: FAIL — `scoped_subject_registry.h` not found.

- [ ] **Step 3: Write the header**

File: `include/helix/xml/scoped_subject_registry.h`

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

namespace helix::xml {

// RAII override for the current XML subject-registration scope.
// When active, register_subject_in_current_scope() registers into the
// overridden scope; otherwise registration goes to the global scope.
//
// Not thread-safe. Tests run single-threaded; production does not use this.
class ScopedSubjectRegistryOverride {
  public:
    explicit ScopedSubjectRegistryOverride(lv_xml_component_scope_t* scope);
    ~ScopedSubjectRegistryOverride();

    ScopedSubjectRegistryOverride(const ScopedSubjectRegistryOverride&) = delete;
    ScopedSubjectRegistryOverride& operator=(const ScopedSubjectRegistryOverride&) = delete;

  private:
    lv_xml_component_scope_t* previous_;
};

// Register a subject in the currently-active scope. Returns LV_RESULT_OK on success.
// When no ScopedSubjectRegistryOverride is active, registers into the global scope
// (equivalent to lv_xml_register_subject(nullptr, name, subject)).
lv_result_t register_subject_in_current_scope(const char* name, lv_subject_t* subject);

// Access the active scope (nullptr if none). For debug assertions only.
lv_xml_component_scope_t* current_scope();

} // namespace helix::xml
```

- [ ] **Step 4: Write the implementation**

File: `src/util/scoped_subject_registry.cpp`

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helix/xml/scoped_subject_registry.h"

namespace helix::xml {

namespace {
lv_xml_component_scope_t* g_active_scope = nullptr;
}

ScopedSubjectRegistryOverride::ScopedSubjectRegistryOverride(lv_xml_component_scope_t* scope)
    : previous_(g_active_scope) {
    g_active_scope = scope;
}

ScopedSubjectRegistryOverride::~ScopedSubjectRegistryOverride() {
    g_active_scope = previous_;
}

lv_result_t register_subject_in_current_scope(const char* name, lv_subject_t* subject) {
    return lv_xml_register_subject(g_active_scope, name, subject);
}

lv_xml_component_scope_t* current_scope() {
    return g_active_scope;
}

} // namespace helix::xml
```

- [ ] **Step 5: Add to Makefile**

Edit `Makefile`: add `src/util/scoped_subject_registry.cpp` to the `APP_SRCS` / `COMMON_SRCS` list (same list that contains `src/util/*.cpp`).

```bash
grep -n "src/util" Makefile | head -5
```

Add the new source file next to the existing `src/util/*.cpp` entries in both the app and test source lists.

- [ ] **Step 6: Run test to verify it passes**

```bash
make -j && make test && ./build/bin/helix-tests "[xml][scope]" -v
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add include/helix/xml/scoped_subject_registry.h \
        src/util/scoped_subject_registry.cpp \
        tests/unit/test_scoped_subject_registry.cpp \
        Makefile
git commit -m "feat(test): scoped XML subject registry for per-test isolation"
```

---

### Task 1.2: Switch subject-registration macros to the helper

**Files:**
- Modify: `include/state/subject_macros.h`
- Modify: `include/ui_subject_registry.h`
- Modify: `include/subject_managed_panel.h`

This task changes all existing `lv_xml_register_subject(nullptr, ...)` macro expansions to call the new helper. Production behavior is unchanged (no scope active = global, identical to current).

- [ ] **Step 1: Locate every macro that hardcodes `nullptr` as scope**

```bash
grep -rn 'lv_xml_register_subject(nullptr' include/
grep -rn 'lv_xml_register_subject(NULL' include/
```

Expected hits: `include/state/subject_macros.h` (~8 lines), `include/ui_subject_registry.h` (~5 lines), `include/subject_managed_panel.h` (~5 lines).

- [ ] **Step 2: Replace in `include/state/subject_macros.h`**

Change every occurrence of `lv_xml_register_subject(nullptr, #name, &name##_);` to:

```cpp
helix::xml::register_subject_in_current_scope(#name, &name##_);
```

Add at the top of the file (after existing includes):

```cpp
#include "helix/xml/scoped_subject_registry.h"
```

- [ ] **Step 3: Replace in `include/ui_subject_registry.h`**

Change every occurrence of `lv_xml_register_subject(NULL, (name), &(subject));` to:

```cpp
helix::xml::register_subject_in_current_scope((name), &(subject));
```

Add `#include "helix/xml/scoped_subject_registry.h"` at the top.

- [ ] **Step 4: Replace in `include/subject_managed_panel.h`**

Change every occurrence of `lv_xml_register_subject(nullptr, (xml_name), &(subject));` to:

```cpp
helix::xml::register_subject_in_current_scope((xml_name), &(subject));
```

Add `#include "helix/xml/scoped_subject_registry.h"` at the top.

- [ ] **Step 5: Build — everything compiles unchanged**

```bash
make -j 2>&1 | tee /tmp/build.log
```

Expected: clean build; no errors. Production behavior unchanged because no scope is ever pushed from production code.

- [ ] **Step 6: Run full test suite — everything still green**

```bash
make test-run 2>&1 | tee /tmp/test.log
```

Expected: all tests pass, no new failures.

- [ ] **Step 7: Commit**

```bash
git add include/state/subject_macros.h \
        include/ui_subject_registry.h \
        include/subject_managed_panel.h
git commit -m "refactor(test): route subject registration through scope-aware helper"
```

---

### Task 1.3: Add `HelixTestFixture` base with singleton reset

**Files:**
- Create: `tests/helix_test_fixture.h`
- Create: `tests/helix_test_fixture.cpp`

- [ ] **Step 1: Write the failing test**

File: `tests/unit/test_helix_test_fixture.cpp`

```cpp
#include "catch_amalgamated.hpp"
#include "helix_test_fixture.h"
#include "system_settings_manager.h"

TEST_CASE_METHOD(HelixTestFixture,
                 "HelixTestFixture resets SystemSettingsManager language",
                 "[test-fixture][isolation]") {
    SystemSettingsManager::instance().set_language("fr");
    // Destructor of prior fixture would have reset; this test confirms entry reset happened.
    // Since we just changed language mid-test, we expect no assertion here — purpose of the
    // test is that a *subsequent* fixture instance sees the default, which a later test
    // case would verify.
    REQUIRE(SystemSettingsManager::instance().get_language() == "fr");
}

TEST_CASE_METHOD(HelixTestFixture,
                 "HelixTestFixture leaves default language after construction",
                 "[test-fixture][isolation]") {
    // If the prior test's destructor reset state, we see the default here.
    REQUIRE(SystemSettingsManager::instance().get_language() == "en");
}
```

- [ ] **Step 2: Run to verify failure**

```bash
make test && ./build/bin/helix-tests "[test-fixture][isolation]" -v
```

Expected: FAIL — header not found.

- [ ] **Step 3: Write the header**

File: `tests/helix_test_fixture.h`

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Base fixture for every HelixScreen test. Deterministically resets
// process-wide singletons tests are known to mutate so ordering cannot
// mask bugs. Expand reset_all() reactively as flakiness surfaces.
//
// Derive test-specific fixtures from this (LVGLTestFixture does). Plain
// non-LVGL unit tests can use it directly via TEST_CASE_METHOD.
class HelixTestFixture {
  public:
    HelixTestFixture();   // calls reset_all() on entry — idempotent
    virtual ~HelixTestFixture();  // calls reset_all() on exit — leaves clean slate

    HelixTestFixture(const HelixTestFixture&) = delete;
    HelixTestFixture& operator=(const HelixTestFixture&) = delete;
    HelixTestFixture(HelixTestFixture&&) = delete;
    HelixTestFixture& operator=(HelixTestFixture&&) = delete;

  protected:
    // List expands reactively. Keep small; don't over-reset.
    static void reset_all();
};
```

- [ ] **Step 4: Write the implementation**

File: `tests/helix_test_fixture.cpp`

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helix_test_fixture.h"

#include "navigation_manager.h"
#include "system_settings_manager.h"
#include "ui_modal_stack.h"
#include "ui_theme.h"
#include "ui_update_queue.h"

HelixTestFixture::HelixTestFixture() {
    reset_all();
}

HelixTestFixture::~HelixTestFixture() {
    reset_all();
}

void HelixTestFixture::reset_all() {
    // Drain any callbacks queued by a prior test before we touch state they read.
    helix::ui::UpdateQueue::instance().drain();

    // Language / theme back to defaults.
    SystemSettingsManager::instance().set_language("en");
    ui_theme_reset_to_default();

    // Navigation + modal stacks empty.
    NavigationManager::instance().clear_stack();
    helix::ui::ModalStack::instance().clear();
}
```

**Note for implementer:** Before writing this file, verify each call is valid. Some may need a different name (e.g., `set_language` may be `set_current_language`) or may not exist at all. Use `grep -rn 'class SystemSettingsManager' include/ src/` to find the real header and API. If a method doesn't exist, either add it (if trivial and test-useful) or omit the reset for that singleton and note it in Step 7's commit message.

- [ ] **Step 5: Add to Makefile**

Edit `Makefile`: `tests/helix_test_fixture.cpp` goes next to `tests/lvgl_test_fixture.cpp` in the test source list.

- [ ] **Step 6: Run to verify tests pass**

```bash
make test-run 2>&1 | tee /tmp/test.log
grep -E "^(passed|failed)" /tmp/test.log | tail -5
```

Expected: both new test cases pass; full suite still green.

- [ ] **Step 7: Commit**

```bash
git add tests/helix_test_fixture.h tests/helix_test_fixture.cpp \
        tests/unit/test_helix_test_fixture.cpp Makefile
git commit -m "feat(test): HelixTestFixture base for singleton reset between tests"
```

---

### Task 1.4: Migrate `LVGLTestFixture` to inherit `HelixTestFixture`

**Files:**
- Modify: `tests/lvgl_test_fixture.h`
- Modify: `tests/lvgl_test_fixture.cpp`

- [ ] **Step 1: Update header**

Edit `tests/lvgl_test_fixture.h`:

At the top, add:
```cpp
#include "helix_test_fixture.h"
```

Change:
```cpp
class LVGLTestFixture {
```
to:
```cpp
class LVGLTestFixture : public HelixTestFixture {
```

- [ ] **Step 2: Build**

```bash
make test -j
```

Expected: clean build. `HelixTestFixture::reset_all()` now runs on every `LVGLTestFixture` ctor/dtor automatically.

- [ ] **Step 3: Run full suite**

```bash
make test-run
```

Expected: all green.

- [ ] **Step 4: Commit**

```bash
git add tests/lvgl_test_fixture.h
git commit -m "refactor(test): LVGLTestFixture inherits HelixTestFixture"
```

---

### Task 1.5: Migrate `XMLTestFixture` to per-test `PrinterState` and per-test scope

**Files:**
- Modify: `tests/test_fixtures.h`
- Modify: `tests/test_fixtures.cpp`

This is the core of Phase 1. It removes the statics and replaces them with per-instance state registered into a per-test scope.

- [ ] **Step 1: Update header**

Edit `tests/test_fixtures.h`. Replace the `XMLTestFixture` declaration block (currently around lines 276–378) with:

```cpp
class XMLTestFixture : public LVGLTestFixture {
  public:
    XMLTestFixture();
    ~XMLTestFixture() override;

    XMLTestFixture(const XMLTestFixture&) = delete;
    XMLTestFixture& operator=(const XMLTestFixture&) = delete;
    XMLTestFixture(XMLTestFixture&&) = delete;
    XMLTestFixture& operator=(XMLTestFixture&&) = delete;

    PrinterState& state() { return m_state; }
    MoonrakerClient& client() { return *m_client; }
    MoonrakerAPI& api() { return *m_api; }

    bool register_component(const char* component_name);
    lv_obj_t* create_component(const char* component_name);
    lv_obj_t* create_component(const char* component_name, const char** attrs);
    void register_subjects();

  private:
    void setup_global_xml_registrations_once();
    static bool s_global_registered;  // XML widget classes, fonts — truly one-time

    std::string m_scope_name;         // unique per instance
    lv_xml_component_scope_t* m_scope = nullptr;
    PrinterState m_state;
    std::unique_ptr<MoonrakerClient> m_client;
    std::unique_ptr<MoonrakerAPI> m_api;
    bool m_theme_initialized = false;
    bool m_subjects_registered = false;
};
```

Remove the static member declarations (`s_state`, `s_client`, `s_api`, `s_initialized`). Remove `reset_subject_values()` — no longer needed; each instance has its own state.

- [ ] **Step 2: Update implementation**

Edit `tests/test_fixtures.cpp`. Replace the `XMLTestFixture` block (starting around line 75 with "XMLTestFixture Implementation" through its destructor) with:

```cpp
bool XMLTestFixture::s_global_registered = false;

namespace {
std::atomic<int> g_xml_fixture_counter{0};
} // namespace

XMLTestFixture::XMLTestFixture() : LVGLTestFixture() {
    if (m_test_screen != nullptr) {
        lv_obj_delete(m_test_screen);
        m_test_screen = nullptr;
    }

    setup_global_xml_registrations_once();

    // One unique scope per test instance. Name ensures no collision even if
    // LVGL leaks a prior test's scope (which would be a separate bug).
    m_scope_name = "helix_xml_test_" + std::to_string(g_xml_fixture_counter.fetch_add(1));
    m_scope = lv_xml_register_component_from_data(
        m_scope_name.c_str(), "<component><view/></component>");
    if (m_scope == nullptr) {
        throw std::runtime_error("XMLTestFixture: failed to create test scope");
    }

    // Register this test's PrinterState subjects into *this* scope.
    {
        helix::xml::ScopedSubjectRegistryOverride push(m_scope);
        m_state.init_subjects(true);
    }

    m_client = std::make_unique<MoonrakerClient>();
    m_api = std::make_unique<MoonrakerAPI>(*m_client, m_state);

    if (!m_theme_initialized) {
        theme_manager_init(lv_display_get_default(), false);
        m_theme_initialized = true;
    }

    m_test_screen = create_test_screen();
    spdlog::debug("[XMLTestFixture] Initialized scope={}", m_scope_name);
}

XMLTestFixture::~XMLTestFixture() {
    // Order matters: API → client → subjects → scope.
    m_api.reset();
    m_client.reset();
    m_state.deinit_subjects();

    if (m_scope != nullptr) {
        lv_xml_unregister_component(m_scope_name.c_str());
        m_scope = nullptr;
    }
    spdlog::debug("[XMLTestFixture] Cleaned up scope={}", m_scope_name);
}

void XMLTestFixture::setup_global_xml_registrations_once() {
    if (s_global_registered) return;

    AssetManager::register_all();
    lv_xml_register_component_from_file("A:ui_xml/globals.xml");
    ui_icon_register_widget();
    ui_text_init();
    ui_text_input_init();
    ui_button_init();
    ui_card_register();
    ui_temp_display_init();
    lv_xml_register_event_cb(nullptr, "", xml_test_noop_event_callback);
    lv_xml_register_event_cb(nullptr, "on_header_back_clicked", xml_test_noop_event_callback);
    lv_xml_register_event_cb(nullptr, "on_nozzle_preset_off_clicked", xml_test_noop_event_callback);
    lv_xml_register_event_cb(nullptr, "on_nozzle_preset_pla_clicked", xml_test_noop_event_callback);
    // Preserve any remaining event-cb registrations from the original setup here.

    s_global_registered = true;
}
```

**Note for implementer:** The original `XMLTestFixture()` body registers many more event callbacks (continuing past line 140). Preserve all of those in `setup_global_xml_registrations_once()`. Read the full original block first (`sed -n '95,250p' tests/test_fixtures.cpp` or use the Read tool) and copy the remaining `lv_xml_register_event_cb` calls verbatim into this function.

- [ ] **Step 3: Update `register_subjects()`, `register_component()`, `create_component()` to use the scope**

These helper methods need to push `ScopedSubjectRegistryOverride{m_scope}` during the registration calls they invoke internally. Wrap each method body:

```cpp
bool XMLTestFixture::register_component(const char* component_name) {
    helix::xml::ScopedSubjectRegistryOverride push(m_scope);
    // ... existing body ...
}
```

For `lv_xml_create(...)` calls inside `create_component()`, pass `m_scope` as the scope parameter instead of `nullptr`.

- [ ] **Step 4: Add includes**

Top of `tests/test_fixtures.cpp`:
```cpp
#include "helix/xml/scoped_subject_registry.h"
#include <atomic>
#include <stdexcept>
```

- [ ] **Step 5: Build**

```bash
make test -j 2>&1 | tee /tmp/build.log
```

Expected: clean build.

- [ ] **Step 6: Run full test suite**

```bash
make test-run 2>&1 | tee /tmp/test.log
```

Expected: all green. If failures appear, they're the real test-isolation bugs we were protecting against via the static state. Triage each: is the failing test depending on leaked state from a prior test? If so, fix the test. If it's a genuine XMLTestFixture regression, revisit scope handling.

- [ ] **Step 7: Smoke-check `--test` mode still works**

```bash
timeout 5 ./build/bin/helix-screen --test -vv 2>&1 | head -30 || true
```

Expected: app launches, logs init messages, no crash. Production code path is unaffected.

- [ ] **Step 8: Commit**

```bash
git add tests/test_fixtures.h tests/test_fixtures.cpp
git commit -m "refactor(test): XMLTestFixture uses per-test scope + per-test PrinterState"
```

---

### Task 1.6: Remove `FirstRunTour::reset_for_test()`

**Files:**
- Modify: `include/first_run_tour.h`
- Modify: `src/ui/tour/first_run_tour.cpp`
- Modify: `tests/unit/test_first_run_tour.cpp`

- [ ] **Step 1: Delete the declaration in the header**

Edit `include/first_run_tour.h`. Delete lines 45–48 (the docstring and `void reset_for_test();` declaration).

- [ ] **Step 2: Delete the implementation**

Edit `src/ui/tour/first_run_tour.cpp`. Find `void FirstRunTour::reset_for_test()` (around line 164) and delete the entire function body plus any leading comment block.

- [ ] **Step 3: Replace the test's usage**

Edit `tests/unit/test_first_run_tour.cpp`. Replace line 25 (`FirstRunTour::instance().reset_for_test();`) with whatever the test needs given that each test now starts with a fresh `HelixTestFixture`-driven reset. Most likely: delete the line; if the test legitimately needs to simulate a fresh `FirstRunTour` (e.g., resetting the running flag), use the friend-class `*TestAccess` pattern to do so from the test file.

- [ ] **Step 4: Build**

```bash
make test -j
```

Expected: clean build.

- [ ] **Step 5: Run the affected test**

```bash
./build/bin/helix-tests "[first-run-tour]" -v
```

Expected: green.

- [ ] **Step 6: Commit**

```bash
git add include/first_run_tour.h src/ui/tour/first_run_tour.cpp \
        tests/unit/test_first_run_tour.cpp
git commit -m "refactor(tour): remove reset_for_test() — superseded by HelixTestFixture"
```

---

### Task 1.7: Phase 1 gate — full suite + `--test` smoke

- [ ] **Step 1: Clean build everything**

```bash
make clean && make -j 2>&1 | tail -30
```

Expected: clean build.

- [ ] **Step 2: Run full test suite**

```bash
make test-run 2>&1 | tee /tmp/test.log
tail -20 /tmp/test.log
```

Expected: all green.

- [ ] **Step 3: Smoke `--test`**

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/smoke.log &
SMOKE_PID=$!
sleep 3
kill $SMOKE_PID 2>/dev/null
grep -E "(error|crash|SIGSEGV)" /tmp/smoke.log | head -5
```

Expected: no errors in the first 3 seconds. (Full UI check requires user interaction per L060.)

- [ ] **Step 4: Tag the phase**

```bash
git tag phase-1-complete
```

---

## Phase 2 — Drift-Protection Smoke Tests

Goal: four backends (`AmsBackend`, `EthernetBackend`, `UsbBackend`, `WifiBackend`) are already pure virtual interfaces. Formalize drift protection by adding a compile-only smoke test per backend that instantiates the mock through a `unique_ptr<BaseInterface>`. If the interface gains a pure-virtual method and the mock doesn't follow, this test fails to compile.

### Task 2.1: Ethernet backend drift smoke test

**Files:**
- Create: `tests/unit/test_interface_drift_ethernet.cpp`

- [ ] **Step 1: Write the smoke test**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection: if EthernetBackend adds a pure-virtual
// method and EthernetBackendMock doesn't implement it, this fails to build.

#include "catch_amalgamated.hpp"
#include "ethernet_backend.h"

#ifdef HELIX_ENABLE_MOCKS
#include "ethernet_backend_mock.h"

TEST_CASE("EthernetBackendMock satisfies EthernetBackend interface", "[compile][drift]") {
    std::unique_ptr<EthernetBackend> p = std::make_unique<EthernetBackendMock>();
    REQUIRE(p != nullptr);
    // Exercise every pure-virtual so a non-abstract mock that happens to compile
    // but is missing overrides produces a link/runtime error rather than silent drift.
    (void)p->has_interface();
    EthernetInfo info = p->get_info();
    (void)info.connected;
}
#endif
```

- [ ] **Step 2: Build and run**

```bash
make test -j && ./build/bin/helix-tests "[compile][drift]" -v
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_interface_drift_ethernet.cpp
git commit -m "test(drift): compile-time guard for EthernetBackend mock parity"
```

---

### Task 2.2: USB backend drift smoke test

**Files:**
- Create: `tests/unit/test_interface_drift_usb.cpp`

- [ ] **Step 1: Write the smoke test**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection for UsbBackend.

#include "catch_amalgamated.hpp"
#include "usb_backend.h"

#ifdef HELIX_ENABLE_MOCKS
#include "usb_backend_mock.h"

TEST_CASE("UsbBackendMock satisfies UsbBackend interface", "[compile][drift]") {
    std::unique_ptr<UsbBackend> p = std::make_unique<UsbBackendMock>();
    REQUIRE(p != nullptr);
    (void)p->is_running();
    // start() and stop() exercised so their virtuals are linked; safe to call on mock.
    (void)p->start();
    p->stop();
}
#endif
```

- [ ] **Step 2: Build and run**

```bash
make test -j && ./build/bin/helix-tests "[compile][drift]" -v
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_interface_drift_usb.cpp
git commit -m "test(drift): compile-time guard for UsbBackend mock parity"
```

---

### Task 2.3: WiFi backend drift smoke test

**Files:**
- Create: `tests/unit/test_interface_drift_wifi.cpp`

- [ ] **Step 1: Write the smoke test**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection for WifiBackend.

#include "catch_amalgamated.hpp"
#include "wifi_backend.h"

#ifdef HELIX_ENABLE_MOCKS
#include "wifi_backend_mock.h"

TEST_CASE("WifiBackendMock satisfies WifiBackend interface", "[compile][drift]") {
    std::unique_ptr<WifiBackend> p = std::make_unique<WifiBackendMock>();
    REQUIRE(p != nullptr);
    (void)p->start();
}
#endif
```

- [ ] **Step 2: Build and run**

```bash
make test -j && ./build/bin/helix-tests "[compile][drift]" -v
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_interface_drift_wifi.cpp
git commit -m "test(drift): compile-time guard for WifiBackend mock parity"
```

---

### Task 2.4: AMS backend drift smoke test

**Files:**
- Create: `tests/unit/test_interface_drift_ams.cpp`

- [ ] **Step 1: Write the smoke test**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection for AmsBackend.

#include "catch_amalgamated.hpp"
#include "ams_backend.h"

#ifdef HELIX_ENABLE_MOCKS
#include "ams_backend_mock.h"

TEST_CASE("AmsBackendMock satisfies AmsBackend interface", "[compile][drift]") {
    std::unique_ptr<AmsBackend> p = std::make_unique<AmsBackendMock>();
    REQUIRE(p != nullptr);
}
#endif
```

- [ ] **Step 2: Build and run**

```bash
make test -j && ./build/bin/helix-tests "[compile][drift]" -v
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_interface_drift_ams.cpp
git commit -m "test(drift): compile-time guard for AmsBackend mock parity"
```

---

### Task 2.5: Audit for silent-drift gaps in the four abstract backends

For each of `AmsBackend`, `EthernetBackend`, `UsbBackend`, `WifiBackend`, scan for methods the mock overrides that are **not declared virtual** in the base. Those are silent-drift risks — the mock's override is actually a shadow, not a polymorphic dispatch. Flag and fix.

- [ ] **Step 1: Audit each base class's mock overrides**

```bash
for base in ams_backend ethernet_backend usb_backend wifi_backend; do
    echo "=== $base ==="
    grep -nE '\b(virtual|override)\b' include/${base}.h | head -40
    echo "--- mock ---"
    grep -nE '\boverride\b' include/${base}_mock.h | head -40
done
```

For each `override` in a mock, verify the corresponding method in the base is declared `virtual`. Any mismatch = silent drift gap.

- [ ] **Step 2: For each gap, add `virtual` in the base class header**

Example — if `EthernetBackendMock` declares `void refresh() override;` but `EthernetBackend::refresh()` isn't virtual, edit `include/ethernet_backend.h` and change `void refresh();` to `virtual void refresh();`.

If no gaps found, skip to Step 3.

- [ ] **Step 3: Build + test**

```bash
make -j && make test-run
```

Expected: still green. `virtual` additions don't break anything.

- [ ] **Step 4: Commit**

```bash
git add include/ams_backend.h include/ethernet_backend.h include/usb_backend.h include/wifi_backend.h
git commit -m "fix(backend): close silent drift gaps in base-class virtuals"
```

Skip this commit if no changes were needed.

---

### Task 2.6: Phase 2 gate

- [ ] **Step 1: Clean build + full suite**

```bash
make clean && make -j && make test-run
```

Expected: all green; four new drift tests pass.

- [ ] **Step 2: Tag the phase**

```bash
git tag phase-2-complete
```

---

## Phase 3 — Extract `IMoonrakerAPI`

Goal: `MoonrakerAPI` today is concrete; `MoonrakerAPIMock : public MoonrakerAPI`. Extract a pure virtual `IMoonrakerAPI`. Real and mock both implement it. Callers hold `IMoonrakerAPI*` / `std::unique_ptr<IMoonrakerAPI>` where they only need the API surface.

### Task 3.1: Enumerate the public API surface

Before writing the interface, produce a complete list of `MoonrakerAPI`'s public methods. The interface will include every public method callers actually use.

**Files:**
- Read: `include/moonraker_api.h` (~1000+ lines)

- [ ] **Step 1: Dump public methods**

```bash
awk '/^class MoonrakerAPI/,/^};/' include/moonraker_api.h \
    | grep -E '^\s+(virtual\s+)?[a-zA-Z_:<>,&*\s]+\w+\s*\(' \
    | grep -v '^\s*//' \
    | tee /tmp/moonraker_api_surface.txt
```

Read `/tmp/moonraker_api_surface.txt`. This is the candidate interface surface. Every method listed here will become pure virtual in `IMoonrakerAPI`.

- [ ] **Step 2: Identify callers to confirm what's actually used**

```bash
grep -rn 'MoonrakerAPI::' src/ | awk -F'::' '{print $2}' | awk '{print $1}' \
    | sort -u | tee /tmp/moonraker_api_used.txt
```

If any method in the class is NOT in `moonraker_api_used.txt`, consider whether it's safe to leave out of the interface (keep concrete). Default: include it in the interface anyway, to preserve the mock's existing surface.

- [ ] **Step 3: Commit the audit notes**

```bash
mkdir -p /tmp/phase3-notes
cp /tmp/moonraker_api_surface.txt /tmp/moonraker_api_used.txt /tmp/phase3-notes/
# These are working notes; no commit needed.
```

---

### Task 3.2: Create `IMoonrakerAPI` with pure-virtual surface

**Files:**
- Create: `include/i_moonraker_api.h`

- [ ] **Step 1: Write the interface**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_error.h"
#include "hv/json.hpp"

#include <functional>
#include <string>

// Forward-declarations used by the interface signatures.
namespace helix {
class PrinterState;
class MoonrakerClient;
} // namespace helix

// Abstract interface for the high-level Moonraker API façade.
//
// Production and test consumers alike should depend on this type rather than
// the concrete MoonrakerAPI. The concrete class inherits this interface;
// MoonrakerAPIMock inherits it directly (not from MoonrakerAPI).
class IMoonrakerAPI {
  public:
    virtual ~IMoonrakerAPI() = default;

    using SuccessCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const MoonrakerError&)>;
    using BoolCallback = std::function<void(bool)>;
    using StringCallback = std::function<void(const std::string&)>;
    using JsonCallback = std::function<void(const nlohmann::json&)>;

    // ====== Begin pure-virtual surface — populate from /tmp/moonraker_api_surface.txt ======
    //
    // For each public method X in moonraker_api.h, add:
    //     virtual <return-type> X(<params>) = 0;
    //
    // Preserve the exact signature (including const-ness and default-arg values).
    // Do NOT include constructors, destructors, or static constants — those stay on
    // the concrete class.
    //
    // Example entries (replace with real ones):

    virtual void set_temperature(const std::string& heater, double temperature,
                                 SuccessCallback on_success, ErrorCallback on_error) = 0;
    virtual void set_fan_speed(const std::string& fan, double speed,
                               SuccessCallback on_success, ErrorCallback on_error) = 0;
    // ...every other public method...

    // ====== End surface ======
};
```

**Note for implementer:** Do NOT attempt to enumerate every method from memory. Open `include/moonraker_api.h` and `/tmp/moonraker_api_surface.txt` side-by-side and translate each public non-static, non-constructor method into a pure-virtual entry. Preserve default arguments and const-qualifiers exactly.

- [ ] **Step 2: Build the interface header alone**

```bash
echo '#include "i_moonraker_api.h"' > /tmp/i_api_check.cpp
g++ -std=c++17 -Iinclude -Ilib/helix-xml/src -c /tmp/i_api_check.cpp -o /tmp/i_api_check.o 2>&1 | tee /tmp/i_api_check.log
```

Expected: compiles without errors (ignore warnings). If it doesn't compile, the signatures have references to types not forward-declared — add the required `#include`s.

- [ ] **Step 3: Commit the interface**

```bash
git add include/i_moonraker_api.h
git commit -m "feat(moonraker): add IMoonrakerAPI pure-virtual interface"
```

---

### Task 3.3: Make `MoonrakerAPI` inherit `IMoonrakerAPI`

**Files:**
- Modify: `include/moonraker_api.h`

- [ ] **Step 1: Add the inheritance**

Edit `include/moonraker_api.h`:

At the top (after existing includes):
```cpp
#include "i_moonraker_api.h"
```

Change:
```cpp
class MoonrakerAPI {
```
to:
```cpp
class MoonrakerAPI : public IMoonrakerAPI {
```

Add `override` to every method that now implements a pure virtual. The compiler will tell you exactly which ones.

- [ ] **Step 2: Build**

```bash
make -j 2>&1 | tee /tmp/build.log
grep -E "(error|missing)" /tmp/build.log | head -20
```

Expected: compile errors listing any method in `MoonrakerAPI` whose signature doesn't match the interface. Fix each by reconciling the interface or the concrete signature — whichever was more correct.

- [ ] **Step 3: Build until clean**

Iterate Steps 1–2 until `make -j` succeeds.

- [ ] **Step 4: Commit**

```bash
git add include/moonraker_api.h
git commit -m "refactor(moonraker): MoonrakerAPI implements IMoonrakerAPI"
```

---

### Task 3.4: Rebase `MoonrakerAPIMock` on `IMoonrakerAPI`

**Files:**
- Modify: `include/moonraker_api_mock.h`

- [ ] **Step 1: Change inheritance**

Edit `include/moonraker_api_mock.h`:

Change:
```cpp
class MoonrakerAPIMock : public MoonrakerAPI {
```
to:
```cpp
class MoonrakerAPIMock : public IMoonrakerAPI {
```

Replace `#include "moonraker_api.h"` with `#include "i_moonraker_api.h"` if the header only needs the interface.

- [ ] **Step 2: Build**

Many compile errors likely: methods the mock previously inherited from `MoonrakerAPI` (not pure virtual) are now undefined.

```bash
make -j 2>&1 | tee /tmp/build.log
grep -E "error.*pure|not defined|undefined reference" /tmp/build.log | head -30
```

- [ ] **Step 3: Implement every missing pure virtual on the mock**

For each error of the form `cannot allocate an object of abstract type 'MoonrakerAPIMock'`, add a trivial mock implementation. Example:

```cpp
void set_temperature(const std::string& heater, double temperature,
                     SuccessCallback on_success, ErrorCallback /*on_error*/) override {
    mock_heaters_[heater] = temperature;
    if (on_success) on_success();
}
```

Keep each implementation minimal — match current mock semantics where they existed; stub with a callback-firing no-op where they didn't.

- [ ] **Step 4: Build until clean**

Iterate.

- [ ] **Step 5: Build tests**

```bash
make test -j
```

Expected: clean.

- [ ] **Step 6: Run the full test suite**

```bash
make test-run
```

Expected: green. If Moonraker API tests fail, the mock's behavior changed — reconcile by checking the test expectations against the mock's new implementation.

- [ ] **Step 7: Commit**

```bash
git add include/moonraker_api_mock.h
git commit -m "refactor(moonraker): MoonrakerAPIMock inherits IMoonrakerAPI directly"
```

---

### Task 3.5: Update call sites to accept `IMoonrakerAPI`

**Files:**
- Multiple across `src/` and `include/` — use grep to find them.

- [ ] **Step 1: Find callers that hold `MoonrakerAPI*` / `MoonrakerAPI&`**

```bash
grep -rnE '\bMoonrakerAPI\s*[*&]' include/ src/ | grep -v moonraker_api.h | grep -v moonraker_api_mock.h | tee /tmp/api_callers.txt
```

- [ ] **Step 2: For each caller, decide: interface or concrete?**

Rule of thumb: if the caller only invokes methods declared on `IMoonrakerAPI`, change it to `IMoonrakerAPI*` / `IMoonrakerAPI&`. If it reaches for concrete-only methods (unlikely but possible), leave it as-is for now.

Typical edit in a caller header:

```cpp
class MyPanel {
public:
    MyPanel(MoonrakerAPI* api) : api_(api) {}   // before
    MyPanel(IMoonrakerAPI* api) : api_(api) {}  // after

private:
    MoonrakerAPI* api_;    // before
    IMoonrakerAPI* api_;   // after
};
```

Add `#include "i_moonraker_api.h"` where needed.

- [ ] **Step 3: Work through the grep list, committing every 5–10 callers**

Build between groups:

```bash
make -j 2>&1 | tail -20
```

Commit each batch:

```bash
git add -p  # review then stage
git commit -m "refactor(moonraker): hold IMoonrakerAPI pointers in <subsystem>"
```

- [ ] **Step 4: Full-suite verify**

```bash
make test-run
```

Expected: green.

---

### Task 3.6: `--test` smoke

- [ ] **Step 1: Launch and verify no crashes**

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/smoke.log &
SMOKE_PID=$!
sleep 5
kill $SMOKE_PID 2>/dev/null
grep -E "(error|crash|SIGSEGV|assert)" /tmp/smoke.log | head -10
```

Expected: no errors.

- [ ] **Step 2: Tag the phase**

```bash
git tag phase-3-complete
```

---

## Phase 4 — Extract `IMoonrakerClient`

Goal: same pattern as Phase 3 but for `MoonrakerClient`. Extra complication: the real class inherits `hv::WebSocketClient`; the current mock inherits `MoonrakerClient` (and thus transitively `hv::WebSocketClient`). After this phase, the mock inherits only `IMoonrakerClient` — no WebSocket baggage.

### Task 4.1: Enumerate the public API surface

**Files:**
- Read: `include/moonraker_client.h`

- [ ] **Step 1: Dump public methods**

```bash
awk '/^class MoonrakerClient/,/^};/' include/moonraker_client.h \
    | grep -E '^\s+(virtual\s+)?[a-zA-Z_:<>,&*\s]+\w+\s*\(' \
    | grep -v '^\s*//' \
    | tee /tmp/moonraker_client_surface.txt
```

- [ ] **Step 2: Identify callers**

```bash
grep -rn 'MoonrakerClient::' src/ | awk -F'::' '{print $2}' | awk '{print $1}' \
    | sort -u | tee /tmp/moonraker_client_used.txt
```

- [ ] **Step 3: Carefully separate WebSocket-specific methods from API methods**

Methods inherited from `hv::WebSocketClient` (like `send`, `close`, etc.) should NOT be in `IMoonrakerClient`. Only methods `MoonrakerClient` itself declares belong in the interface.

---

### Task 4.2: Create `IMoonrakerClient`

**Files:**
- Create: `include/i_moonraker_client.h`

- [ ] **Step 1: Write the interface**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

namespace helix {

// Abstract interface for MoonrakerClient — the WebSocket + JSON-RPC layer
// that talks to Moonraker. Callers that don't need the underlying libhv
// WebSocket API should depend on this interface. The concrete MoonrakerClient
// implements it; MoonrakerClientMock implements it directly (no WebSocket
// base class).
class IMoonrakerClient {
  public:
    virtual ~IMoonrakerClient() = default;

    // ====== Populate from /tmp/moonraker_client_surface.txt ======
    //
    // Entries to include: every method MoonrakerClient itself declares that
    // callers use.
    // Entries to EXCLUDE: anything inherited from hv::WebSocketClient.
    //
    // Example:

    virtual int connect(const char* url, std::function<void()> on_connected,
                        std::function<void()> on_disconnected) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    // ... more ...
};

} // namespace helix
```

**Note for implementer:** Same methodology as Task 3.2. Do not enumerate from memory. Read the real header, cross-check with caller usage, and translate each method that's genuinely Moonraker-specific (not libhv WebSocket machinery) into a pure virtual.

- [ ] **Step 2: Build the interface header alone**

```bash
echo '#include "i_moonraker_client.h"' > /tmp/i_client_check.cpp
g++ -std=c++17 -Iinclude -c /tmp/i_client_check.cpp -o /tmp/i_client_check.o 2>&1 | tail -20
```

Expected: compiles.

- [ ] **Step 3: Commit**

```bash
git add include/i_moonraker_client.h
git commit -m "feat(moonraker): add IMoonrakerClient pure-virtual interface"
```

---

### Task 4.3: Make `MoonrakerClient` inherit `IMoonrakerClient`

**Files:**
- Modify: `include/moonraker_client.h`

- [ ] **Step 1: Add the inheritance**

Edit `include/moonraker_client.h`:

```cpp
#include "i_moonraker_client.h"
```

Change:
```cpp
class MoonrakerClient : public hv::WebSocketClient {
```
to:
```cpp
class MoonrakerClient : public hv::WebSocketClient, public IMoonrakerClient {
```

Add `override` to every method now implementing an `IMoonrakerClient` pure virtual.

- [ ] **Step 2: Build**

```bash
make -j 2>&1 | tee /tmp/build.log
grep -E "error" /tmp/build.log | head -20
```

Reconcile any signature mismatches.

- [ ] **Step 3: Commit**

```bash
git add include/moonraker_client.h
git commit -m "refactor(moonraker): MoonrakerClient implements IMoonrakerClient"
```

---

### Task 4.4: Rebase `MoonrakerClientMock` on `IMoonrakerClient`

**Files:**
- Modify: `include/moonraker_client_mock.h`

This is the biggest single change in the plan: the mock currently inherits `MoonrakerClient` (transitively `hv::WebSocketClient`) and relies on that inheritance for some behavior (e.g., the WebSocket base's event dispatch). Rebasing requires the mock to stand alone.

- [ ] **Step 1: Change inheritance**

Edit `include/moonraker_client_mock.h`:

Change:
```cpp
class MoonrakerClientMock : public helix::MoonrakerClient {
```
to:
```cpp
class MoonrakerClientMock : public helix::IMoonrakerClient {
```

Replace `#include "moonraker_client.h"` with `#include "i_moonraker_client.h"`.

- [ ] **Step 2: Build — expect many errors**

```bash
make -j 2>&1 | tee /tmp/build.log
grep -E "error.*pure|undefined reference|abstract type" /tmp/build.log | head -40
```

For each missing method, add a mock implementation. If the mock currently leans on a protected helper from `MoonrakerClient` (e.g., internal request tracking), replicate just enough of that helper inside the mock, or stub it.

- [ ] **Step 3: Delete WebSocket-specific overrides that no longer apply**

The mock may currently override WebSocket callbacks like `onopen`, `onclose`, `onmessage`. Since the mock no longer inherits `hv::WebSocketClient`, delete those overrides — they're now dead code.

- [ ] **Step 4: Build until clean**

Iterate Steps 2–3.

- [ ] **Step 5: Build tests**

```bash
make test -j 2>&1 | tail -30
```

- [ ] **Step 6: Run full test suite**

```bash
make test-run 2>&1 | tee /tmp/test.log
tail -30 /tmp/test.log
```

Expected: green. If Moonraker client tests fail, the mock's behavior shifted — reconcile by reading the failure and adjusting the mock impl.

- [ ] **Step 7: Commit**

```bash
git add include/moonraker_client_mock.h
git commit -m "refactor(moonraker): MoonrakerClientMock inherits IMoonrakerClient only"
```

---

### Task 4.5: Update call sites to accept `IMoonrakerClient`

**Files:**
- Multiple across `src/` — use grep.

- [ ] **Step 1: Find callers**

```bash
grep -rnE '\bMoonrakerClient\s*[*&]' include/ src/ \
    | grep -v moonraker_client.h | grep -v moonraker_client_mock.h \
    | tee /tmp/client_callers.txt
```

- [ ] **Step 2: For each caller, prefer `helix::IMoonrakerClient*` unless it uses concrete-only methods**

Same procedure as Task 3.5. Update in batches, build between groups, commit each batch.

- [ ] **Step 3: Full-suite verify**

```bash
make test-run
```

Expected: green.

---

### Task 4.6: Drift smoke tests for the new interfaces

**Files:**
- Create: `tests/unit/test_interface_drift_moonraker.cpp`

- [ ] **Step 1: Write the smoke tests**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection for IMoonrakerAPI and IMoonrakerClient.

#include "catch_amalgamated.hpp"
#include "i_moonraker_api.h"
#include "i_moonraker_client.h"

#ifdef HELIX_ENABLE_MOCKS
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"

TEST_CASE("MoonrakerAPIMock satisfies IMoonrakerAPI interface", "[compile][drift]") {
    // Mock needs a client + state; use the mock client to avoid network.
    helix::MoonrakerClientMock client;
    helix::PrinterState state;
    state.init_subjects(false);

    std::unique_ptr<IMoonrakerAPI> p = std::make_unique<MoonrakerAPIMock>(client, state);
    REQUIRE(p != nullptr);
}

TEST_CASE("MoonrakerClientMock satisfies IMoonrakerClient interface", "[compile][drift]") {
    std::unique_ptr<helix::IMoonrakerClient> p = std::make_unique<helix::MoonrakerClientMock>();
    REQUIRE(p != nullptr);
    REQUIRE_FALSE(p->is_connected());
}
#endif
```

- [ ] **Step 2: Build and run**

```bash
make test -j && ./build/bin/helix-tests "[compile][drift]" -v
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_interface_drift_moonraker.cpp
git commit -m "test(drift): compile-time guards for IMoonrakerAPI and IMoonrakerClient"
```

---

### Task 4.7: `--test` smoke + phase gate

- [ ] **Step 1: Smoke `--test`**

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/smoke.log &
SMOKE_PID=$!
sleep 5
kill $SMOKE_PID 2>/dev/null
grep -E "(error|crash|SIGSEGV|assert)" /tmp/smoke.log | head -10
```

- [ ] **Step 2: Tag the phase**

```bash
git tag phase-4-complete
```

---

## Phase 5 — Verification and Cleanup

### Task 5.1: Dynamic-cast audit

- [ ] **Step 1: Scan for concrete-type reach-throughs**

```bash
grep -rnE 'dynamic_cast\s*<\s*(MoonrakerClient|MoonrakerAPI)\s*\*?\s*>' src/ include/ \
    | tee /tmp/dyn_cast_hits.txt
```

- [ ] **Step 2: Resolve each hit**

For each dynamic_cast to a concrete Moonraker type, decide:
- If the code only needs interface methods: remove the cast, use the interface pointer directly.
- If the code genuinely needs a concrete method not on the interface: add that method to the interface (preferred), or leave the cast with a comment documenting why.

Commit each group of fixes.

---

### Task 5.2: Audit remaining `HELIX_ENABLE_MOCKS` guards

- [ ] **Step 1: Scan**

```bash
grep -rn 'HELIX_ENABLE_MOCKS' include/ src/ | tee /tmp/mock_guards.txt
```

- [ ] **Step 2: Identify any guards that can be narrowed**

If a block of code exists only to branch between real and mock based on the runtime flag, confirm it's still necessary after the interface refactor. In most cases it still is (factories still select between real/mock at runtime). Leave `--test` behavior unchanged.

No commit required unless obsolete guards are found.

---

### Task 5.3: Update CLAUDE.md guidance

- [ ] **Step 1: Add to the Where Things Live section**

Edit `CLAUDE.md`. Under `include/` or a new "Interfaces" subsection, note:

```
**Mock-facing interfaces**: `IMoonrakerAPI`, `IMoonrakerClient`. Depend on these
from callers that don't need concrete-specific methods. Real + mock both
implement. Drift protection via `tests/unit/test_interface_drift_*.cpp`.

**Test isolation**: `HelixTestFixture` is the base for all test fixtures — it
resets process-wide singletons between tests. XML subject registration goes
through `helix::xml::register_subject_in_current_scope()` so test fixtures
can scope subjects per-test via `ScopedSubjectRegistryOverride`.
```

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(claude): reflect interface + test-isolation architecture"
```

---

### Task 5.4: Final full-suite and `--test` run

- [ ] **Step 1: Clean build everything**

```bash
make clean && make -j
```

- [ ] **Step 2: Run full test suite**

```bash
make test-run 2>&1 | tee /tmp/test.log
tail -20 /tmp/test.log
```

Expected: all green, no hangs, all four drift smoke tests passing.

- [ ] **Step 3: Smoke `--test`**

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/smoke.log &
SMOKE_PID=$!
sleep 5
kill $SMOKE_PID 2>/dev/null
grep -E "(error|crash|SIGSEGV)" /tmp/smoke.log | head -10
```

Expected: no errors.

- [ ] **Step 4: Tag final**

```bash
git tag test-mock-decoupling-complete
```

- [ ] **Step 5: Ready-to-merge check**

The worktree branch is now ready to merge into `main`. Before merging:
- Confirm `make pi-test` still passes (cross-compile + deploy + run).
- Confirm one cross-platform build still passes (e.g., `make android-docker` or `make k1-docker`).
- If either fails, the issue is likely a missed interface import in a platform-specific file — grep for `i_moonraker_*` includes and add where needed.

---

## Deferred / Explicit Non-Goals

(Preserved from the spec so an engineer reading this plan without the spec knows what's intentionally out of scope.)

- Removing `is_test_mode()` branches in production code.
- Consolidating or retiring the 42 `*TestAccess` friend classes.
- Moving to constructor-injected dependencies (`RuntimeConfig` as true DI).
- Splitting the test binary for parallel execution.
- Stripping mocks from the production binary.
