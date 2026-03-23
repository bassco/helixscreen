#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later

#include <glm/vec2.hpp>
#include <cmath>
#include <utility>

namespace helix::ui {

class ExcludeObjectMapView {
  public:
    static constexpr float MIN_TOUCH_TARGET_PX = 28.0f;

    struct PixelRect { float x, y, w, h; };

    class CoordMapper {
      public:
        CoordMapper(float bed_w_mm, float bed_h_mm, int viewport_w_px, int viewport_h_px);
        std::pair<float, float> mm_to_px(float x_mm, float y_mm) const;
        PixelRect bbox_to_rect(glm::vec2 bbox_min, glm::vec2 bbox_max) const;
        float scale() const { return scale_; }

      private:
        float scale_{1.0f};
        float offset_x_{0.0f};
        float offset_y_{0.0f};
        int viewport_h_{0};
    };
};

}  // namespace helix::ui
