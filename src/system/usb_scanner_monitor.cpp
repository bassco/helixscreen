// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_scanner_monitor.h"

#include "input_device_scanner.h"
#include "qr_decoder.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#endif

namespace helix {

UsbScannerMonitor::~UsbScannerMonitor()
{
    stop();
}

char UsbScannerMonitor::keycode_to_char(int keycode, bool shift)
{
    // Number row: KEY_1(2)..KEY_9(10), KEY_0(11)
    if (keycode >= 2 && keycode <= 10) {
        if (shift) {
            static const char shifted_nums[] = "!@#$%^&*(";
            return shifted_nums[keycode - 2];
        }
        return '0' + (keycode - 1);
    }
    if (keycode == 11) return shift ? ')' : '0';

    if (keycode == 12) return shift ? '_' : '-';
    if (keycode == 13) return shift ? '+' : '=';

    // Letter keys by physical layout
    static const struct { int keycode; char lower; } letter_map[] = {
        {16, 'q'}, {17, 'w'}, {18, 'e'}, {19, 'r'}, {20, 't'},
        {21, 'y'}, {22, 'u'}, {23, 'i'}, {24, 'o'}, {25, 'p'},
        {30, 'a'}, {31, 's'}, {32, 'd'}, {33, 'f'}, {34, 'g'},
        {35, 'h'}, {36, 'j'}, {37, 'k'}, {38, 'l'},
        {44, 'z'}, {45, 'x'}, {46, 'c'}, {47, 'v'}, {48, 'b'},
        {49, 'n'}, {50, 'm'},
    };
    for (const auto& entry : letter_map) {
        if (entry.keycode == keycode)
            return shift ? static_cast<char>(std::toupper(entry.lower)) : entry.lower;
    }

    if (keycode == 39) return shift ? ':' : ';';
    if (keycode == 40) return shift ? '"' : '\'';
    if (keycode == 41) return shift ? '~' : '`';
    if (keycode == 43) return shift ? '|' : '\\';
    if (keycode == 26) return shift ? '{' : '[';
    if (keycode == 27) return shift ? '}' : ']';
    if (keycode == 51) return shift ? '<' : ',';
    if (keycode == 52) return shift ? '>' : '.';
    if (keycode == 53) return shift ? '?' : '/';
    if (keycode == 57) return ' ';   // KEY_SPACE

    return 0;
}

int UsbScannerMonitor::check_spoolman_pattern(const std::string& input)
{
    return QrDecoder::parse_spoolman_id(input);
}

#ifdef __linux__

std::vector<std::string> UsbScannerMonitor::find_scanner_devices()
{
    // Check if user has manually configured a specific scanner device
    std::string configured_id = helix::SettingsManager::instance().get_scanner_device_id();

    auto devices = helix::input::find_hid_keyboard_devices(
        "/dev/input", "/sys/class/input", configured_id);
    if (devices.empty()) return {};

    spdlog::info("UsbScannerMonitor: using HID device: {} ({})",
                 devices[0].path, devices[0].name);
    return {devices[0].path};
}

void UsbScannerMonitor::start(ScanCallback on_scan)
{
    if (running_.load()) {
        spdlog::warn("UsbScannerMonitor: already running");
        return;
    }

    callback_ = std::move(on_scan);

    if (pipe(stop_pipe_) != 0) {
        spdlog::error("UsbScannerMonitor: failed to create pipe: {}", strerror(errno));
        return;
    }

    running_.store(true);
    monitor_thread_ = std::thread(&UsbScannerMonitor::monitor_thread_func, this);
    spdlog::info("UsbScannerMonitor: started");
}

void UsbScannerMonitor::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (stop_pipe_[1] >= 0) {
        char c = 'x';
        (void)write(stop_pipe_[1], &c, 1);
    }

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    if (stop_pipe_[0] >= 0) { close(stop_pipe_[0]); stop_pipe_[0] = -1; }
    if (stop_pipe_[1] >= 0) { close(stop_pipe_[1]); stop_pipe_[1] = -1; }

    spdlog::info("UsbScannerMonitor: stopped");
}

void UsbScannerMonitor::monitor_thread_func()
{
    spdlog::debug("UsbScannerMonitor: monitor thread started");

    std::vector<int> device_fds;
    std::string accumulator;
    bool capslock_on = false;
    bool shift_held = false;
    std::chrono::steady_clock::time_point last_key_time{};

    auto open_devices = [&]() {
        for (int fd : device_fds) close(fd);
        device_fds.clear();
        capslock_on = false;
        shift_held = false;
        accumulator.clear();

        auto paths = find_scanner_devices();
        for (const auto& path : paths) {
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                spdlog::warn("UsbScannerMonitor: failed to open {}: {}", path, strerror(errno));
                continue;
            }
            // NOTE: Do NOT use EVIOCGRAB! Many barcode scanners toggle Caps Lock
            // before/after typing characters. The kernel's `leds` handler must
            // receive the Caps Lock event to send the LED state update back to
            // the scanner. Without it, the scanner waits forever for LED
            // confirmation and retries Caps Lock indefinitely.
            device_fds.push_back(fd);
        }
    };

    open_devices();

    while (running_.load()) {
        // Build poll fd list: stop_pipe + device fds
        std::vector<struct pollfd> pfds;
        pfds.push_back({stop_pipe_[0], POLLIN, 0});

        if (device_fds.empty()) {
            int ret = poll(pfds.data(), pfds.size(), 5000);
            if (ret > 0 && (pfds[0].revents & POLLIN)) break;
            open_devices();
            continue;
        }

        for (int fd : device_fds) {
            pfds.push_back({fd, POLLIN, 0});
        }

        int ret = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 5000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            spdlog::error("UsbScannerMonitor: poll error: {}", strerror(errno));
            break;
        }
        if (ret == 0) continue;
        if (pfds[0].revents & POLLIN) break;

        // Clear stale partial scans (e.g. interrupted mid-barcode)
        if (!accumulator.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_key_time);
            if (elapsed.count() >= 3) {
                spdlog::debug("UsbScannerMonitor: clearing stale accumulator ({}s idle)",
                              elapsed.count());
                accumulator.clear();
            }
        }

        bool device_error = false;
        for (size_t i = 1; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;

            // Drain all available events — scanners send rapid bursts
            while (true) {
                struct input_event ev {};
                ssize_t n = read(pfds[i].fd, &ev, sizeof(ev));
                if (n < static_cast<ssize_t>(sizeof(ev))) {
                    if (n < 0 && errno != EAGAIN) {
                        spdlog::warn("UsbScannerMonitor: device read error, will re-scan");
                        device_error = true;
                    }
                    break;
                }

                if (ev.type != EV_KEY) continue;

                // Track Caps Lock — scanners toggle it for uppercase letters.
                if (ev.code == KEY_CAPSLOCK) {
                    if (ev.value == 1) capslock_on = !capslock_on;
                    continue;
                }

                if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                    shift_held = (ev.value != 0);
                    continue;
                }

                if (ev.value != 1) continue;

                last_key_time = std::chrono::steady_clock::now();

                if (ev.code == KEY_ENTER || ev.code == KEY_KPENTER) {
                    if (!accumulator.empty()) {
                        spdlog::debug("UsbScannerMonitor: scanned text: '{}'", accumulator);
                        int spool_id = check_spoolman_pattern(accumulator);
                        if (spool_id >= 0 && callback_) {
                            spdlog::info("UsbScannerMonitor: detected Spoolman spool ID: {}",
                                         spool_id);
                            callback_(spool_id);
                        } else if (spool_id < 0) {
                            spdlog::debug("UsbScannerMonitor: text '{}' not a Spoolman QR code",
                                          accumulator);
                        }
                        accumulator.clear();
                    }
                    continue;
                }

                // Caps Lock affects only letter keys; Shift affects everything.
                int code = static_cast<int>(ev.code);
                bool is_letter = (code >= 16 && code <= 25) ||  // Q-P
                                 (code >= 30 && code <= 38) ||  // A-L
                                 (code >= 44 && code <= 50);    // Z-M
                bool effective_shift = is_letter ? (shift_held ^ capslock_on) : shift_held;
                char ch = keycode_to_char(code, effective_shift);
                if (ch != 0) {
                    accumulator += ch;
                }
            }
        }

        if (device_error) {
            open_devices();
        }
    }

    for (int fd : device_fds) close(fd);

    spdlog::debug("UsbScannerMonitor: monitor thread exiting");
}

#else  // !__linux__

std::vector<std::string> UsbScannerMonitor::find_scanner_devices() { return {}; }
void UsbScannerMonitor::start(ScanCallback) {
    spdlog::warn("UsbScannerMonitor: only supported on Linux");
}
void UsbScannerMonitor::stop() {}
void UsbScannerMonitor::monitor_thread_func() {}

#endif  // __linux__

} // namespace helix
