#pragma once
#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    uint32_t timeout_ms;
    uint32_t idle_core_mask;
    bool trigger_panic;
} esp_task_wdt_config_t;

esp_err_t esp_task_wdt_add(TaskHandle_t task);
esp_err_t esp_task_wdt_delete(TaskHandle_t task);
esp_err_t esp_task_wdt_reset();
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t* config);
