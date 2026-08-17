#include "screensaver.h"

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../../config/constants.h"

// Bold grotesque for route bullets (Arimo Bold, Helvetica-metric); the glyph
// cap height is ~55% of the badge diameter, matching official MTA bullets
LV_FONT_DECLARE(lv_font_bullet_40)

namespace {

constexpr uint32_t kWaveTickMs = 40;                    // 25 fps ripple animation
constexpr uint32_t kTrainsTickMs = 1000;                // Arrivals list refresh check
constexpr float kWavePeriodS = 2.4f;                    // Time for one outward wave cycle
constexpr float kWaveLengthPx = 96.0f;                  // Radial distance between crests
constexpr float kSecondWaveFrequency = 1.7f;            // Detail wave relative frequency
constexpr float kTwoPi = 6.28318530f;

constexpr int kVariantCount = 3;

constexpr int kBadgeSizePx = 52;
constexpr int kMaxGroupedRows = 4;
constexpr int kMaxBoardRows = 6;
constexpr int kCatchDotSizePx = 6;

// Countdowns follow the MTA platform clocks: big bare minutes, in rounded
// tiles on the grouped view, with a single tiny "mins" as a column header
// (board) or row label (grouped). The header's line box carries more air
// below its glyphs than the rows need, so it is pulled back up by
// kBoardHeaderTuckPx
constexpr int kCountdownTileRadiusPx = 10;
constexpr int kCountdownTilePadPx = 8;
constexpr int kCatchDotGapPx = 5;
constexpr int kBoardHeaderTuckPx = 8;

// Grouped entries (52px badge row + tiles gap + 40px tile row) are spaced so four
// of them span the page's 424px content height; the tiles take the space the
// gaps give up so the countdowns read from across the room, and the tighter
// gap inside an entry keeps it grouped against the wider gap between watches
constexpr int kGroupedTilesGapPx = 4;
constexpr int kGroupedRowGapPx = 13;
constexpr int kGroupedTileGapPx = 8;
constexpr int kGroupedUnitMarginPx = 4;
constexpr int kBoardRowGapPx = 12;
const lv_font_t* const kCountdownUnitFont = &lv_font_montserrat_14;
const lv_font_t* const kBoardCountdownFont = &lv_font_montserrat_36;
const lv_font_t* const kTileCountdownFont = &lv_font_montserrat_36;

// Page indicator dots stacked along the right edge of a multi-page trains
// view; the rows shift over by the gutter width so nothing sits under the dots
constexpr int kPageDotSizePx = 6;
constexpr int kPageDotGapPx = 8;
constexpr int kPageDotGutterPx = 14;

constexpr int kTrainsPagePadVerPx = 16;
constexpr int kTrainsPagePadLeftPx = 2;

// The bullet font's glyphs are all cap-height and sit on the baseline, leaving
// the font's 8px descent as empty space below them; shift down by half of it
// so the glyph is visually centered in the badge
constexpr int kBadgeGlyphNudgePx = 4;

struct WatchEntry {
    const TrainArrivalItem* item;
    uint8_t mins[NET_MAX_ARRIVAL_MINS];
    uint8_t count;
};

struct BoardEntry {
    const TrainArrivalItem* item;
    uint8_t min;
};

// Warning color for an arrival, judged against the watch's walk-to-platform
// estimate: yellow when only a rushed walk (NET_WALK_RUSH_PERCENT of the normal
// time) still makes it, red when the train can't be caught. Trains reachable at
// a normal pace and watches without an estimate are not flagged.
constexpr uint32_t kNoCatchWarning = 0;

uint32_t arrival_catch_color(uint8_t walk_min, uint8_t mins) {
    if (walk_min == 0 || mins >= walk_min) {
        return kNoCatchWarning;
    }
    uint8_t rushed_walk_min =
        static_cast<uint8_t>((walk_min * NET_WALK_RUSH_PERCENT + 99) / 100);
    return mins >= rushed_walk_min ? THEME_COLOR_SCREENSAVER_CATCH_RUSH
                                   : THEME_COLOR_SCREENSAVER_CATCH_MISS;
}

// Tiny dot flagging a rushed or missed catch
void make_catch_dot(lv_obj_t* parent, uint32_t color) {
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_size(dot, kCatchDotSizePx, kCatchDotSizePx);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t* make_flex_container(lv_obj_t* parent, lv_flex_flow_t flow, int32_t gap) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(obj, gap, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

// Full-tile column that holds one trains page, rows stacked from the top so
// the list starts at the same place however many rows it has; non-clickable
// so taps land on the tile underneath
lv_obj_t* make_trains_page(lv_obj_t* tile, int32_t gap) {
    lv_obj_t* page = lv_obj_create(tile);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_ver(page, kTrainsPagePadVerPx, 0);
    lv_obj_set_style_pad_left(page, kTrainsPagePadLeftPx, 0);
    lv_obj_set_style_pad_right(page, 0, 0);
    lv_obj_set_layout(page, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(page, gap, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE);
    return page;
}

void make_route_badge(lv_obj_t* parent, const TrainArrivalItem& item) {
    lv_obj_t* badge = lv_obj_create(parent);
    lv_obj_set_size(badge, kBadgeSizePx, kBadgeSizePx);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(item.color), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* badge_label = lv_label_create(badge);
    lv_label_set_text(badge_label, item.route);
    lv_obj_set_style_text_font(badge_label, &lv_font_bullet_40, 0);
    lv_obj_set_style_text_color(badge_label, lv_color_hex(item.text_color), 0);
    lv_obj_align(badge_label, LV_ALIGN_CENTER, 0, kBadgeGlyphNudgePx);
}

// Tiny muted "mins" caption
lv_obj_t* make_unit_label(lv_obj_t* parent) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, "mins");
    lv_obj_set_style_text_font(label, kCountdownUnitFont, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    return label;
}

// The minutes and, when the catch is hard, a dot to their left, only as wide
// as their content
lv_obj_t* make_countdown(lv_obj_t* parent, const TrainArrivalItem& item, uint8_t mins,
                         const lv_font_t* font) {
    lv_obj_t* countdown = make_flex_container(parent, LV_FLEX_FLOW_ROW, kCatchDotGapPx);
    lv_obj_set_width(countdown, LV_SIZE_CONTENT);

    uint32_t dot_color = arrival_catch_color(item.walk_min, mins);
    if (dot_color != kNoCatchWarning) {
        make_catch_dot(countdown, dot_color);
    }

    char mins_text[4];
    snprintf(mins_text, sizeof(mins_text), "%u", mins);
    lv_obj_t* mins_label = lv_label_create(countdown);
    lv_label_set_text(mins_label, mins_text);
    lv_obj_set_style_text_font(mins_label, font, 0);
    lv_obj_set_style_text_color(mins_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    return countdown;
}

// Grouped-view countdown in a rounded tile
void make_countdown_tile(lv_obj_t* parent, const TrainArrivalItem& item, uint8_t mins) {
    lv_obj_t* tile = make_countdown(parent, item, mins, kTileCountdownFont);
    lv_obj_set_style_pad_hor(tile, kCountdownTilePadPx, 0);
    lv_obj_set_style_radius(tile, kCountdownTileRadiusPx, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(THEME_COLOR_SCREENSAVER_PILL_BG), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
}

// Direction + optional station, stacked; grows to fill the space between the
// badge and whatever sits at the row's right edge
void make_watch_text_column(lv_obj_t* row, const TrainArrivalItem& item) {
    lv_obj_t* text_col = make_flex_container(row, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_width(text_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text_col, 1);

    lv_obj_t* direction_label = lv_label_create(text_col);
    lv_label_set_text(direction_label, item.direction);
    lv_obj_set_size(direction_label, LV_PCT(100), lv_font_get_line_height(&lv_font_montserrat_24));
    lv_obj_set_style_text_font(direction_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(direction_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_label_set_long_mode(direction_label, LV_LABEL_LONG_DOT);

    if (item.station[0] != '\0') {
        lv_obj_t* station_label = lv_label_create(text_col);
        lv_label_set_text(station_label, item.station);
        lv_obj_set_size(station_label, LV_PCT(100), lv_font_get_line_height(&lv_font_montserrat_20));
        lv_obj_set_style_text_font(station_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(station_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        lv_label_set_long_mode(station_label, LV_LABEL_LONG_DOT);
    }
}

// Empty/loading/error message centered on a page with no rows, or the stale
// marker beneath the rows it does have
void add_trains_status(lv_obj_t* page, bool have_data, int rows, bool stale) {
    if (rows == 0) {
        lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        const char* message = "No upcoming trains";
        NetworkState state = train_data_client.get_state();
        if (!train_data_client.has_config()) {
            message = "WiFi not set up\n\nRun:\ngrinder.py wifi";
        } else if (!have_data && state == NetworkState::ERROR) {
            message = "Gateway\nunreachable";
        } else if (!have_data) {
            message = "Loading trains...";
        }
        lv_obj_t* label = lv_label_create(page);
        lv_label_set_text(label, message);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    if (stale) {
        lv_obj_t* stale_label = lv_label_create(page);
        lv_label_set_text(stale_label, LV_SYMBOL_WARNING " stale data");
        lv_obj_set_style_text_font(stale_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(stale_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    }
}

// Splits entries into fixed pages and clamps the shown page so a page that
// vanished from the feed falls back to the last one that still exists
void resolve_trains_page(int entry_count, int rows_per_page, uint8_t& page, uint8_t& page_count) {
    int pages = entry_count > 0 ? (entry_count + rows_per_page - 1) / rows_per_page : 1;
    page_count = static_cast<uint8_t>(pages);
    if (page >= pages) {
        page = static_cast<uint8_t>(pages - 1);
    }
}

// Vertical dot column in the page's left gutter, one dot per page with the
// shown page lit; floats outside the flex flow so the rows keep their layout
void add_page_dots(lv_obj_t* page, int page_count, int active_page) {
    if (page_count <= 1) {
        return;
    }
    lv_obj_set_style_pad_left(page, kTrainsPagePadLeftPx + kPageDotGutterPx, 0);

    lv_obj_t* dots = make_flex_container(page, LV_FLEX_FLOW_COLUMN, kPageDotGapPx);
    lv_obj_set_size(dots, kPageDotSizePx, LV_SIZE_CONTENT);
    lv_obj_add_flag(dots, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(dots, LV_ALIGN_LEFT_MID, -(kPageDotGutterPx + kPageDotSizePx) / 2, 0);

    for (int i = 0; i < page_count; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_set_size(dot, kPageDotSizePx, kPageDotSizePx);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(
            dot,
            lv_color_hex(i == active_page ? THEME_COLOR_TEXT_PRIMARY : THEME_COLOR_SCREENSAVER_PAGE_DOT),
            0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
}

} // namespace

void ScreensaverOverlay::create() {
    if (overlay_) {
        return;
    }

    overlay_ = lv_tileview_create(lv_layer_top());
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(overlay_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_set_scrollbar_mode(overlay_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    // Gestures bubble up from the tiles and stop here, so one handler covers
    // swipes that start anywhere on the overlay
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(overlay_, gesture_cb, LV_EVENT_GESTURE, this);

    // Tiles are not draggable; a swipe jumps straight to the neighboring tile
    lv_obj_t** tiles[] = {&wave_tile_, &grouped_tile_, &board_tile_};
    for (int i = 0; i < kVariantCount; i++) {
        lv_obj_t* tile = lv_tileview_add_tile(overlay_, i, 0, LV_DIR_NONE);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_add_event_cb(tile, pressed_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(tile, clicked_cb, LV_EVENT_CLICKED, this);
        *tiles[i] = tile;
    }
    lv_obj_add_event_cb(wave_tile_, draw_cb, LV_EVENT_DRAW_MAIN, this);

    for (int row = 0; row < kDotRows; row++) {
        for (int col = 0; col < kDotCols; col++) {
            float dx = static_cast<float>(kGridOriginX + col * kDotSpacingPx - HW_DISPLAY_WIDTH_PX / 2);
            float dy = static_cast<float>(kGridOriginY + row * kDotSpacingPx - HW_DISPLAY_HEIGHT_PX / 2);
            dot_distance_px_[row * kDotCols + col] = static_cast<uint16_t>(sqrtf(dx * dx + dy * dy));
        }
    }

    lv_color_t dark = lv_color_hex(THEME_COLOR_SCREENSAVER_DOT_DARK);
    lv_color_t mid = lv_color_hex(THEME_COLOR_SCREENSAVER_DOT);
    lv_color_t bright = lv_color_hex(THEME_COLOR_SCREENSAVER_DOT_BRIGHT);
    for (int i = 0; i < kShadeCount; i++) {
        int half = kShadeCount / 2;
        if (i < half) {
            shade_lut_[i] = lv_color_mix(mid, dark, (255 * i) / (half - 1));
        } else {
            shade_lut_[i] = lv_color_mix(bright, mid, (255 * (i - half)) / (kShadeCount - half - 1));
        }
    }
}

void ScreensaverOverlay::show() {
    if (!overlay_ || visible_) {
        return;
    }

    Preferences prefs;
    prefs.begin("screensaver", true);
    // Devices provisioned before variants existed carry the legacy style/layout keys
    int fallback = prefs.getInt("style", 0) == 1 ? 1 + prefs.getInt("layout", 0) : 0;
    int variant = prefs.getInt("variant", fallback);
    prefs.end();
    if (variant < 0 || variant >= kVariantCount) {
        variant = 0;
    }
    variant_ = static_cast<ScreensaverVariant>(variant);

    visible_ = true;
    grouped_page_ = 0;
    board_page_ = 0;
    lv_tileview_set_tile_by_index(overlay_, variant, 0, LV_ANIM_OFF);
    lv_obj_move_foreground(overlay_);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    // Build both trains pages up front so a swipe never reveals a blank page
    refresh_trains(true);
    start_variant();
}

void ScreensaverOverlay::hide() {
    if (!overlay_ || !visible_) {
        return;
    }

    visible_ = false;
    stop_variant();
    if (grouped_container_) {
        lv_obj_del(grouped_container_);
        grouped_container_ = nullptr;
    }
    if (board_container_) {
        lv_obj_del(board_container_);
        board_container_ = nullptr;
    }
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

void ScreensaverOverlay::start_variant() {
    if (variant_ == ScreensaverVariant::WAVE) {
        phase_ = 0.0f;
        timer_ = lv_timer_create(tick_cb, kWaveTickMs, this);
    } else {
        train_data_client.set_polling_active(true);
        refresh_trains(true);
        timer_ = lv_timer_create(tick_cb, kTrainsTickMs, this);
    }
}

void ScreensaverOverlay::stop_variant() {
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    train_data_client.set_polling_active(false);
}

void ScreensaverOverlay::set_variant(ScreensaverVariant variant) {
    stop_variant();
    variant_ = variant;

    Preferences prefs;
    prefs.begin("screensaver", false);
    prefs.putInt("variant", static_cast<int>(variant));
    prefs.end();

    start_variant();
}

// Jumps one page left (-1) or right (+1) with no transition; swipes past
// either end do nothing
void ScreensaverOverlay::step_variant(int direction) {
    int index = static_cast<int>(variant_) + direction;
    if (index < 0 || index >= kVariantCount) {
        return;
    }
    set_variant(static_cast<ScreensaverVariant>(index));
    lv_tileview_set_tile_by_index(overlay_, index, 0, LV_ANIM_OFF);
}

// Renders the next (+1) or previous (-1) page of the visible trains view
// outright; swipes past either end do nothing
void ScreensaverOverlay::step_trains_page(int direction) {
    uint8_t* page = nullptr;
    uint8_t page_count = 1;
    if (variant_ == ScreensaverVariant::TRAINS_GROUPED) {
        page = &grouped_page_;
        page_count = grouped_page_count_;
    } else if (variant_ == ScreensaverVariant::TRAINS_BOARD) {
        page = &board_page_;
        page_count = board_page_count_;
    }
    if (!page) {
        return;
    }
    int index = *page + direction;
    if (index < 0 || index >= page_count) {
        return;
    }
    *page = static_cast<uint8_t>(index);
    refresh_trains(true);
}

void ScreensaverOverlay::draw_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (!self || !self->visible_) {
        return;
    }
    self->draw_wave(lv_event_get_layer(e));
}

void ScreensaverOverlay::pressed_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (self) {
        self->swiped_ = false;
    }
}

// LVGL still reports a click when a swipe's finger lifts, so a touch that
// paged must not also dismiss
void ScreensaverOverlay::clicked_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (!self || !self->visible_ || self->swiped_) {
        return;
    }
    self->hide();
}

void ScreensaverOverlay::gesture_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (!self || !self->visible_) {
        return;
    }

    // Horizontal swipes switch screensaver pages, vertical swipes page the
    // trains list
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    self->swiped_ = true;
    switch (dir) {
        case LV_DIR_LEFT:
            self->step_variant(1);
            break;
        case LV_DIR_RIGHT:
            self->step_variant(-1);
            break;
        case LV_DIR_TOP:
            self->step_trains_page(1);
            break;
        case LV_DIR_BOTTOM:
            self->step_trains_page(-1);
            break;
        default:
            break;
    }
}

void ScreensaverOverlay::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_timer_get_user_data(timer));
    if (!self || !self->visible_) {
        return;
    }

    if (self->variant_ != ScreensaverVariant::WAVE) {
        self->refresh_trains(false);
        return;
    }

    self->phase_ += kTwoPi * (kWaveTickMs / 1000.0f) / kWavePeriodS;
    if (self->phase_ >= kTwoPi) {
        self->phase_ -= kTwoPi;
    }
    lv_obj_invalidate(self->wave_tile_);
}

void ScreensaverOverlay::draw_wave(lv_layer_t* layer) {
    constexpr float kWaveNumber = kTwoPi / kWaveLengthPx;

    // Anchor the grid to the tile's on-screen coordinates rather than assuming
    // the tile sits at the screen origin
    lv_area_t coords;
    lv_obj_get_coords(wave_tile_, &coords);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;
    dsc.bg_opa = LV_OPA_COVER;

    for (int row = 0; row < kDotRows; row++) {
        for (int col = 0; col < kDotCols; col++) {
            float distance = static_cast<float>(dot_distance_px_[row * kDotCols + col]);
            float s = 0.5f + 0.35f * sinf(distance * kWaveNumber - phase_) +
                      0.15f * sinf(distance * kWaveNumber * kSecondWaveFrequency - phase_ * 1.3f);
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;

            dsc.bg_color = shade_lut_[static_cast<int>(s * (kShadeCount - 1) + 0.5f)];
            int radius = 1 + static_cast<int>(s * 3.0f + 0.5f);
            int cx = coords.x1 + kGridOriginX + col * kDotSpacingPx;
            int cy = coords.y1 + kGridOriginY + row * kDotSpacingPx;

            lv_area_t area;
            area.x1 = cx - radius;
            area.y1 = cy - radius;
            area.x2 = cx + radius;
            area.y2 = cy + radius;
            lv_draw_rect(layer, &dsc, &area);
        }
    }
}

void ScreensaverOverlay::refresh_trains(bool force) {
    TrainArrivals arrivals;
    bool have_data = train_data_client.get_arrivals(arrivals);
    NetworkState state = train_data_client.get_state();

    // Minutes are counted down locally between polls, and a snapshot the
    // gateway stopped refreshing is first flagged as stale, then hidden
    uint32_t age_ms = have_data ? millis() - arrivals.fetched_at_ms : 0;
    uint32_t elapsed_min = age_ms / 60000;
    uint8_t staleness = age_ms >= NET_DATA_EXPIRED_MS ? 2 : (age_ms >= NET_DATA_STALE_MS ? 1 : 0);

    if (!force && arrivals.fetched_at_ms == rendered_fetch_ms_ && state == rendered_state_ &&
        elapsed_min == rendered_elapsed_min_ && staleness == rendered_staleness_) {
        return;
    }
    rendered_fetch_ms_ = arrivals.fetched_at_ms;
    rendered_state_ = state;
    rendered_elapsed_min_ = elapsed_min;
    rendered_staleness_ = staleness;

    rebuild_trains_views(arrivals, have_data && staleness < 2, elapsed_min, staleness == 1);
}

// Both trains pages are rebuilt together so whichever one a swipe reveals
// is already populated
void ScreensaverOverlay::rebuild_trains_views(const TrainArrivals& arrivals, bool have_data,
                                              uint32_t elapsed_min, bool device_stale) {
    if (grouped_container_) {
        lv_obj_del(grouped_container_);
        grouped_container_ = nullptr;
    }
    if (board_container_) {
        lv_obj_del(board_container_);
        board_container_ = nullptr;
    }

    grouped_container_ = make_trains_page(grouped_tile_, kGroupedRowGapPx);
    board_container_ = make_trains_page(board_tile_, kBoardRowGapPx);

    bool stale = arrivals.gateway_stale || device_stale;
    if (!have_data) {
        grouped_page_ = 0;
        grouped_page_count_ = 1;
        board_page_ = 0;
        board_page_count_ = 1;
    }
    int grouped_rows = have_data ? build_grouped_rows(grouped_container_, arrivals, elapsed_min, stale) : 0;
    int board_rows = have_data ? build_board_rows(board_container_, arrivals, elapsed_min, stale) : 0;

    add_trains_status(grouped_container_, have_data, grouped_rows, stale);
    add_trains_status(board_container_, have_data, board_rows, stale);
    add_page_dots(grouped_container_, grouped_page_count_, grouped_page_);
    add_page_dots(board_container_, board_page_count_, board_page_);
}

// One entry per watch, in gateway order: bullet + destination/station, then a
// full-width row of countdown tiles with every arrival. Trains arriving right
// now show as 0 min; only trains the local countdown has pushed past due are
// dropped, and watches left empty are hidden. Watches beyond one screenful
// are split across pages the user swipes between.
int ScreensaverOverlay::build_grouped_rows(lv_obj_t* parent, const TrainArrivals& arrivals,
                                           uint32_t elapsed_min, bool stale) {
    WatchEntry entries[NET_MAX_ARRIVAL_ITEMS];
    int entry_count = 0;
    for (int i = 0; i < arrivals.item_count; i++) {
        const TrainArrivalItem& item = arrivals.items[i];
        WatchEntry entry = {&item, {}, 0};
        for (int m = 0; m < item.mins_count; m++) {
            if (item.mins[m] < elapsed_min) {
                continue;
            }
            entry.mins[entry.count++] = static_cast<uint8_t>(item.mins[m] - elapsed_min);
        }
        if (entry.count > 0) {
            entries[entry_count++] = entry;
        }
    }

    // Pages fill to capacity in order (five watches show as 4+1) so a watch
    // stays on the same page as the count changes; the stale marker takes
    // the last slot on every page
    int rows_per_page = stale ? kMaxGroupedRows - 1 : kMaxGroupedRows;
    resolve_trains_page(entry_count, rows_per_page, grouped_page_, grouped_page_count_);
    int first = grouped_page_ * rows_per_page;
    int last = std::min(entry_count, first + rows_per_page);

    for (int i = first; i < last; i++) {
        const WatchEntry& entry = entries[i];
        const TrainArrivalItem& item = *entry.item;

        lv_obj_t* entry_box = make_flex_container(parent, LV_FLEX_FLOW_COLUMN, kGroupedTilesGapPx);

        lv_obj_t* row = make_flex_container(entry_box, LV_FLEX_FLOW_ROW, 12);
        make_route_badge(row, item);
        make_watch_text_column(row, item);

        // The "mins" label sits centered under the route bullet and the tiles
        // start where the text column does
        lv_obj_t* tiles_row = make_flex_container(entry_box, LV_FLEX_FLOW_ROW, kGroupedTileGapPx);
        lv_obj_t* unit_label = make_unit_label(tiles_row);
        lv_obj_set_width(unit_label, kBadgeSizePx);
        lv_obj_set_style_text_align(unit_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_margin_right(unit_label, kGroupedUnitMarginPx, 0);

        // Tiles this large no longer all fit; the row keeps every arrival and
        // lets the far-out ones run off the right edge
        for (int m = 0; m < entry.count; m++) {
            make_countdown_tile(tiles_row, item, entry.mins[m]);
        }
    }

    return last - first;
}

// Flat departure board: one row per upcoming train sorted by arrival time,
// with a big countdown on the right. Trains arriving right now show as 0 min;
// only trains the local countdown has pushed past due are dropped. Trains
// beyond one screenful are split across pages the user swipes between.
int ScreensaverOverlay::build_board_rows(lv_obj_t* parent, const TrainArrivals& arrivals,
                                         uint32_t elapsed_min, bool stale) {
    BoardEntry entries[NET_MAX_ARRIVAL_ITEMS * NET_MAX_ARRIVAL_MINS];
    int entry_count = 0;
    for (int i = 0; i < arrivals.item_count; i++) {
        const TrainArrivalItem& item = arrivals.items[i];
        for (int m = 0; m < item.mins_count; m++) {
            if (item.mins[m] < elapsed_min) {
                continue;
            }
            entries[entry_count++] = {&item, static_cast<uint8_t>(item.mins[m] - elapsed_min)};
        }
    }

    std::stable_sort(entries, entries + entry_count,
                     [](const BoardEntry& a, const BoardEntry& b) { return a.min < b.min; });

    // The stale marker takes the last row's slot on every page
    int rows_per_page = stale ? kMaxBoardRows - 1 : kMaxBoardRows;
    resolve_trains_page(entry_count, rows_per_page, board_page_, board_page_count_);
    int first = board_page_ * rows_per_page;
    int last = std::min(entry_count, first + rows_per_page);

    // "mins" column header flush with the countdown digits at the right edge,
    // pulled down close to the first row
    if (last > first) {
        lv_obj_t* header = make_unit_label(parent);
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_style_text_align(header, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_margin_bottom(header, -kBoardHeaderTuckPx, 0);
    }

    for (int i = first; i < last; i++) {
        const TrainArrivalItem& item = *entries[i].item;

        lv_obj_t* row = make_flex_container(parent, LV_FLEX_FLOW_ROW, 12);
        make_route_badge(row, item);
        make_watch_text_column(row, item);
        make_countdown(row, item, entries[i].min, kBoardCountdownFont);
    }
    return last - first;
}
