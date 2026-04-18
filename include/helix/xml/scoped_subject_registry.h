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
