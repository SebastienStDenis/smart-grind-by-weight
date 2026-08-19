/**
 * Host stand-in for the ESP-IDF RMT transmitter.
 *
 * Grinder::start()/stop()/start_pulse_rmt() short-circuit to the mock load cell
 * when DEBUG_ENABLE_LOADCELL_MOCK is set, so these entry points are compiled
 * but never reached; they fail closed so a mistake cannot silently pass.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "gpio.h"
#include "rmt_encoder.h"

typedef struct rmt_channel_t* rmt_channel_handle_t;

typedef enum {
    RMT_CLK_SRC_DEFAULT = 0
} rmt_clock_source_t;

typedef struct {
    gpio_num_t gpio_num;
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    size_t trans_queue_depth;
} rmt_tx_channel_config_t;

typedef struct {
    int loop_count;
} rmt_transmit_config_t;

static inline esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t* config, rmt_channel_handle_t* out) {
    (void)config;
    if (out) *out = nullptr;
    return ESP_FAIL;
}
static inline esp_err_t rmt_enable(rmt_channel_handle_t channel) { (void)channel; return ESP_FAIL; }
static inline esp_err_t rmt_disable(rmt_channel_handle_t channel) { (void)channel; return ESP_FAIL; }
static inline esp_err_t rmt_del_channel(rmt_channel_handle_t channel) { (void)channel; return ESP_FAIL; }
static inline esp_err_t rmt_transmit(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder,
                                     const void* payload, size_t payload_bytes,
                                     const rmt_transmit_config_t* config) {
    (void)channel; (void)encoder; (void)payload; (void)payload_bytes; (void)config;
    return ESP_FAIL;
}
