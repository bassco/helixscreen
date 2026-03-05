// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_snake_game.cpp
 * @brief Snake easter egg - filament tube edition
 *
 * Grid-based Snake game rendered using custom draw callbacks.
 * Snake body drawn as 3D filament tubes (shadow/body/highlight layers).
 * Food drawn as spool boxes using ui_draw_spool_box().
 * Input via swipe gestures + arrow keys.
 */

#include "ui_snake_game.h"

#include "ui_effects.h"
#include "ui_spool_drawing.h"
#include "ui_utils.h"

#include "config.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <deque>

namespace helix {

// ============================================================================
// CONSTANTS
// ============================================================================

static constexpr int32_t CELL_SIZE = 20;
static constexpr uint32_t INITIAL_TICK_MS = 150;
static constexpr uint32_t MIN_TICK_MS = 70;
static constexpr int SPEED_UP_INTERVAL = 5; // Speed up every N food items
static constexpr uint8_t BACKDROP_OPACITY = 220;

// Config key for persisted high score (non-obvious name)
static constexpr const char* HIGH_SCORE_KEY = "/display/frame_counter";

// Filament colors for snake body (random at game start)
static constexpr uint32_t FILAMENT_COLORS[] = {
    0xED1C24, // Red
    0x00A651, // Green
    0x2E3192, // Blue
    0xFFF200, // Yellow
    0xF7941D, // Orange
    0x92278F, // Purple
    0x00AEEF, // Cyan
    0xEC008C, // Magenta
    0x8DC63F, // Lime
    0xF15A24, // Vermillion
};
static constexpr int NUM_FILAMENT_COLORS =
    static_cast<int>(sizeof(FILAMENT_COLORS) / sizeof(FILAMENT_COLORS[0]));

// Food spool colors (random per food item)
static constexpr uint32_t FOOD_COLORS[] = {
    0xFF6B35, // Tangerine
    0x00D2FF, // Sky blue
    0xFFD700, // Gold
    0xFF1493, // Deep pink
    0x7FFF00, // Chartreuse
    0xDA70D6, // Orchid
};
static constexpr int NUM_FOOD_COLORS =
    static_cast<int>(sizeof(FOOD_COLORS) / sizeof(FOOD_COLORS[0]));

// ============================================================================
// TYPES
// ============================================================================

enum class Direction { UP, DOWN, LEFT, RIGHT };

struct GridPos {
    int x;
    int y;
    bool operator==(const GridPos& o) const {
        return x == o.x && y == o.y;
    }
};

// ============================================================================
// GAME STATE (namespace-level, static)
// ============================================================================

namespace {

lv_obj_t* g_overlay = nullptr;        // Full-screen backdrop
lv_obj_t* g_game_area = nullptr;      // Game rendering area
lv_obj_t* g_score_label = nullptr;    // Score display
lv_obj_t* g_gameover_label = nullptr; // Game over text
lv_obj_t* g_close_btn = nullptr;      // X close button
lv_timer_t* g_game_timer = nullptr;   // Game tick timer

// Grid dimensions (calculated from screen size)
int g_grid_cols = 0;
int g_grid_rows = 0;
int g_grid_offset_x = 0; // Pixel offset to center grid in game area
int g_grid_offset_y = 0;

// Snake state
std::deque<GridPos> g_snake;
Direction g_direction = Direction::RIGHT;
Direction g_next_direction = Direction::RIGHT; // Buffered input
bool g_game_over = false;
bool g_game_started = false;

// Food state
GridPos g_food = {0, 0};
lv_color_t g_food_color = {};

// Score and speed
int g_score = 0;
int g_high_score = 0;
uint32_t g_current_tick_ms = INITIAL_TICK_MS;

// Visual
lv_color_t g_snake_color = {};

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void init_game();
void game_tick(lv_timer_t* timer);
void draw_cb(lv_event_t* e);
void gesture_cb(lv_event_t* e);
void input_cb(lv_event_t* e);
void close_cb(lv_event_t* e);
void place_food();
void update_score_label();
void show_game_over();
void create_overlay();
void destroy_overlay();

// ============================================================================
// TUBE DRAWING (local reimplementation of filament path pattern)
// ============================================================================

/// Draw a flat line segment (base primitive for tube layers)
void draw_flat_line(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                    lv_color_t color, int32_t width) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = width;
    line_dsc.p1.x = x1;
    line_dsc.p1.y = y1;
    line_dsc.p2.x = x2;
    line_dsc.p2.y = y2;
    line_dsc.round_start = true;
    line_dsc.round_end = true;
    lv_draw_line(layer, &line_dsc);
}

/// Draw a 3D tube segment between two points (shadow/body/highlight layers)
void draw_tube_segment(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       lv_color_t color, int32_t width) {
    // Shadow: wider, darker
    int32_t shadow_extra = LV_MAX(2, width / 2);
    lv_color_t shadow_color = ui_color_darken(color, 35);
    draw_flat_line(layer, x1, y1, x2, y2, shadow_color, width + shadow_extra);

    // Body: main tube surface
    draw_flat_line(layer, x1, y1, x2, y2, color, width);

    // Highlight: narrower, lighter, offset toward top-left
    int32_t hl_width = LV_MAX(1, width * 2 / 5);
    lv_color_t hl_color = ui_color_lighten(color, 44);

    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    int32_t offset_x = 0;
    int32_t offset_y = 0;
    int32_t off_amount = width / 4 + 1;

    if (dx == 0) {
        offset_x = off_amount; // Vertical: highlight right
    } else if (dy == 0) {
        offset_y = -off_amount; // Horizontal: highlight up
    }

    draw_flat_line(layer, x1 + offset_x, y1 + offset_y, x2 + offset_x, y2 + offset_y, hl_color,
                   hl_width);
}

// ============================================================================
// GRID HELPERS
// ============================================================================

/// Convert grid position to pixel center coordinates
void grid_to_pixel(const GridPos& pos, int32_t& px, int32_t& py) {
    px = g_grid_offset_x + pos.x * CELL_SIZE + CELL_SIZE / 2;
    py = g_grid_offset_y + pos.y * CELL_SIZE + CELL_SIZE / 2;
}

/// Pick a random filament color
lv_color_t random_filament_color() {
    return lv_color_hex(FILAMENT_COLORS[rand() % NUM_FILAMENT_COLORS]);
}

/// Pick a random food color
lv_color_t random_food_color() {
    return lv_color_hex(FOOD_COLORS[rand() % NUM_FOOD_COLORS]);
}

// ============================================================================
// GAME LOGIC
// ============================================================================

void load_high_score() {
    auto* cfg = Config::get_instance();
    if (cfg) {
        g_high_score = cfg->get<int>(HIGH_SCORE_KEY, 0);
    }
    spdlog::debug("[SnakeGame] Loaded high score: {}", g_high_score);
}

void save_high_score() {
    auto* cfg = Config::get_instance();
    if (cfg) {
        cfg->set(HIGH_SCORE_KEY, g_high_score);
        cfg->save();
    }
    spdlog::info("[SnakeGame] Saved new high score: {}", g_high_score);
}

void init_game() {
    g_snake.clear();
    g_direction = Direction::RIGHT;
    g_next_direction = Direction::RIGHT;
    g_game_over = false;
    g_game_started = true;
    g_score = 0;
    g_current_tick_ms = INITIAL_TICK_MS;

    // Random snake color
    g_snake_color = random_filament_color();

    // Start snake in center, 3 segments long
    int start_x = g_grid_cols / 2;
    int start_y = g_grid_rows / 2;
    for (int i = 2; i >= 0; i--) {
        g_snake.push_back({start_x - i, start_y});
    }

    place_food();
    update_score_label();

    // Hide game over label
    if (g_gameover_label) {
        lv_obj_add_flag(g_gameover_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Reset timer speed
    if (g_game_timer) {
        lv_timer_set_period(g_game_timer, g_current_tick_ms);
    }
}

void place_food() {
    // Check if snake fills the entire grid (you win!)
    if (static_cast<int>(g_snake.size()) >= g_grid_cols * g_grid_rows) {
        g_game_over = true;
        spdlog::info("[SnakeGame] Snake filled the grid — you win!");

        // Update high score for the win
        if (g_score > g_high_score) {
            g_high_score = g_score;
            save_high_score();
        }

        if (g_gameover_label) {
            char buf[96];
            snprintf(buf, sizeof(buf), "YOU WIN!\nScore: %d\nTap to play again", g_score);
            lv_label_set_text(g_gameover_label, buf);
            lv_obj_remove_flag(g_gameover_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (g_game_timer) {
            lv_timer_pause(g_game_timer);
        }
        update_score_label();
        return;
    }

    // Find a position not occupied by the snake
    int attempts = 0;
    do {
        g_food.x = rand() % g_grid_cols;
        g_food.y = rand() % g_grid_rows;
        attempts++;
    } while (std::find(g_snake.begin(), g_snake.end(), g_food) != g_snake.end() && attempts < 1000);

    g_food_color = random_food_color();
}

void game_tick(lv_timer_t* /*timer*/) {
    if (g_game_over || !g_game_started) {
        return;
    }

    // Apply buffered direction
    g_direction = g_next_direction;

    // Calculate new head position
    GridPos head = g_snake.back();
    GridPos new_head = head;

    switch (g_direction) {
    case Direction::UP:
        new_head.y--;
        break;
    case Direction::DOWN:
        new_head.y++;
        break;
    case Direction::LEFT:
        new_head.x--;
        break;
    case Direction::RIGHT:
        new_head.x++;
        break;
    }

    // Wall collision
    if (new_head.x < 0 || new_head.x >= g_grid_cols || new_head.y < 0 ||
        new_head.y >= g_grid_rows) {
        g_game_over = true;
        show_game_over();
        return;
    }

    // Self collision
    if (std::find(g_snake.begin(), g_snake.end(), new_head) != g_snake.end()) {
        g_game_over = true;
        show_game_over();
        return;
    }

    // Move snake
    g_snake.push_back(new_head);

    // Check food collision
    if (new_head == g_food) {
        g_score++;
        update_score_label();
        place_food();

        // Speed up periodically
        if (g_score % SPEED_UP_INTERVAL == 0 && g_current_tick_ms > MIN_TICK_MS) {
            g_current_tick_ms -= 10;
            if (g_game_timer) {
                lv_timer_set_period(g_game_timer, g_current_tick_ms);
            }
        }
    } else {
        // Remove tail (no growth)
        g_snake.pop_front();
    }

    // Trigger redraw
    if (g_game_area) {
        lv_obj_invalidate(g_game_area);
    }
}

void update_score_label() {
    if (g_score_label) {
        char buf[48];
        if (g_high_score > 0) {
            snprintf(buf, sizeof(buf), "Score: %d  |  Best: %d", g_score, g_high_score);
        } else {
            snprintf(buf, sizeof(buf), "Score: %d", g_score);
        }
        lv_label_set_text(g_score_label, buf);
    }
}

void show_game_over() {
    bool new_high = g_score > g_high_score && g_score > 0;
    if (new_high) {
        g_high_score = g_score;
        save_high_score();
    }

    spdlog::info("[SnakeGame] Game over! Score: {} | Best: {}{}", g_score, g_high_score,
                 new_high ? " (NEW!)" : "");

    if (g_gameover_label) {
        char buf[96];
        if (new_high) {
            snprintf(buf, sizeof(buf), "NEW HIGH SCORE!\n%d\nTap to play again", g_score);
        } else {
            snprintf(buf, sizeof(buf), "Game Over!\nScore: %d\nTap to restart", g_score);
        }
        lv_label_set_text(g_gameover_label, buf);
        lv_obj_remove_flag(g_gameover_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Update score label to reflect new high score
    update_score_label();

    // Stop the timer
    if (g_game_timer) {
        lv_timer_pause(g_game_timer);
    }

    // Invalidate for final red-flash render
    if (g_game_area) {
        lv_obj_invalidate(g_game_area);
    }
}

// ============================================================================
// DRAWING
// ============================================================================

void draw_cb(lv_event_t* e) {
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_obj_t* obj = lv_event_get_current_target_obj(e);

    // Get object coordinates for clipping context
    lv_area_t obj_area;
    lv_obj_get_coords(obj, &obj_area);

    // Draw border around game area
    {
        lv_draw_rect_dsc_t border_dsc;
        lv_draw_rect_dsc_init(&border_dsc);
        border_dsc.bg_opa = LV_OPA_TRANSP;
        border_dsc.border_color = lv_color_hex(0x444444);
        border_dsc.border_opa = LV_OPA_COVER;
        border_dsc.border_width = 2;
        border_dsc.radius = 4;

        lv_area_t border_area = {
            obj_area.x1 + g_grid_offset_x - 2,
            obj_area.y1 + g_grid_offset_y - 2,
            obj_area.x1 + g_grid_offset_x + g_grid_cols * CELL_SIZE + 1,
            obj_area.y1 + g_grid_offset_y + g_grid_rows * CELL_SIZE + 1,
        };
        lv_draw_rect(layer, &border_dsc, &border_area);
    }

    if (!g_game_started) {
        return;
    }

    // Draw food as spool box
    {
        int32_t fx, fy;
        grid_to_pixel(g_food, fx, fy);
        fx += obj_area.x1;
        fy += obj_area.y1;
        ui_draw_spool_box(layer, fx, fy, g_food_color, true, CELL_SIZE / 4);
    }

    // Draw snake body as tube segments
    lv_color_t body_color = g_game_over ? lv_color_hex(0xCC2222) : g_snake_color;
    int32_t tube_width = CELL_SIZE * 2 / 3;

    for (size_t i = 1; i < g_snake.size(); i++) {
        int32_t x1, y1, x2, y2;
        grid_to_pixel(g_snake[i - 1], x1, y1);
        grid_to_pixel(g_snake[i], x2, y2);

        // Offset to absolute coordinates
        x1 += obj_area.x1;
        y1 += obj_area.y1;
        x2 += obj_area.x1;
        y2 += obj_area.y1;

        // Head segment is slightly wider and brighter
        bool is_head = (i == g_snake.size() - 1);
        int32_t w = is_head ? tube_width + 2 : tube_width;
        lv_color_t c = is_head ? ui_color_lighten(body_color, 20) : body_color;

        draw_tube_segment(layer, x1, y1, x2, y2, c, w);
    }

    // Draw eyes on snake head
    if (g_snake.size() >= 2) {
        const GridPos& head = g_snake.back();
        int32_t hx, hy;
        grid_to_pixel(head, hx, hy);
        hx += obj_area.x1;
        hy += obj_area.y1;

        // Eye positions depend on direction
        int32_t eye_offset = CELL_SIZE / 4;
        int32_t ex1 = hx, ey1 = hy, ex2 = hx, ey2 = hy;

        switch (g_direction) {
        case Direction::UP:
        case Direction::DOWN:
            ex1 = hx - eye_offset;
            ex2 = hx + eye_offset;
            ey1 = ey2 = hy + (g_direction == Direction::UP ? -eye_offset / 2 : eye_offset / 2);
            break;
        case Direction::LEFT:
        case Direction::RIGHT:
            ey1 = hy - eye_offset;
            ey2 = hy + eye_offset;
            ex1 = ex2 = hx + (g_direction == Direction::LEFT ? -eye_offset / 2 : eye_offset / 2);
            break;
        }

        // Draw eyes as small white circles with dark pupils
        lv_draw_arc_dsc_t eye_dsc;
        lv_draw_arc_dsc_init(&eye_dsc);
        eye_dsc.width = 3;
        eye_dsc.start_angle = 0;
        eye_dsc.end_angle = 360;
        eye_dsc.color = lv_color_white();
        eye_dsc.radius = 3;

        eye_dsc.center.x = ex1;
        eye_dsc.center.y = ey1;
        lv_draw_arc(layer, &eye_dsc);

        eye_dsc.center.x = ex2;
        eye_dsc.center.y = ey2;
        lv_draw_arc(layer, &eye_dsc);

        // Pupils
        eye_dsc.color = lv_color_black();
        eye_dsc.radius = 2;
        eye_dsc.width = 2;

        eye_dsc.center.x = ex1;
        eye_dsc.center.y = ey1;
        lv_draw_arc(layer, &eye_dsc);

        eye_dsc.center.x = ex2;
        eye_dsc.center.y = ey2;
        lv_draw_arc(layer, &eye_dsc);
    }
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

/// Set direction with 180-degree reversal prevention
void set_direction(Direction dir) {
    // Prevent reversing into yourself
    if ((dir == Direction::UP && g_direction == Direction::DOWN) ||
        (dir == Direction::DOWN && g_direction == Direction::UP) ||
        (dir == Direction::LEFT && g_direction == Direction::RIGHT) ||
        (dir == Direction::RIGHT && g_direction == Direction::LEFT)) {
        return;
    }
    g_next_direction = dir;
}

// Touch state for swipe detection
lv_point_t g_touch_start = {0, 0};
bool g_swipe_handled = false;

void touch_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_active();
    if (!indev) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &g_touch_start);
        g_swipe_handled = false;
    } else if (code == LV_EVENT_PRESSING) {
        // React as soon as finger moves enough — don't wait for release
        if (g_swipe_handled || g_game_over) {
            return;
        }

        lv_point_t current;
        lv_indev_get_point(indev, &current);

        int32_t dx = current.x - g_touch_start.x;
        int32_t dy = current.y - g_touch_start.y;
        int32_t abs_dx = LV_ABS(dx);
        int32_t abs_dy = LV_ABS(dy);

        constexpr int32_t SWIPE_THRESHOLD = 20;
        if (abs_dx < SWIPE_THRESHOLD && abs_dy < SWIPE_THRESHOLD) {
            return;
        }

        if (abs_dx > abs_dy) {
            set_direction(dx > 0 ? Direction::RIGHT : Direction::LEFT);
        } else {
            set_direction(dy > 0 ? Direction::DOWN : Direction::UP);
        }
        g_swipe_handled = true;
    } else if (code == LV_EVENT_RELEASED) {
        if (!g_swipe_handled) {
            // PRESSING might not have fired — check swipe on release as fallback
            lv_point_t end;
            lv_indev_get_point(indev, &end);

            int32_t dx = end.x - g_touch_start.x;
            int32_t dy = end.y - g_touch_start.y;
            int32_t abs_dx = LV_ABS(dx);
            int32_t abs_dy = LV_ABS(dy);

            constexpr int32_t SWIPE_THRESHOLD = 20;
            if (abs_dx >= SWIPE_THRESHOLD || abs_dy >= SWIPE_THRESHOLD) {
                if (!g_game_over) {
                    if (abs_dx > abs_dy) {
                        set_direction(dx > 0 ? Direction::RIGHT : Direction::LEFT);
                    } else {
                        set_direction(dy > 0 ? Direction::DOWN : Direction::UP);
                    }
                }
            } else if (g_game_over) {
                // Tap — restart
                init_game();
                if (g_game_timer) {
                    lv_timer_resume(g_game_timer);
                }
                if (g_game_area) {
                    lv_obj_invalidate(g_game_area);
                }
            }
        }
        g_swipe_handled = false;
    }
}

void input_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_KEY) {
        // Arrow key support for dev/testing
        uint32_t key = lv_event_get_key(e);

        if (key == LV_KEY_ESC) {
            SnakeGame::hide();
            return;
        }

        if (g_game_over) {
            // Any key restarts
            init_game();
            if (g_game_timer) {
                lv_timer_resume(g_game_timer);
            }
            if (g_game_area) {
                lv_obj_invalidate(g_game_area);
            }
            return;
        }

        switch (key) {
        case LV_KEY_UP:
            set_direction(Direction::UP);
            break;
        case LV_KEY_DOWN:
            set_direction(Direction::DOWN);
            break;
        case LV_KEY_LEFT:
            set_direction(Direction::LEFT);
            break;
        case LV_KEY_RIGHT:
            set_direction(Direction::RIGHT);
            break;
        default:
            break;
        }
    }
}

void close_cb(lv_event_t* /*e*/) {
    SnakeGame::hide();
}

// ============================================================================
// OVERLAY LIFECYCLE
// ============================================================================

void create_overlay() {
    if (g_overlay) {
        spdlog::warn("[SnakeGame] Overlay already exists");
        return;
    }

    spdlog::info("[SnakeGame] Launching snake game!");

    // Load persisted high score
    load_high_score();

    // Seed RNG
    srand(static_cast<unsigned>(time(nullptr)));

    // Create full-screen backdrop on top layer
    lv_obj_t* parent = lv_layer_top();
    g_overlay = helix::ui::create_fullscreen_backdrop(parent, BACKDROP_OPACITY);
    if (!g_overlay) {
        spdlog::error("[SnakeGame] Failed to create backdrop");
        return;
    }

    // Content container on top of backdrop for flex layout.
    // The backdrop may be an lv_image (blurred) with a tint child — adding
    // flex children directly to it breaks layout and input hit testing.
    lv_obj_t* content = lv_obj_create(g_overlay);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 4, LV_PART_MAIN);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // === Header row (score + close button) ===
    lv_obj_t* header = lv_obj_create(content);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Score label
    g_score_label = lv_label_create(header);
    lv_obj_set_style_text_color(g_score_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_score_label, theme_manager_get_font("font_heading"), LV_PART_MAIN);
    lv_label_set_text(g_score_label, "Score: 0");

    // Close button (X)
    g_close_btn = lv_button_create(header);
    lv_obj_set_size(g_close_btn, 36, 36);
    lv_obj_set_style_bg_color(g_close_btn, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_close_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g_close_btn, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(g_close_btn, close_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close_label = lv_label_create(g_close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_set_style_text_color(close_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(close_label, theme_manager_get_font("font_heading"), LV_PART_MAIN);
    lv_obj_center(close_label);

    // === Game area ===
    g_game_area = lv_obj_create(content);
    lv_obj_set_style_bg_opa(g_game_area, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_game_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_game_area, 0, LV_PART_MAIN);
    lv_obj_set_flex_grow(g_game_area, 1);
    lv_obj_set_width(g_game_area, LV_PCT(100));
    lv_obj_remove_flag(g_game_area, LV_OBJ_FLAG_SCROLLABLE);

    // Calculate grid dimensions from available space
    lv_coord_t screen_w = lv_display_get_horizontal_resolution(nullptr);
    lv_coord_t screen_h = lv_display_get_vertical_resolution(nullptr);

    // Reserve space for header (~48px) and padding
    lv_coord_t avail_w = screen_w - 24; // 12px padding each side
    lv_coord_t avail_h = screen_h - 64; // Header + padding

    g_grid_cols = avail_w / CELL_SIZE;
    g_grid_rows = avail_h / CELL_SIZE;

    // Center the grid within available space
    g_grid_offset_x = (avail_w - g_grid_cols * CELL_SIZE) / 2;
    g_grid_offset_y = (avail_h - g_grid_rows * CELL_SIZE) / 2;

    spdlog::debug("[SnakeGame] Grid: {}x{} cells, offset: ({}, {})", g_grid_cols, g_grid_rows,
                  g_grid_offset_x, g_grid_offset_y);

    // Register custom draw callback
    lv_obj_add_event_cb(g_game_area, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);

    // Register touch input on the game area. PRESSING fires on each indev
    // read while touched — detect swipe direction as early as possible.
    // RELEASED is the guaranteed fallback for devices where PRESSING is flaky.
    // Remove SCROLL_CHAIN so LVGL's scroll handler doesn't walk up to
    // lv_layer_top() and absorb touch movement into scroll processing.
    lv_obj_add_flag(g_game_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_game_area, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(g_game_area, touch_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(g_game_area, touch_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(g_game_area, touch_cb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(g_game_area, input_cb, LV_EVENT_KEY, nullptr);

    // Add to default group for keyboard input
    lv_group_t* group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, g_game_area);
        lv_group_focus_obj(g_game_area);
    }

    // === Game over overlay label ===
    g_gameover_label = lv_label_create(content);
    lv_obj_set_style_text_color(g_gameover_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_gameover_label, theme_manager_get_font("font_heading"),
                               LV_PART_MAIN);
    lv_obj_set_style_text_align(g_gameover_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(g_gameover_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_gameover_label, LV_OBJ_FLAG_HIDDEN);
    // Float on top of game area
    lv_obj_add_flag(g_gameover_label, LV_OBJ_FLAG_FLOATING);

    // Bring overlay to front
    lv_obj_move_foreground(g_overlay);

    // Initialize game state
    init_game();

    // Start game timer
    g_game_timer = lv_timer_create(game_tick, g_current_tick_ms, nullptr);

    spdlog::info("[SnakeGame] Game started! Grid: {}x{}", g_grid_cols, g_grid_rows);
}

void destroy_overlay() {
    // Stop timers
    if (g_game_timer) {
        lv_timer_delete(g_game_timer);
        g_game_timer = nullptr;
    }

    // Remove from focus group before deletion
    if (g_game_area) {
        lv_group_t* group = lv_group_get_default();
        if (group) {
            lv_group_remove_obj(g_game_area);
        }
    }

    // Clean up overlay
    if (helix::ui::safe_delete(g_overlay)) {
        g_game_area = nullptr;
        g_score_label = nullptr;
        g_gameover_label = nullptr;
        g_close_btn = nullptr;
    }

    // Reset state
    g_snake.clear();
    g_game_started = false;
    g_game_over = false;
    g_swipe_handled = false;

    spdlog::info("[SnakeGame] Game closed");
}

} // anonymous namespace

// ============================================================================
// PUBLIC API
// ============================================================================

void SnakeGame::show() {
    if (g_overlay) {
        spdlog::debug("[SnakeGame] Already visible");
        return;
    }
    create_overlay();
}

void SnakeGame::hide() {
    destroy_overlay();
}

bool SnakeGame::is_visible() {
    return g_overlay != nullptr;
}

} // namespace helix
