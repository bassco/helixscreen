// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_file_sorter.h"

#include <algorithm>

namespace helix::ui {

void PrintSelectFileSorter::sort_by(SortColumn column) {
    if (column == current_column_) {
        current_direction_ = (current_direction_ == SortDirection::ASCENDING)
                                 ? SortDirection::DESCENDING
                                 : SortDirection::ASCENDING;
    } else {
        current_column_ = column;
        current_direction_ = SortDirection::ASCENDING;
    }
}

void PrintSelectFileSorter::set_sort(SortColumn column, SortDirection direction) {
    current_column_ = column;
    current_direction_ = direction;
}

void PrintSelectFileSorter::apply_sort(std::vector<PrintFileData>& files) {
    auto sort_column = current_column_;
    auto sort_direction = current_direction_;

    std::sort(files.begin(), files.end(),
              [sort_column, sort_direction](const PrintFileData& a, const PrintFileData& b) {
                  // Directories always sort to top
                  if (a.is_dir != b.is_dir) {
                      return a.is_dir;
                  }

                  bool result = false;

                  // Filename tiebreaker ensures strict weak ordering when
                  // primary values are equal (e.g. all directories have
                  // modified_timestamp=0). Without this, descending sort
                  // with equal values violates comp(a,b) && comp(b,a) = UB.
                  switch (sort_column) {
                  case SortColumn::FILENAME:
                      result = a.filename < b.filename;
                      break;
                  case SortColumn::SIZE:
                      result = (a.file_size_bytes != b.file_size_bytes)
                                   ? (a.file_size_bytes < b.file_size_bytes)
                                   : (a.filename < b.filename);
                      break;
                  case SortColumn::MODIFIED:
                      result = (a.modified_timestamp != b.modified_timestamp)
                                   ? (a.modified_timestamp < b.modified_timestamp)
                                   : (a.filename < b.filename);
                      break;
                  case SortColumn::PRINT_TIME:
                      result = (a.print_time_minutes != b.print_time_minutes)
                                   ? (a.print_time_minutes < b.print_time_minutes)
                                   : (a.filename < b.filename);
                      break;
                  case SortColumn::FILAMENT:
                      result = (a.filament_grams != b.filament_grams)
                                   ? (a.filament_grams < b.filament_grams)
                                   : (a.filename < b.filename);
                      break;
                  }

                  if (sort_direction == SortDirection::DESCENDING) {
                      result = !result;
                  }

                  return result;
              });

    // Pin ".." parent directory to position 0 (after sort, bulletproof)
    for (size_t i = 1; i < files.size(); i++) {
        if (files[i].is_dir && files[i].filename == "..") {
            std::rotate(files.begin(), files.begin() + i, files.begin() + i + 1);
            break;
        }
    }
}

} // namespace helix::ui
