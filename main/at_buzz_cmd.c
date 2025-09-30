/* LEDC (LED Controller) basic example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "driver/ledc.h"
#include "esp_err.h"

// #define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include "esp_at.h"

static const char *TAG = "esp_at_buzz_cmd";

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (8) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (4096) // Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz

/* Warning:
 * For ESP32, ESP32S2, ESP32S3, ESP32C3, ESP32C2, ESP32C6, ESP32H2 (rev < 1.2), ESP32P4 targets,
 * when LEDC_DUTY_RES selects the maximum duty resolution (i.e. value equal to SOC_LEDC_TIMER_BIT_WIDTH),
 * 100% duty cycle is not reachable (duty cannot be set to (2 ** SOC_LEDC_TIMER_BIT_WIDTH)).
 */

void at_buzz_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static uint8_t at_setup_cmd_buzz(uint8_t para_num)
{
    // get first parameter, and parse it into a digit
    int32_t digit = 0;
    if (esp_at_get_para_as_digit(0, &digit) != ESP_AT_PARA_PARSE_RESULT_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    if (digit == 0) {
        // Stop sound
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
        // Update duty to apply the new value
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

        return ESP_AT_RESULT_CODE_OK;
    }

    if (digit < 20 || digit > 20000) {
        ESP_LOGE(TAG, "Frequency value out of range (20-20000): %d", digit);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // Set frequency
    ESP_ERROR_CHECK(ledc_set_freq(LEDC_MODE, LEDC_TIMER, digit));
    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    return ESP_AT_RESULT_CODE_OK;
}

static const esp_at_cmd_struct at_buzz_cmd[] = {
    {"+BUZZ", NULL, NULL, at_setup_cmd_buzz, NULL},
};

bool esp_at_buzz_cmd_regist(void)
{
    ESP_LOGI(TAG, "registering");
    return esp_at_custom_cmd_array_regist(at_buzz_cmd, sizeof(at_buzz_cmd) / sizeof(at_buzz_cmd[0]));
}

ESP_AT_CMD_SET_FIRST_INIT_FN(esp_at_buzz_cmd_regist, 22);
