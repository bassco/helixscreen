// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

namespace helix::timelapse {

class TimelapseThumbnailer {
  public:
    static std::string cache_key(const std::string& video_filename);
    static std::string companion_filename(const std::string& video_filename);
    static std::string ffmpeg_extract_command(const std::string& input_path,
                                              const std::string& output_path);
    static bool is_video_file(const std::string& filename);
};

}  // namespace helix::timelapse
