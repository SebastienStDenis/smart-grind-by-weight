#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

/* The simulator never applies an image; these types exist so the OTA handler
 * header compiles. The handler itself is replaced by sim_bluetooth.cpp. */
typedef struct esp_partition_t esp_partition_t;
typedef uint32_t esp_ota_handle_t;
