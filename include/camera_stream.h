// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"

// Camera features available on Pi/desktop. JPEG decoding uses stb_image.
#if HELIX_HAS_CAMERA

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class HttpRequest;

namespace helix {

/**
 * @brief MJPEG camera stream decoder with snapshot fallback
 *
 * Connects to an MJPEG stream URL, parses multipart boundaries, decodes
 * JPEG frames via stb_image, and delivers decoded RGB888 frames via callback.
 * Falls back to periodic snapshot polling if streaming fails.
 *
 * Threading: decode happens on a background thread. The on_frame callback is
 * called from that thread — callers must use ui_queue_update() to marshal to
 * the LVGL thread.
 */
class CameraStream {
  public:
    using FrameCallback = std::function<void(lv_draw_buf_t* frame)>;
    using ErrorCallback = std::function<void(const char* message)>;

    CameraStream() = default;
    ~CameraStream();

    CameraStream(const CameraStream&) = delete;
    CameraStream& operator=(const CameraStream&) = delete;

    void set_flip(bool horizontal, bool vertical) {
        flip_h_.store(horizontal);
        flip_v_.store(vertical);
    }

    /**
     * @brief Copy RGB pixels to LVGL BGR format with optional flip.
     *
     * stb_image outputs R,G,B byte order but LVGL's RGB888 stores B,G,R in
     * memory (matching lv_color_t layout). This helper swaps R↔B during copy.
     * Exposed as public static for unit testing.
     */
    static void copy_pixels_rgb_to_lvgl(const uint8_t* src, uint8_t* dst, int width, int height,
                                        int src_stride, int dst_stride, bool flip_h, bool flip_v);

    /**
     * @brief Parse a boundary string from a Content-Type header value.
     *
     * Extracts the boundary parameter, strips quotes, and prepends "--" if
     * needed. Returns empty string if no boundary found. Exposed for testing.
     */
    static std::string parse_boundary(const std::string& content_type);

    void start(const std::string& stream_url, const std::string& snapshot_url,
               FrameCallback on_frame, ErrorCallback on_error = nullptr);
    void stop();
    bool is_running() const;

    /// Called by widget when it has consumed the current frame
    void frame_consumed();

  private:
    int process_stream_data();
    void stream_thread_func();
    void snapshot_poll_loop();
    void fetch_snapshot();
    bool decode_jpeg(const uint8_t* data, size_t len);
    void deliver_frame();
    void ensure_buffers(int width, int height);
    void free_buffers();

    std::string stream_url_;
    std::string snapshot_url_;
    FrameCallback on_frame_;
    ErrorCallback on_error_;
    std::atomic<bool> flip_h_{false};
    std::atomic<bool> flip_v_{false};

    // Draw buffer helpers — use system malloc (thread-safe) instead of
    // lv_draw_buf_create which calls lv_malloc (NOT thread-safe)
    static lv_draw_buf_t* create_draw_buf(uint32_t w, uint32_t h, lv_color_format_t cf);
    static void destroy_draw_buf(lv_draw_buf_t* buf);

    // Double buffer — decode into back, swap to front on delivery
    lv_draw_buf_t* front_buf_ = nullptr;
    lv_draw_buf_t* back_buf_ = nullptr;
    int frame_width_ = 0;
    int frame_height_ = 0;
    std::mutex buf_mutex_;

    // Old front buffers awaiting safe cleanup — LVGL may still reference
    // them via lv_image_set_src until the widget clears the source
    std::vector<lv_draw_buf_t*> retired_bufs_;

    // MJPEG parser state
    std::vector<uint8_t> recv_buf_;
    std::string boundary_;

    // Active HTTP request for streaming — stored for cancellation in stop()
    std::shared_ptr<HttpRequest> active_req_;
    std::mutex req_mutex_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> frame_pending_{false};
    int stream_fail_count_ = 0;
    std::thread stream_thread_;

    static constexpr int kMaxStreamFailures = 3;
    static constexpr int kSnapshotIntervalMs = 2000;
    static constexpr int kStreamTimeoutSec = 30;
};

} // namespace helix

#endif // HELIX_HAS_CAMERA
