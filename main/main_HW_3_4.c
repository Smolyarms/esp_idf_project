#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/touch_sens.h"
#include "esp_log.h"

// Піни та параметри LEDC
#define BUZZER_PIN          GPIO_NUM_2
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT // Роздільна здатність 10 біт (0 - 1023)
#define LEDC_DUTY           512               // 50% заповнення (меандр для максимальної гучності)

// Частоти нот (октава 4)
#define NOTE_C4  262
#define NOTE_G4  392
#define NOTE_A4  440


#define TOUCH_CHANNEL_C     4
#define TOUCH_CHANNEL_G     5
#define TOUCH_CHANNEL_A     6
#define TOUCH_THRESHOLD     5000

static const char *TAG = "touch_synth";
static touch_sensor_handle_t touch_sensor;
static touch_channel_handle_t touch_channel_c;
static touch_channel_handle_t touch_channel_g;
static touch_channel_handle_t touch_channel_a;

// Ініціалізація LEDC PWM
static void buzzer_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = 1000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BUZZER_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static void touch_init(void)
{
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)
    };
    touch_sensor_config_t sensor_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sensor_cfg, &touch_sensor));

    touch_channel_config_t channel_cfg = {
        .active_thresh = {TOUCH_THRESHOLD},
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT
    };
    ESP_ERROR_CHECK(touch_sensor_new_channel(touch_sensor, TOUCH_CHANNEL_C, &channel_cfg, &touch_channel_c));
    ESP_ERROR_CHECK(touch_sensor_new_channel(touch_sensor, TOUCH_CHANNEL_G, &channel_cfg, &touch_channel_g));
    ESP_ERROR_CHECK(touch_sensor_new_channel(touch_sensor, TOUCH_CHANNEL_A, &channel_cfg, &touch_channel_a));
    ESP_ERROR_CHECK(touch_sensor_enable(touch_sensor));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(touch_sensor));
}

static bool touch_detected(uint32_t baseline, uint32_t value)
{
    uint32_t difference = baseline > value ? baseline - value : value - baseline;
    return difference > TOUCH_THRESHOLD;
}

static void play_tone(uint32_t frequency)
{
    if (frequency == 0) {
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
    } else {
        ESP_ERROR_CHECK(ledc_set_freq(LEDC_MODE, LEDC_TIMER, frequency));
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    }
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Ініціалізація сенсорного синтезатора...");
    buzzer_init();
    touch_init();
    vTaskDelay(pdMS_TO_TICKS(100));

    uint32_t baseline_c = 0;
    uint32_t baseline_g = 0;
    uint32_t baseline_a = 0;
    ESP_ERROR_CHECK(touch_channel_read_data(touch_channel_c, TOUCH_CHAN_DATA_TYPE_RAW, &baseline_c));
    ESP_ERROR_CHECK(touch_channel_read_data(touch_channel_g, TOUCH_CHAN_DATA_TYPE_RAW, &baseline_g));
    ESP_ERROR_CHECK(touch_channel_read_data(touch_channel_a, TOUCH_CHAN_DATA_TYPE_RAW, &baseline_a));
    ESP_LOGI(TAG, "Готово: GPIO4=C, GPIO5=G, GPIO6=A");

    while (1) {
        uint32_t value_c = 0;
        uint32_t value_g = 0;
        uint32_t value_a = 0;
        ESP_ERROR_CHECK(touch_channel_read_data(touch_channel_c, TOUCH_CHAN_DATA_TYPE_RAW, &value_c));
        ESP_ERROR_CHECK(touch_channel_read_data(touch_channel_g, TOUCH_CHAN_DATA_TYPE_RAW, &value_g));
        ESP_ERROR_CHECK(touch_channel_read_data(touch_channel_a, TOUCH_CHAN_DATA_TYPE_RAW, &value_a));

        uint32_t frequency = 0;
        if (touch_detected(baseline_c, value_c)) {
            frequency = NOTE_C4;
        } else if (touch_detected(baseline_g, value_g)) {
            frequency = NOTE_G4;
        } else if (touch_detected(baseline_a, value_a)) {
            frequency = NOTE_A4;
        }

        play_tone(frequency);
        printf(">Active_Note_Hz:%lu\n", (unsigned long)frequency);
        printf(">Touch_C:%lu\n", (unsigned long)value_c);
        printf(">Touch_G:%lu\n", (unsigned long)value_g);
        printf(">Touch_A:%lu\n", (unsigned long)value_a);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}