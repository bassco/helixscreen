// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../catch_amalgamated.hpp"

#include "timelapse_thumbnailer.h"

using namespace helix::timelapse;

TEST_CASE("Thumbnailer: cache key generation", "[timelapse][thumbnailer]") {
    SECTION("different videos produce different keys") {
        auto key1 = TimelapseThumbnailer::cache_key("benchy_20260312.mp4");
        auto key2 = TimelapseThumbnailer::cache_key("vase_spiral.mp4");
        REQUIRE(key1 != key2);
    }

    SECTION("same video produces same key") {
        auto key1 = TimelapseThumbnailer::cache_key("benchy.mp4");
        auto key2 = TimelapseThumbnailer::cache_key("benchy.mp4");
        REQUIRE(key1 == key2);
    }

    SECTION("key has timelapse prefix") {
        auto key = TimelapseThumbnailer::cache_key("test.mp4");
        REQUIRE(key.find("tl_") == 0);
    }
}

TEST_CASE("Thumbnailer: companion filename", "[timelapse][thumbnailer]") {
    SECTION("mp4 gets .thumb.jpg companion") {
        REQUIRE(TimelapseThumbnailer::companion_filename("benchy.mp4") == "benchy.thumb.jpg");
    }

    SECTION("mkv gets .thumb.jpg companion") {
        REQUIRE(TimelapseThumbnailer::companion_filename("print.mkv") == "print.thumb.jpg");
    }
}

TEST_CASE("Thumbnailer: ffmpeg command construction", "[timelapse][thumbnailer]") {
    auto cmd = TimelapseThumbnailer::ffmpeg_extract_command(
        "/home/pi/printer_data/timelapse/benchy.mp4",
        "/home/pi/printer_data/timelapse/benchy.thumb.jpg");

    REQUIRE(cmd.find("ffmpeg") != std::string::npos);
    REQUIRE(cmd.find("-i") != std::string::npos);
    REQUIRE(cmd.find("benchy.mp4") != std::string::npos);
    REQUIRE(cmd.find("-vframes 1") != std::string::npos);
    REQUIRE(cmd.find("benchy.thumb.jpg") != std::string::npos);
}

TEST_CASE("Thumbnailer: video file filtering", "[timelapse][thumbnailer]") {
    REQUIRE(TimelapseThumbnailer::is_video_file("benchy.mp4") == true);
    REQUIRE(TimelapseThumbnailer::is_video_file("print.mkv") == true);
    REQUIRE(TimelapseThumbnailer::is_video_file("timelapse.avi") == true);
    REQUIRE(TimelapseThumbnailer::is_video_file("thumbnail.jpg") == false);
    REQUIRE(TimelapseThumbnailer::is_video_file("benchy.thumb.jpg") == false);
    REQUIRE(TimelapseThumbnailer::is_video_file("config.cfg") == false);
    REQUIRE(TimelapseThumbnailer::is_video_file("") == false);
}
