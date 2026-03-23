// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"

#include <vector>

/**
 * @file ui_carousel.h
 * @brief General-purpose carousel widget using horizontal scroll-snap
 *
 * Provides a <ui_carousel> XML widget with:
 * - Horizontal scrollable container with snap-to-page behavior
 * - Page indicator dots
 * - Optional auto-scroll timer
 * - Optional wrap-around
 * - Subject binding for current page
 *
 * Usage in XML:
 *   <ui_carousel wrap="true" auto_scroll_ms="5000" show_indicators="true">
 *     <lv_obj>Page 1 content</lv_obj>
 *     <lv_obj>Page 2 content</lv_obj>
 *   </ui_carousel>
 */

struct CarouselState {
    static constexpr uint32_t MAGIC = 0x43415231; // "CAR1"
    uint32_t magic{MAGIC};
    lv_obj_t* scroll_container = nullptr;
    lv_obj_t* indicator_row = nullptr;
    std::vector<lv_obj_t*> real_tiles;
    lv_subject_t* page_subject = nullptr;
    lv_timer_t* auto_timer = nullptr;
    int current_page = 0;
    int auto_scroll_ms = 0;
    int real_page_count = -1; ///< When >= 0, limits indicator dots and page clamping
    bool wrap = true;
    bool show_indicators = true;
    bool user_touching = false;
};

/**
 * @brief Initialize the ui_carousel custom widget
 *
 * Registers the <ui_carousel> XML widget with LVGL's XML parser.
 * Must be called after lv_xml_init() and before any XML using this widget.
 */
void ui_carousel_init();

/**
 * @brief Get the CarouselState for a carousel object
 * @param obj The carousel container object
 * @return Pointer to CarouselState, or nullptr if obj is not a carousel
 */
CarouselState* ui_carousel_get_state(lv_obj_t* obj);

/**
 * @brief Navigate to a specific page
 * @param carousel The carousel container object
 * @param page Zero-based page index
 * @param animate Whether to animate the transition
 */
void ui_carousel_goto_page(lv_obj_t* carousel, int page, bool animate = true);

/**
 * @brief Get the currently visible page index
 * @param carousel The carousel container object
 * @return Zero-based page index, or 0 if not a valid carousel
 */
int ui_carousel_get_current_page(lv_obj_t* carousel);

/**
 * @brief Get the total number of pages (excluding clones)
 * @param carousel The carousel container object
 * @return Number of real pages, or 0 if not a valid carousel
 */
int ui_carousel_get_page_count(lv_obj_t* carousel);

/**
 * @brief Add a child item as a new page in the carousel
 * @param carousel The carousel container object
 * @param item The widget to add as a page (will be reparented into a tile)
 */
void ui_carousel_add_item(lv_obj_t* carousel, lv_obj_t* item);

/**
 * @brief Rebuild the indicator dots to match current page count
 * @param carousel The carousel container object
 */
void ui_carousel_rebuild_indicators(lv_obj_t* carousel);

/**
 * @brief Start auto-advancing the carousel on a timer
 *
 * Uses auto_scroll_ms from CarouselState for the interval.
 * Stops any existing timer first. No-op if auto_scroll_ms <= 0.
 *
 * @param carousel The carousel container object
 */
void ui_carousel_start_auto_advance(lv_obj_t* carousel);

/**
 * @brief Stop the auto-advance timer
 * @param carousel The carousel container object
 */
void ui_carousel_stop_auto_advance(lv_obj_t* carousel);

/**
 * @brief Create a carousel programmatically (not via XML)
 *
 * Creates the same structure as the XML factory: outer container with
 * scroll container and indicator row. Use this from C++ code when
 * creating carousels without XML.
 *
 * @param parent Parent LVGL object
 * @return The carousel container object
 */
lv_obj_t* ui_carousel_create_obj(lv_obj_t* parent);

/**
 * @brief Set the real page count for indicator display
 *
 * When set to >= 0, indicator dots and page clamping use this count
 * instead of real_tiles.size(). Useful when the carousel manages
 * virtual pages that don't map 1:1 to tiles.
 *
 * @param carousel The carousel container object
 * @param count Number of real pages, or -1 to use tile count
 */
void ui_carousel_set_real_page_count(lv_obj_t* carousel, int count);

/**
 * @brief Remove an item (tile) from the carousel by index
 *
 * Deletes the tile's LVGL object and removes it from real_tiles.
 * Adjusts current_page if it was pointing at or past the removed item.
 * Rebuilds indicators after removal.
 *
 * @param carousel The carousel container object
 * @param index Zero-based index of the tile to remove
 */
void ui_carousel_remove_item(lv_obj_t* carousel, int index);

/**
 * @brief Enable or disable scroll input on the carousel
 *
 * When disabled, the scroll container loses LV_OBJ_FLAG_SCROLLABLE
 * and scroll direction is set to LV_DIR_NONE. When enabled, scrollable
 * flag and horizontal direction are restored.
 *
 * @param carousel The carousel container object
 * @param enabled Whether scrolling should be enabled
 */
void ui_carousel_set_scroll_enabled(lv_obj_t* carousel, bool enabled);
