/**
 * @file lv_conf.h
 * Host simulator LVGL configuration.
 *
 * Includes the firmware's own lv_conf.h verbatim and overrides only what the
 * host build cannot satisfy, so the simulator renders with the same widget,
 * font and draw settings as the device.
 */
#pragma once

#include "../include/lv_conf.h"

/* Draw into an SDL window instead of the AMOLED panel. */
#undef LV_USE_SDL
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH     <SDL2/SDL.h>
/* PARTIAL matches the device's render mode, and unlike DIRECT it does not rely
 * on the backbuffer surviving a present - which SDL3, under Homebrew's
 * sdl2-compat, does not guarantee. DIRECT flickers badly there. */
#define LV_SDL_RENDER_MODE      LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_SDL_BUF_COUNT        2
#define LV_SDL_ACCELERATED      1
#define LV_SDL_FULLSCREEN       0
#define LV_SDL_DIRECT_EXIT      1
#define LV_SDL_MOUSEWHEEL_MODE  LV_SDL_MOUSEWHEEL_MODE_ENCODER

/* The 256 kB pool size is kept so the simulator hits the same allocation
 * ceiling as the device; only the PSRAM-backed pool allocator is swapped. */
#undef LV_MEM_POOL_INCLUDE
#undef LV_MEM_POOL_ALLOC
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC(size) malloc(size)
