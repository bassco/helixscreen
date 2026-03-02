// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera_stream.h"

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "hv/requests.h"
#include "spdlog/spdlog.h"
#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string_view>

namespace helix {

// ============================================================================
// Construction / Destruction
// ============================================================================

CameraStream::~CameraStream() {
    stop();
}

// ============================================================================
// Public API
// ============================================================================

void CameraStream::start(const std::string& stream_url, const std::string& snapshot_url,
                          FrameCallback on_frame, ErrorCallback on_error) {
    if (running_.load()) {
        stop();
    }

    stream_url_ = stream_url;
    snapshot_url_ = snapshot_url;
    on_frame_ = std::move(on_frame);
    on_error_ = std::move(on_error);
    stream_fail_count_ = 0;
    frame_pending_.store(false);
    running_.store(true);

    spdlog::info("[CameraStream] Starting — stream={}, snapshot={}", stream_url_, snapshot_url_);

    if (!stream_url_.empty()) {
        spdlog::info("[CameraStream] Using MJPEG streaming mode");
        stream_thread_ = std::thread(&CameraStream::stream_thread_func, this);
    } else if (!snapshot_url_.empty()) {
        spdlog::info("[CameraStream] Using snapshot mode (interval={}ms)", kSnapshotIntervalMs);
        stream_thread_ = std::thread(&CameraStream::snapshot_poll_loop, this);
    } else {
        spdlog::warn("[CameraStream] No stream or snapshot URL provided");
        running_.store(false);
    }
}

void CameraStream::stop() {
    bool was_running = running_.exchange(false);

    if (was_running) {
        spdlog::info("[CameraStream] Stopping");

        // Cancel any in-flight HTTP request to unblock the stream thread
        {
            std::lock_guard<std::mutex> lock(req_mutex_);
            if (active_req_) {
                active_req_->Cancel();
            }
        }
    }

    // Always join/detach a joinable thread — even if running_ was already
    // false (e.g. called from ~CameraStream after an earlier stop()).
    // Destroying a joinable std::thread calls std::terminate().
    bool thread_joined = true;
    if (stream_thread_.joinable()) {
        auto joined = std::make_shared<std::atomic<bool>>(false);
        std::thread join_helper([this, joined]() {
            stream_thread_.join();
            joined->store(true);
        });

        auto start = std::chrono::steady_clock::now();
        constexpr auto kJoinTimeout = std::chrono::seconds(2);
        constexpr auto kPollInterval = std::chrono::milliseconds(50);

        while (!joined->load()) {
            if (std::chrono::steady_clock::now() - start > kJoinTimeout) {
                spdlog::warn("[CameraStream] Stream thread join timed out, detaching");
                join_helper.detach();
                stream_thread_.detach();
                thread_joined = false;
                break;
            }
            std::this_thread::sleep_for(kPollInterval);
        }

        if (join_helper.joinable()) {
            join_helper.join();
        }
    }

    if (was_running) {
        if (thread_joined) {
            // Safe to free — stream thread has exited
            free_buffers();
        } else {
            // Stream thread still running — leak buffers to avoid UAF.
            // They are small (~1.8MB for 640x480 double buffer) and this
            // only happens when the camera connection is stuck on shutdown.
            spdlog::warn("[CameraStream] Leaking draw buffers (stream thread still running)");
            front_buf_ = nullptr;
            back_buf_ = nullptr;
            retired_bufs_.clear();
            frame_width_ = 0;
            frame_height_ = 0;
        }
        recv_buf_.clear();
        boundary_.clear();
        on_frame_ = nullptr;
        on_error_ = nullptr;
    }
}

bool CameraStream::is_running() const {
    return running_.load();
}

void CameraStream::frame_consumed() {
    frame_pending_.store(false);
}

// ============================================================================
// Boundary Parsing
// ============================================================================

std::string CameraStream::parse_boundary(const std::string& content_type) {
    auto pos = content_type.find("boundary=");
    if (pos == std::string::npos) {
        return {};
    }

    std::string boundary = content_type.substr(pos + 9);
    // Strip trailing parameters (after ';' or whitespace)
    auto end = boundary.find_first_of("; \t");
    if (end != std::string::npos) {
        boundary = boundary.substr(0, end);
    }
    // Strip quotes
    if (!boundary.empty() && boundary.front() == '"') {
        boundary = boundary.substr(1);
    }
    if (!boundary.empty() && boundary.back() == '"') {
        boundary.pop_back();
    }
    // Ensure "--" prefix
    if (boundary.size() >= 2 && boundary.substr(0, 2) == "--") {
        return boundary;
    }
    return "--" + boundary;
}

// ============================================================================
// MJPEG Stream Thread (http_cb streaming)
// ============================================================================

void CameraStream::stream_thread_func() {
    try {
    spdlog::debug("[CameraStream] Stream thread started for {}", stream_url_);
    recv_buf_.clear();
    recv_buf_.reserve(256 * 1024);
    boundary_.clear();
    got_stream_data_.store(false);
    bool ever_connected = false;

    while (running_.load() && stream_fail_count_ < kMaxStreamFailures) {
        auto req = std::make_shared<HttpRequest>();
        req->method = HTTP_GET;
        req->url = stream_url_;
        // Short timeout until the first successful connection, then long
        // timeout for the persistent stream. MJPEG responses are infinite —
        // the timeout just drives periodic reconnection.
        int timeout = ever_connected ? kStreamTimeoutSec : kStreamConnectTimeoutSec;
        req->timeout = timeout;
        spdlog::debug("[CameraStream] Attempting stream connection to {} (timeout={}s, attempt={})",
                      stream_url_, timeout, stream_fail_count_ + 1);

        // Store the request for cancellation by stop()
        {
            std::lock_guard<std::mutex> lock(req_mutex_);
            active_req_ = req;
        }

        // Set up http_cb for incremental MJPEG parsing. The callback fires as
        // HTTP data arrives: HP_HEADERS_COMPLETE once headers are ready, then
        // HP_BODY repeatedly with each chunk of the multipart body.
        req->http_cb = [this](HttpMessage* resp, http_parser_state state,
                              const char* data, size_t size) {
            if (!running_.load()) {
                // Cancel the request from within the callback
                std::lock_guard<std::mutex> lock(req_mutex_);
                if (active_req_) {
                    active_req_->Cancel();
                }
                return;
            }

            if (state == HP_HEADERS_COMPLETE) {
                std::string content_type = resp->GetHeader("Content-Type");
                boundary_ = parse_boundary(content_type);
                if (boundary_.empty()) {
                    spdlog::warn("[CameraStream] No boundary in Content-Type: {}", content_type);
                } else {
                    got_stream_data_.store(true);
                }
                spdlog::debug("[CameraStream] Stream connected, boundary='{}'", boundary_);
            } else if (state == HP_BODY) {
                if (data && size > 0) {
                    recv_buf_.insert(recv_buf_.end(), data, data + size);

                    // Cap buffer growth — if camera sends faster than we decode
                    // (e.g. frame_pending_ blocks consumption), drop stale data
                    if (recv_buf_.size() > 4 * 1024 * 1024) {
                        spdlog::warn("[CameraStream] recv_buf_ exceeded 4MB, clearing");
                        recv_buf_.clear();
                    } else if (!boundary_.empty()) {
                        process_stream_data();
                    }
                }
            }
        };

        auto resp = requests::request(req);

        // Clear active request
        {
            std::lock_guard<std::mutex> lock(req_mutex_);
            active_req_ = nullptr;
        }

        if (!running_.load()) {
            break;
        }

        // If boundary was empty and we got body data, try as single JPEG
        if (boundary_.empty() && resp && !resp->body.empty()) {
            auto* body_data = reinterpret_cast<const uint8_t*>(resp->body.data());
            if (decode_jpeg(body_data, resp->body.size())) {
                deliver_frame();
            }
        }

        // Evaluate success: MJPEG streams are infinite so requests::request()
        // always returns with status=-1 (timeout). If http_cb received data,
        // the stream was healthy — the timeout just ended a streaming session.
        bool had_data = got_stream_data_.load();
        bool got_response = resp && resp->status_code >= 200 && resp->status_code < 300;

        if (had_data) {
            // Stream was flowing — timeout is normal, not a failure
            spdlog::debug("[CameraStream] Stream session ended (received data, reconnecting)");
            stream_fail_count_ = 0;
            ever_connected = true;
        } else if (!got_response) {
            spdlog::warn("[CameraStream] Stream request failed (status={})",
                         resp ? static_cast<int>(resp->status_code) : -1);
            stream_fail_count_++;
        } else if (boundary_.empty()) {
            // Got HTTP 200 but no multipart boundary — not an MJPEG stream
            stream_fail_count_++;
        } else {
            stream_fail_count_ = 0;
        }

        // Clear partial data before reconnecting
        recv_buf_.clear();
        boundary_.clear();
        got_stream_data_.store(false);

        if (running_.load() && stream_fail_count_ < kMaxStreamFailures) {
            // Brief backoff before reconnecting
            int backoff_ms = std::min(1000 * stream_fail_count_, 5000);
            if (backoff_ms > 0) {
                for (int i = 0; i < backoff_ms / 100 && running_.load(); i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
    }

    // Fall back to snapshot mode if streaming failed
    if (running_.load() && stream_fail_count_ >= kMaxStreamFailures) {
        spdlog::warn("[CameraStream] Stream failed {} times, falling back to snapshot mode",
                     stream_fail_count_);
        if (!snapshot_url_.empty()) {
            if (on_error_) on_error_("Stream failed, trying snapshots...");
            snapshot_poll_loop();
        } else {
            if (on_error_) on_error_("Stream unavailable");
        }
    }

    spdlog::debug("[CameraStream] Stream thread exiting");
    } catch (const std::bad_alloc& e) {
        spdlog::error("[CameraStream] Out of memory in stream thread: {}", e.what());
        recv_buf_.clear();
        recv_buf_.shrink_to_fit();
        if (on_error_) on_error_("Out of memory");
    } catch (const std::exception& e) {
        spdlog::error("[CameraStream] Uncaught exception in stream thread: {}", e.what());
        if (on_error_) on_error_(e.what());
    }
}

// ============================================================================
// Incremental MJPEG Parser
// ============================================================================

int CameraStream::process_stream_data() {
    int frames_decoded = 0;

    while (running_.load()) {
        // Find boundary in the receive buffer (string_view avoids copying)
        std::string_view buf_view(reinterpret_cast<const char*>(recv_buf_.data()),
                                  recv_buf_.size());
        auto bpos = buf_view.find(boundary_);
        if (bpos == std::string::npos) {
            break;
        }

        // Find end of part headers (double CRLF after boundary)
        auto header_end = buf_view.find("\r\n\r\n", bpos);
        if (header_end == std::string::npos) {
            // Incomplete headers — wait for more data
            break;
        }
        size_t jpeg_start = header_end + 4;

        // Find next boundary to delimit JPEG data end
        auto next_bpos = buf_view.find(boundary_, jpeg_start);
        if (next_bpos == std::string::npos) {
            // No closing boundary yet — wait for more data (unless buffer is huge,
            // in which case the data before the current boundary is stale)
            if (bpos > 0) {
                // Trim everything before the current boundary to limit growth
                recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + static_cast<long>(bpos));
            }
            break;
        }

        // Extract JPEG data, strip trailing CRLF
        size_t jpeg_len = next_bpos - jpeg_start;
        while (jpeg_len > 0 && (recv_buf_[jpeg_start + jpeg_len - 1] == '\r' ||
                                recv_buf_[jpeg_start + jpeg_len - 1] == '\n')) {
            jpeg_len--;
        }

        // Consume this part from the buffer (up to the next boundary)
        // Keep next_bpos as the start so the next iteration finds it
        bool decoded = false;
        if (jpeg_len > 0 && !frame_pending_.load()) {
            decoded = decode_jpeg(recv_buf_.data() + jpeg_start, jpeg_len);
            if (decoded) {
                deliver_frame();
                frames_decoded++;
            }
        }

        // Erase consumed data up to the next boundary
        recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + static_cast<long>(next_bpos));
    }

    return frames_decoded;
}

void CameraStream::snapshot_poll_loop() {
    spdlog::info("[CameraStream] Starting snapshot poll loop (interval={}ms)", kSnapshotIntervalMs);

    while (running_.load()) {
        if (!frame_pending_.load()) {
            fetch_snapshot();
        }
        // Sleep in small increments to check running_ flag
        for (int i = 0; i < kSnapshotIntervalMs / 100 && running_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void CameraStream::fetch_snapshot() {
    if (snapshot_url_.empty()) {
        return;
    }

    spdlog::trace("[CameraStream] Fetching snapshot from {}", snapshot_url_);

    auto req = std::make_shared<HttpRequest>();
    req->method = HTTP_GET;
    req->url = snapshot_url_;
    req->timeout = kStreamTimeoutSec;

    auto resp = requests::request(req);

    // Check running_ after blocking HTTP call — stop() may have been called
    if (!running_.load()) {
        return;
    }

    if (!resp || resp->status_code < 200 || resp->status_code >= 300) {
        spdlog::debug("[CameraStream] Snapshot fetch failed (status={})",
                      resp ? static_cast<int>(resp->status_code) : -1);
        return;
    }

    if (resp->body.empty()) {
        return;
    }

    auto* data = reinterpret_cast<const uint8_t*>(resp->body.data());
    if (decode_jpeg(data, resp->body.size())) {
        deliver_frame();
    }
}

// ============================================================================
// Pixel Copy: RGB (stb_image) → BGR (LVGL RGB888)
// ============================================================================

void CameraStream::copy_pixels_rgb_to_lvgl(const uint8_t* src, uint8_t* dst, int width,
                                            int height, int src_stride, int dst_stride,
                                            bool flip_h, bool flip_v) {
    for (int y = 0; y < height; y++) {
        int src_y = flip_v ? (height - 1 - y) : y;
        const uint8_t* src_row = src + src_y * src_stride;
        uint8_t* dst_row = dst + y * dst_stride;

        if (flip_h) {
            for (int x = 0; x < width; x++) {
                int src_x = width - 1 - x;
                // Swap R↔B: src is R,G,B → dst is B,G,R
                dst_row[x * 3 + 0] = src_row[src_x * 3 + 2];
                dst_row[x * 3 + 1] = src_row[src_x * 3 + 1];
                dst_row[x * 3 + 2] = src_row[src_x * 3 + 0];
            }
        } else {
            for (int x = 0; x < width; x++) {
                // Swap R↔B: src is R,G,B → dst is B,G,R
                dst_row[x * 3 + 0] = src_row[x * 3 + 2];
                dst_row[x * 3 + 1] = src_row[x * 3 + 1];
                dst_row[x * 3 + 2] = src_row[x * 3 + 0];
            }
        }
    }
}

// ============================================================================
// JPEG Decode (using stb_image — no external dependency)
// ============================================================================

bool CameraStream::decode_jpeg(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return false;
    }

    // Validate JPEG SOI marker
    if (len < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        spdlog::debug("[CameraStream] Invalid JPEG data (no SOI marker)");
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    // Decode JPEG to RGB (3 channels)
    uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(len), &width, &height,
                                            &channels, 3);
    if (!pixels) {
        spdlog::debug("[CameraStream] JPEG decode failed: {}", stbi_failure_reason());
        return false;
    }

    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        spdlog::warn("[CameraStream] Invalid JPEG dimensions: {}x{}", width, height);
        stbi_image_free(pixels);
        return false;
    }

    ensure_buffers(width, height);

    if (!back_buf_) {
        stbi_image_free(pixels);
        return false;
    }

    // Copy pixels from stb_image RGB to LVGL BGR, applying flip if configured
    auto* dst = static_cast<uint8_t*>(back_buf_->data);
    int dst_stride = static_cast<int>(back_buf_->header.stride);
    int src_stride = width * 3;

    copy_pixels_rgb_to_lvgl(pixels, dst, width, height, src_stride, dst_stride,
                            flip_h_.load(), flip_v_.load());

    stbi_image_free(pixels);
    spdlog::trace("[CameraStream] Decoded frame {}x{}", width, height);
    return true;
}

// ============================================================================
// Frame Delivery
// ============================================================================

void CameraStream::deliver_frame() {
    if (!on_frame_ || !back_buf_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        std::swap(front_buf_, back_buf_);
    }

    frame_pending_.store(true);
    on_frame_(front_buf_);
}

// ============================================================================
// Buffer Management
// ============================================================================

// Allocate a draw buffer using the system allocator (calloc/free) instead of
// lv_draw_buf_create which calls lv_malloc — LVGL's allocator is NOT
// thread-safe and these run on the background stream thread.
lv_draw_buf_t* CameraStream::create_draw_buf(uint32_t w, uint32_t h, lv_color_format_t cf) {
    auto* buf = static_cast<lv_draw_buf_t*>(calloc(1, sizeof(lv_draw_buf_t)));
    if (!buf) return nullptr;

    uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
    uint32_t data_size = stride * h;

    auto* data = calloc(1, data_size);
    if (!data) {
        free(buf);
        return nullptr;
    }

    lv_draw_buf_init(buf, w, h, cf, stride, data, data_size);
    lv_draw_buf_set_flag(buf, LV_IMAGE_FLAGS_MODIFIABLE);
    return buf;
}

void CameraStream::destroy_draw_buf(lv_draw_buf_t* buf) {
    if (!buf) return;
    free(buf->data);
    free(buf);
}

void CameraStream::ensure_buffers(int width, int height) {
    if (front_buf_ && frame_width_ == width && frame_height_ == height) {
        return;
    }

    spdlog::debug("[CameraStream] Allocating buffers for {}x{}", width, height);

    // Retire old front buffer — LVGL may still reference it via lv_image_set_src
    // until the widget processes the next frame and updates the source pointer.
    // Retired buffers are freed in free_buffers() after the thread joins.
    if (front_buf_) {
        retired_bufs_.push_back(front_buf_);
        front_buf_ = nullptr;
    }
    if (back_buf_) {
        destroy_draw_buf(back_buf_);
        back_buf_ = nullptr;
    }

    auto w = static_cast<uint32_t>(width);
    auto h = static_cast<uint32_t>(height);
    front_buf_ = create_draw_buf(w, h, LV_COLOR_FORMAT_RGB888);
    back_buf_ = create_draw_buf(w, h, LV_COLOR_FORMAT_RGB888);

    if (!front_buf_ || !back_buf_) {
        spdlog::error("[CameraStream] Failed to allocate draw buffers for {}x{}", width, height);
        destroy_draw_buf(front_buf_);
        destroy_draw_buf(back_buf_);
        front_buf_ = nullptr;
        back_buf_ = nullptr;
        return;
    }

    frame_width_ = width;
    frame_height_ = height;
}

void CameraStream::free_buffers() {
    std::lock_guard<std::mutex> lock(buf_mutex_);
    destroy_draw_buf(front_buf_);
    front_buf_ = nullptr;
    destroy_draw_buf(back_buf_);
    back_buf_ = nullptr;
    for (auto* buf : retired_bufs_) {
        destroy_draw_buf(buf);
    }
    retired_bufs_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
}

} // namespace helix

#endif // HELIX_HAS_CAMERA
