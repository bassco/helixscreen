// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_exclude_object_map_view.h"
#include <algorithm>

namespace helix::ui {

ExcludeObjectMapView::CoordMapper::CoordMapper(
    float bed_w_mm, float bed_h_mm, int viewport_w_px, int viewport_h_px)
    : viewport_h_(viewport_h_px) {
    float scale_x = static_cast<float>(viewport_w_px) / bed_w_mm;
    float scale_y = static_cast<float>(viewport_h_px) / bed_h_mm;
    scale_ = std::min(scale_x, scale_y);
    float rendered_w = bed_w_mm * scale_;
    float rendered_h = bed_h_mm * scale_;
    offset_x_ = (static_cast<float>(viewport_w_px) - rendered_w) / 2.0f;
    offset_y_ = (static_cast<float>(viewport_h_px) - rendered_h) / 2.0f;
}

std::pair<float, float> ExcludeObjectMapView::CoordMapper::mm_to_px(
    float x_mm, float y_mm) const {
    float px = offset_x_ + x_mm * scale_;
    float bed_h_px = static_cast<float>(viewport_h_) - 2.0f * offset_y_;
    float py = offset_y_ + bed_h_px - y_mm * scale_;
    return {px, py};
}

ExcludeObjectMapView::PixelRect ExcludeObjectMapView::CoordMapper::bbox_to_rect(
    glm::vec2 bbox_min, glm::vec2 bbox_max) const {
    auto [x1, y1] = mm_to_px(bbox_min.x, bbox_max.y);
    auto [x2, y2] = mm_to_px(bbox_max.x, bbox_min.y);
    float raw_w = x2 - x1, raw_h = y2 - y1;
    float w = std::max(raw_w, MIN_TOUCH_TARGET_PX);
    float h = std::max(raw_h, MIN_TOUCH_TARGET_PX);
    if (w > raw_w) x1 -= (w - raw_w) / 2.0f;
    if (h > raw_h) y1 -= (h - raw_h) / 2.0f;
    return {x1, y1, w, h};
}

}  // namespace helix::ui
