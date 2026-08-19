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

/* The panel is RGB565, but the simulator renders 32bpp. Homebrew's SDL2 is
 * sdl2-compat on top of SDL3, whose SDL_PIXELFORMAT_RGB565 texture path takes
 * the bytes in the opposite order, which turns greys green and dark glyphs
 * pink while leaving black and white - palindromic in RGB565 - looking right.
 * ARGB8888 is the SDL backend's native texture format, so it avoids the
 * conversion entirely. Only colour precision differs from the device, and no
 * firmware code touches raw pixels. */
#undef LV_COLOR_DEPTH
#define LV_COLOR_DEPTH 32

/* Draw into an SDL window instead of the AMOLED panel. */
#undef LV_USE_SDL
#define LV_USE_SDL 1

/* Lets the simulator capture LVGL's own output for verification. */
#undef LV_USE_SNAPSHOT
#define LV_USE_SNAPSHOT 1
#define LV_SDL_INCLUDE_PATH     <SDL2/SDL.h>
/* PARTIAL matches the render mode the device's display_manager.cpp uses. */
#define LV_SDL_RENDER_MODE      LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_SDL_BUF_COUNT        2
#define LV_SDL_ACCELERATED      1
#define LV_SDL_FULLSCREEN       0
#define LV_SDL_DIRECT_EXIT      1
#define LV_SDL_MOUSEWHEEL_MODE  LV_SDL_MOUSEWHEEL_MODE_ENCODER

/* A 32bpp full-screen snapshot is ~500 kB on its own, so the device's 256 kB
 * pool is raised here. The simulator therefore does not reproduce the device's
 * LVGL allocation ceiling. */
#undef LV_MEM_SIZE
#define LV_MEM_SIZE (2048U * 1024U)

#undef LV_MEM_POOL_INCLUDE
#undef LV_MEM_POOL_ALLOC
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC(size) malloc(size)
