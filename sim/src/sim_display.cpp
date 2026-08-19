/**
 * DisplayManager and TouchDriver for the host.
 *
 * Draws through LVGL's SDL backend at the panel's native 280x456 and feeds
 * mouse state into the same TouchDriver the firmware polls, so touch timing
 * and the idle/screensaver logic behave as they do on the device.
 */

#include "hardware/display_manager.h"

#include <lvgl.h>
#include <display/lv_display_private.h>
#include <SDL2/SDL.h>

#include <Arduino.h>

#include <cstdlib>
#include <cstring>
#include <cmath>

#include "config/constants.h"

DisplayManager* g_display_manager = nullptr;

namespace {

constexpr float kMinZoom = 0.5f;
constexpr float kMaxZoom = 4.0f;
constexpr float kZoomStep = 0.25f;

/* Ceiling on the brightness scrim so a dimmed panel stays legible. */
constexpr float kMaxScrim = 0.55f;

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

    done = true;

    /* SIM_SNAPSHOT_SDL reads back what was actually presented, which is the
     * only way to see compositing faults; SIM_SNAPSHOT captures LVGL's own
     * output and so cannot show them. */
    if (std::getenv("SIM_SNAPSHOT_SDL")) {
        auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
        if (!renderer) return;

        int width = 0;
        int height = 0;
        SDL_GetRendererOutputSize(renderer, &width, &height);

        SDL_Surface* presented = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                                SDL_PIXELFORMAT_ARGB8888);
        if (!presented) return;

        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                 presented->pixels, presented->pitch) == 0) {
            SDL_SaveBMP(presented, path);
            LOG_BLE("[SIM] Presented-frame snapshot written to %s (%dx%d)\n", path, width, height);
        } else {
            LOG_BLE("[SIM] Presented-frame snapshot failed: %s\n", SDL_GetError());
        }

        SDL_FreeSurface(presented);
        return;
    }

    /* Captured from LVGL rather than the SDL backbuffer, whose contents are
     * undefined after a present. */
    const char* which = std::getenv("SIM_SNAPSHOT_OBJ");
    lv_obj_t* target = lv_screen_active();
    if (which && strcmp(which, "top") == 0) target = lv_layer_top();
    else if (which && strcmp(which, "sys") == 0) target = lv_layer_sys();

    lv_draw_buf_t* frame = lv_snapshot_take(target, LV_COLOR_FORMAT_ARGB8888);
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

/** Homebrew ships sdl2-compat on SDL3, whose RGB565 texture upload takes the
 *  bytes in the opposite order: greys render green and dark glyphs pink, while
 *  black and white survive because they are palindromic in RGB565. Swapping
 *  each flush before the backend sees it cancels that out, and keeps the
 *  simulator at the panel's real 16bpp depth. */
lv_display_flush_cb_t backend_flush = nullptr;

void swapping_flush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_width(area) * lv_area_get_height(area));
    backend_flush(display, area, px_map);
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

    /* Setting the zoom reallocates the SDL backend's framebuffer, and the
     * backend only clears it once during creation, so a redundant call leaves
     * uninitialised heap on screen. Only change it when it actually differs. */
    active_zoom = window_zoom();
    if (active_zoom != lv_sdl_window_get_zoom(lvgl_display)) {
        lv_sdl_window_set_zoom(lvgl_display, active_zoom);
    }
    lv_sdl_window_set_title(lvgl_display, "Smart Grind by Weight");

    /* Byte-swap every flush before the backend uploads it; see swapping_flush. */
    backend_flush = lvgl_display->flush_cb;
    lv_display_set_flush_cb(lvgl_display, swapping_flush);

    touch_driver.init();
    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);
    lv_indev_set_display(lvgl_input, lvgl_display);

    ensure_dim_overlay();

    initialized = true;
    LOG_BLE("[DISPLAY] SDL window ready: %ux%u at %.1fx zoom\n",
            (unsigned)screen_width, (unsigned)screen_height, active_zoom);

    /* Window, drawable and LVGL resolution should agree; a mismatch means the
     * panel is being letterboxed or scaled by the compositor. */
    int window_w = 0, window_h = 0, render_w = 0, render_h = 0;
    SDL_GetWindowSize(lv_sdl_window_get_window(lvgl_display), &window_w, &window_h);
    if (auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(lvgl_display))) {
        SDL_GetRendererOutputSize(renderer, &render_w, &render_h);
    }
    LOG_BLE("[DISPLAY] geometry: window %dx%d, drawable %dx%d, lvgl %dx%d\n",
            window_w, window_h, render_w, render_h,
            (int)lv_display_get_horizontal_resolution(lvgl_display),
            (int)lv_display_get_vertical_resolution(lvgl_display));

    /* If LVGL's pixel size and the SDL texture's disagree, only part of the
     * framebuffer reaches the screen. */
    lv_color_format_t cf = lv_display_get_color_format(lvgl_display);
    uint32_t texture_format = 0;
    int texture_w = 0, texture_h = 0;
    if (auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(lvgl_display))) {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) == 0 && info.num_texture_formats > 0) {
            texture_format = info.texture_formats[0];
        }
        (void)texture_w;
        (void)texture_h;
    }
    LOG_BLE("[DISPLAY] format: LV_COLOR_DEPTH=%d cf=%d px_size=%d stride=%d, SDL first fmt=%s\n",
            (int)LV_COLOR_DEPTH, (int)cf, (int)lv_color_format_get_size(cf),
            (int)lv_draw_buf_width_to_stride(screen_width, cf),
            SDL_GetPixelFormatName(texture_format));
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

    /* A backlight at 35% does not look 65% black: perceived lightness follows
     * roughly the 1/2.2 power of luminance. Scaling the scrim linearly made the
     * dimmed screensaver state unreadable, so it is gamma-corrected and capped. */
    static lv_opa_t applied_scrim = LV_OPA_TRANSP;
    float perceived = powf(brightness, 1.0f / 2.2f);
    float darkness = 1.0f - perceived;
    if (darkness > kMaxScrim) darkness = kMaxScrim;

    lv_opa_t scrim = (lv_opa_t)(darkness * LV_OPA_COVER);
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
