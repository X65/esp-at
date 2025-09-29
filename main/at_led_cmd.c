/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2025 Tomasz Sterna <tomasz@sterna.link>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// #define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include "esp_at.h"

#include "driver/rmt_tx.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      5        // Pin 10 / MTDI / GPIO5

#define MAX_LED_NUMBERS             256
#define DEFAULT_LED_NUMBERS         4

static const char *TAG = "esp_at_led_cmd";

static uint8_t led_strip_pixels[MAX_LED_NUMBERS * 3];

static unsigned int led_used_no = 0;

static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t simple_encoder = NULL;

static const rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,
    .duration0 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0H=0.3us
    .level1 = 0,
    .duration1 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0L=0.9us
};

static const rmt_symbol_word_t ws2812_one = {
    .level0 = 1,
    .duration0 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1H=0.9us
    .level1 = 0,
    .duration1 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1L=0.3us
};

//reset defaults to 50uS
static const rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,
    .duration0 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,
    .duration1 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2,
};

static size_t encoder_callback(const void *data, size_t data_size,
                               size_t symbols_written, size_t symbols_free,
                               rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    // ESP_LOGI(TAG, "encode_callback: data_size=%d, symbols_written=%d, symbols_free=%d", data_size, symbols_written, symbols_free);
    // We need a minimum of 8 symbol spaces to encode a byte. We only
    // need one to encode a reset, but it's simpler to simply demand that
    // there are 8 symbol spaces free to write anything.
    if (symbols_free < 8) {
        return 0;
    }

    // We can calculate where in the data we are from the symbol pos.
    // Alternatively, we could use some counter referenced by the arg
    // parameter to keep track of this.
    size_t data_pos = symbols_written / 8;
    uint8_t *data_bytes = (uint8_t*)data;
    if (data_pos < data_size) {
        // Encode a byte
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            if (data_bytes[data_pos]&bitmask) {
                symbols[symbol_pos++] = ws2812_one;
            } else {
                symbols[symbol_pos++] = ws2812_zero;
            }
        }
        // We're done; we should have written 8 symbols.
        return symbol_pos;
    } else {
        //All bytes already are encoded.
        //Encode the reset, and we're done.
        symbols[0] = ws2812_reset;
        *done = 1; //Indicate end of the transaction.
        return 1; //we only wrote one symbol
    }
}

void at_led_set_value(uint8_t led_no, uint8_t red, uint8_t green, uint8_t blue)
{
    if (led_no >= MAX_LED_NUMBERS) {
        ESP_LOGE(TAG, "LED number %d is out of range", led_no);
        return;
    }
    led_strip_pixels[led_no * 3 + 0] = green;
    led_strip_pixels[led_no * 3 + 1] = red;
    led_strip_pixels[led_no * 3 + 2] = blue;
    if (led_no >= led_used_no) {
        led_used_no = led_no + 1;
    }
}

static void at_led_flush_no(uint8_t count) {
    if (count > led_used_no) {
        count = led_used_no;
    }
    ESP_LOGI(TAG, "Flush %d LEDs", count);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
    ESP_ERROR_CHECK(rmt_transmit(led_chan,
        simple_encoder,
        led_strip_pixels,
        count * 3,
        &tx_config));
}

void at_led_clear_all(void) {
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    led_used_no = 0;
        ESP_LOGI(TAG, "Clear %d LEDs", DEFAULT_LED_NUMBERS);
    for (int led = 0; led < DEFAULT_LED_NUMBERS; led++) {
        at_led_set_value(led, 0, 0, 0);
    }

    ESP_LOGI(TAG, "Set default values %d LEDs", led_used_no);
    at_led_flush_no(DEFAULT_LED_NUMBERS * 3);
}

void at_led_init(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Create simple callback-based encoder");
    const rmt_simple_encoder_config_t simple_encoder_cfg = {
        .callback = encoder_callback
        //Note we don't set min_chunk_size here as the default of 64 is good enough.
    };
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&simple_encoder_cfg, &simple_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    at_led_clear_all();
}

// gamma-corrected LUTs.
// Reserve code 0 as “off,” then spread codes 1..max to 2..255 with a power curve (γ≈2.2 works well for LEDs).
// Start from 2 to avoid very low values which may not be visible at all.
// 3-bit channels (R,G)
static const uint8_t LUT3[8] = { 0, 2, 7, 25, 57, 106, 171, 255 };
// 2-bit channel (B)
static const uint8_t LUT2[4] = { 0, 2, 57, 255 };

static uint8_t at_setup_cmd_led(uint8_t para_num)
{
    unsigned int index = 0;

    // parse all provided digit parameters
    int32_t digit = 0;
    while (index < MAX_LED_NUMBERS && esp_at_get_para_as_digit(index, &digit) == ESP_AT_PARA_PARSE_RESULT_OK) {
        if (digit < 0 || digit > 255) {
            return ESP_AT_RESULT_CODE_ERROR;
        }

        // convert RGB332 to separate R, G, B values
        uint8_t r8 = LUT3[(digit >> 5) & 0x7];
        uint8_t g8 = LUT3[(digit >> 2) & 0x7];
        uint8_t b8 = LUT2[digit & 0x3];
        ESP_LOGI(TAG, "Set LED %d to %02x / %d,%d,%d", index, digit, r8, g8, b8);

        at_led_set_value(index, r8, g8, b8);
        index++;
    }

    // fail if nothing parsed
    if (index == 0) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    at_led_flush_no(index);

    return ESP_AT_RESULT_CODE_OK;
}

static const esp_at_cmd_struct at_led_cmd[] = {
    {"+LED", NULL, NULL, at_setup_cmd_led, NULL},
};

bool esp_at_led_cmd_regist(void)
{
    ESP_LOGI(TAG, "registering");
    return esp_at_custom_cmd_array_regist(at_led_cmd, sizeof(at_led_cmd) / sizeof(at_led_cmd[0]));
}

ESP_AT_CMD_SET_FIRST_INIT_FN(esp_at_led_cmd_regist, 22);
