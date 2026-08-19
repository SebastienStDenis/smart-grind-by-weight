/**
 * Host stand-in for the Arduino_GFX types named in display_manager.h.
 * The simulator's DisplayManager draws through LVGL's SDL backend instead.
 */
#pragma once

#include <cstdint>

class Arduino_DataBus;
class Arduino_GFX;

#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xFFFF
