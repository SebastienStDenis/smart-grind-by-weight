/**
 * DisplayManager and TouchDriver for the host.
 *
 * Draws through LVGL's SDL backend at the panel's native 280x456 and feeds
 * mouse state into the same TouchDriver the firmware polls, so touch timing
 * and the idle/screensaver logic behave as they do on the device.
 */

#include "hardware/display_manager.h"

#include <lvgl.h>
#include <SDL2/SDL.h>

#include <Arduino.h>

#include <cstdlib>

#include "config/constants.h"

DisplayManager* g_display_manager = nullptr;

namespace {

constexpr float kMinZoom = 0.5f;
constexpr float kMaxZoom = 4.0f;
constexpr float kZoomStep = 0.25f;

float window_zoom() {
    const char* setting = std::getenv("SIM_ZOOM");
    float zoom = setting ? strtof(setting, nullptr) : 0.0f;
    if (zoom < kMinZoom || zoom > kMaxZoom) zoom = 1.0f;
    return zoom;
}

float active_zoom = 1.0f;

/* Panel brightness is emulated with a black scrim on the system layer, which
 * is what the screensaver's dim step actually looks like to the user. */
lv_obj_t* dim_overlay = nullptr;

/** The panel is a fixed 280x456, so the window scales rather than reflows.
 *  '-' and '+' step the zoom, '0' returns to 1:1. */
void apply_zoom_keys(lv_display_t* display) {
    static bool minus_was_down = false;
    static bool plus_was_down = false;
    static bool reset_was_down = false;

    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) return;

    bool minus_down = keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS];
    bool plus_down = keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS];
    bool reset_down = keys[SDL_SCANCODE_0] || keys[SDL_SCANCODE_KP_0];

    float zoom = active_zoom;
    if (minus_down && !minus_was_down) zoom -= kZoomStep;
    if (plus_down && !plus_was_down) zoom += kZoomStep;
    if (reset_down && !reset_was_down) zoom = 1.0f;

    minus_was_down = minus_down;
    plus_was_down = plus_down;
    reset_was_down = reset_down;

    if (zoom < kMinZoom) zoom = kMinZoom;
    if (zoom > kMaxZoom) zoom = kMaxZoom;

    if (zoom != active_zoom) {
        active_zoom = zoom;
        lv_sdl_window_set_zoom(display, active_zoom);
    }
}

/** Writes the rendered window to a BMP once, SIM_SNAPSHOT_MS after boot.
 *  Lets the panel output be inspected without screen-recording permission. */
void maybe_write_snapshot(lv_display_t* display) {
    static const char* path = std::getenv("SIM_SNAPSHOT");
    static bool done = false;
    if (!path || !*path || done) return;

    const char* delay_setting = std::getenv("SIM_SNAPSHOT_MS");
    uint32_t delay_ms = delay_setting ? (uint32_t)strtoul(delay_setting, nullptr, 10) : 3000;
    if (millis() < delay_ms) return;

    (void)display;
    done = true;

    /* Captured from LVGL rather than the SDL backbuffer, whose contents are
     * undefined after a present. */
    lv_draw_buf_t* frame = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!frame) {
        LOG_BLE("[SIM] Snapshot failed: lv_snapshot_take returned nothing\n");
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        frame->data, frame->header.w, frame->header.h, 32,
        (int)frame->header.stride, SDL_PIXELFORMAT_ARGB8888);

    if (surface) {
        SDL_SaveBMP(surface, path);
        SDL_FreeSurface(surface);
        LOG_BLE("[SIM] Snapshot written to %s (%dx%d)\n", path,
                (int)frame->header.w, (int)frame->header.h);
    }

    lv_draw_buf_destroy(frame);
}

void ensure_dim_overlay() {
    if (dim_overlay) return;

    dim_overlay = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(dim_overlay);
    lv_obj_set_size(dim_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dim_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dim_overlay, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(dim_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(dim_overlay, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

void DisplayManager::init() {
    g_display_manager = this;

    lv_init();
    lv_tick_set_cb(millis_cb);

    screen_width = HW_DISPLAY_WIDTH_PX;
    screen_height = HW_DISPLAY_HEIGHT_PX;
    draw_buffer = nullptr;
    dma_staging_buffer = nullptr;
    dma_staging_rows = 0;
    buffer_size = 0;

    lvgl_display = lv_sdl_window_create(screen_width, screen_height);
    if (!lvgl_display) {
        LOG_BLE("[DISPLAY] ERROR: Failed to create the SDL window\n");
        return;
    }

    active_zoom = window_zoom();
    lv_sdl_window_set_zoom(lvgl_display, active_zoom);
    lv_sdl_window_set_title(lvgl_display, "Smart Grind by Weight");

    touch_driver.init();
    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);
    lv_indev_set_display(lvgl_input, lvgl_display);

    ensure_dim_overlay();

    initialized = true;
    LOG_BLE("[DISPLAY] SDL window ready: %ux%u at %.1fx zoom\n",
            (unsigned)screen_width, (unsigned)screen_height, active_zoom);
}

void DisplayManager::update() {
    if (!initialized) return;

    apply_zoom_keys(lvgl_display);
    touch_driver.update();
    lv_timer_handler();
    maybe_write_snapshot(lvgl_display);
}

void DisplayManager::set_brightness(float brightness) {
    if (!initialized) return;

    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;

    ensure_dim_overlay();

    static lv_opa_t applied_scrim = LV_OPA_TRANSP;
    lv_opa_t scrim = (lv_opa_t)((1.0f - brightness) * LV_OPA_COVER);
    if (scrim == applied_scrim) return;

    applied_scrim = scrim;
    lv_obj_set_style_bg_opa(dim_overlay, scrim, 0);
}

void DisplayManager::display_rounder_cb(lv_event_t* e) {
    (void)e;  // The SDL backend redraws whole areas; no row rounding needed.
}

void DisplayManager::display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

void DisplayManager::touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (!g_display_manager) return;

    TouchData touch = g_display_manager->get_touch_driver()->get_touch_data();
    data->point.x = touch.x;
    data->point.y = touch.y;
    data->state = touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

uint32_t DisplayManager::millis_cb() {
    return millis();
}

void TouchDriver::init() {
    last_touch = {0, 0, false};
    last_touch_time = millis();
    disabled = false;
    initialized = true;
}

void TouchDriver::update() {
    if (!initialized || disabled) return;

    int x = 0;
    int y = 0;
    uint32_t buttons = SDL_GetMouseState(&x, &y);
    bool pressed = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    if (pressed) {
        int32_t display_x = (int32_t)(x / active_zoom);
        int32_t display_y = (int32_t)(y / active_zoom);

        last_touch.x = (uint16_t)(display_x < 0 ? 0 : display_x);
        last_touch.y = (uint16_t)(display_y < 0 ? 0 : display_y);
        last_touch.pressed = true;
        last_touch_time = millis();
    } else {
        last_touch.pressed = false;
    }
}

void TouchDriver::disable() {
    disabled = true;
    last_touch.pressed = false;
}

void TouchDriver::enable() {
    disabled = false;
}

uint32_t TouchDriver::get_ms_since_last_touch() const {
    if (!initialized || disabled) return 0;
    return millis() - last_touch_time;
}
