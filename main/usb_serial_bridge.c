/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#define BUF_SIZE (1024)
#define TASK_STACK_SIZE (4096)

static void usb_serial_task(void *arg)
{
    // Configure USB SERIAL JTAG
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .rx_buffer_size = BUF_SIZE,
        .tx_buffer_size = BUF_SIZE,
    };

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_jtag_config));

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    // Leave pins on defaults (U0TXD/U0RXD at chip pins 28/27)
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    if (data == NULL) {
        ESP_LOGE("usb_serial_con", "no memory for data");
        return;
    }

    int64_t last_tx_ms = 0;
    int len;
    while (1) {
        len = usb_serial_jtag_read_bytes(data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if (len) {
            int64_t curr_tx_ms = esp_timer_get_time();
            if (len == 1 && curr_tx_ms - last_tx_ms > 100000 && data[0] == 0x10) {
                // Send a break if DLE (Data Link Escape) was sent after a pause of >0.1s
                uart_write_bytes_with_break(UART_NUM_0, (const char *) data, len, 160);
            } else{
                uart_write_bytes(UART_NUM_0, (const char *) data, len);
            }
            last_tx_ms = curr_tx_ms;
        }

        len = uart_read_bytes(UART_NUM_0, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if (len) usb_serial_jtag_write_bytes((const char *) data, len, 20 / portTICK_PERIOD_MS);
    }
}

void usb_serial_init(void)
{
    xTaskCreate(usb_serial_task, "USB SERIAL JTAG_task", TASK_STACK_SIZE, NULL, 10, NULL);
}
