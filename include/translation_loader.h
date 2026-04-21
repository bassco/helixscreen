// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix::ui {

/**
 * @brief Ensure the given locale's translation pack is loaded into LVGL.
 *
 * HelixScreen ships one XML per locale (ui_xml/translations/<lang>.xml) so the
 * app can load only the current language at startup (~60-80 KB heap) instead
 * of the combined 9-language translations.xml (~500-700 KB heap).
 *
 * LVGL's translation system has no remove API — once a pack is registered it
 * stays until lv_translation_deinit(). This function de-duplicates via an
 * internal set so repeated calls for the same lang are no-ops. As the user
 * cycles between languages during a session, multiple packs accumulate, but
 * typical usage touches one or two.
 *
 * English IS loaded too (even though tags ARE English) — otherwise every
 * lv_tr() call that doesn't find a matching pack logs a warning. The ~140 KB
 * heap cost of en.xml is tolerable vs. warning-per-call log spam.
 *
 * @param lang Locale code, e.g. "en", "de", "zh"
 */
void ensure_translation_loaded(const std::string& lang);

} // namespace helix::ui
