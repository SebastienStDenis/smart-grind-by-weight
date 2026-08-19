#pragma once
#include <cstdint>
#include "esp_err.h"

typedef struct rmt_encoder_t* rmt_encoder_handle_t;

typedef struct {
    uint32_t duration0 : 15;
    uint32_t level0 : 1;
    uint32_t duration1 : 15;
    uint32_t level1 : 1;
} rmt_symbol_word_t;

typedef struct {
    int reserved;
} rmt_copy_encoder_config_t;

static inline esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t* config, rmt_encoder_handle_t* out) {
    (void)config;
    if (out) *out = nullptr;
    return ESP_FAIL;
}
static inline esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) { (void)encoder; return ESP_FAIL; }
