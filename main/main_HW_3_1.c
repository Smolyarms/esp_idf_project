#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LDR_ADC_UNIT          ADC_UNIT_1
#define LDR_ADC_CHANNEL       ADC_CHANNEL_5     // GPIO6
#define LED_GPIO              GPIO_NUM_7

#define SMA_WINDOW_SIZE       16
#define SAMPLE_PERIOD_MS      100
#define DARK_THRESHOLD_RAW    1500
#define LIGHT_THRESHOLD_RAW   1800

typedef struct {
    int samples[SMA_WINDOW_SIZE];
    int count;
    int next_index;
    int64_t sum;
} sma_filter_t;

static const char *TAG = "LDR";

static int sma_filter_add(sma_filter_t *filter, int sample)
{
    if (filter->count == SMA_WINDOW_SIZE) {
        filter->sum -= filter->samples[filter->next_index];
    } else {
        filter->count++;
    }

    filter->samples[filter->next_index] = sample;
    filter->sum += sample;
    filter->next_index = (filter->next_index + 1) % SMA_WINDOW_SIZE;

    return (int)(filter->sum / filter->count);
}

void app_main(void)
{
    const gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_config));
    ESP_ERROR_CHECK(gpio_set_level(LED_GPIO, 0));

    adc_oneshot_unit_handle_t adc1_handle = NULL;
    const adc_oneshot_unit_init_cfg_t adc_init_config = {
        .unit_id = LDR_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_init_config, &adc1_handle));

    const adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc1_handle, LDR_ADC_CHANNEL, &channel_config));

    sma_filter_t filter = {0};
    bool led_on = false;

    while (true) {
        int raw = 0;
        const esp_err_t read_result =
            adc_oneshot_read(adc1_handle, LDR_ADC_CHANNEL, &raw);

        if (read_result == ESP_OK) {
            const int filtered_raw = sma_filter_add(&filter, raw);

            if (!led_on && filtered_raw < DARK_THRESHOLD_RAW) {
                led_on = true;
            } else if (led_on && filtered_raw > LIGHT_THRESHOLD_RAW) {
                led_on = false;
            }

            ESP_ERROR_CHECK(gpio_set_level(LED_GPIO, led_on ? 1 : 0));
            ESP_LOGI(TAG, "ADC raw=%d, SMA=%d, LED=%s",
                     raw, filtered_raw, led_on ? "ON" : "OFF");
        } else {
            ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(read_result));
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}
