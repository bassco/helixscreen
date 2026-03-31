// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "input_device_scanner.h"
#include "ui_modal.h"

#include <functional>
#include <string>
#include <vector>

namespace helix::ui {

/// Modal that lists connected USB HID devices for manual scanner selection.
/// Shows auto-detect option at top, then each discovered device.
class ScannerPickerModal : public Modal {
  public:
    using SelectionCallback = std::function<void(const std::string& vendor_product,
                                                 const std::string& device_name)>;

    explicit ScannerPickerModal(SelectionCallback on_select);
    ~ScannerPickerModal() override = default;

    const char* get_name() const override {
        return "Scanner Picker";
    }
    const char* component_name() const override {
        return "scanner_picker_modal";
    }

  protected:
    void on_show() override;

  private:
    void populate_device_list();
    void add_device_row(lv_obj_t* list, const std::string& label, const std::string& sublabel,
                        const std::string& vendor_product);
    void handle_device_selected(const std::string& vendor_product, const std::string& device_name);

    SelectionCallback on_select_;
    std::string current_device_id_;
    lv_obj_t* device_list_ = nullptr;
    lv_obj_t* empty_state_ = nullptr;

    /// Active instance pointer (only one picker modal open at a time)
    static ScannerPickerModal* s_active_instance_;
};

} // namespace helix::ui
