#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "SERVO";

#define SERVO_GPIO          4
#define POT_ADC_UNIT        ADC_UNIT_1
#define POT_ADC_CHANNEL     ADC_CHANNEL_0  // GPIO1 на ESP32-S3

#define SERVO_TIMER         LEDC_TIMER_0
#define SERVO_CHANNEL       LEDC_CHANNEL_0
#define SERVO_MODE          LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ       50
#define SERVO_RES           LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX      ((1 << 14) - 1)
#define SERVO_PERIOD_US     20000

#define SERVO_MIN_US        500
#define SERVO_MAX_US        2400
#define SERVO_MIN_ANGLE_DEG  0
#define SERVO_MAX_ANGLE_DEG  180

#define POT_MIN_ANGLE_DEG    0
#define POT_MAX_ANGLE_DEG    270

#define DUTY_MIN            ((SERVO_MIN_US * SERVO_DUTY_MAX) / SERVO_PERIOD_US)
#define DUTY_MAX            ((SERVO_MAX_US * SERVO_DUTY_MAX) / SERVO_PERIOD_US)

#define ADC_MAX_RAW         4095

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = SERVO_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RES,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = SERVO_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = DUTY_MIN,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void servo_set_duty(int duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, (uint32_t)duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_MODE, SERVO_CHANNEL));
}

static adc_oneshot_unit_handle_t pot_adc_init(void)
{
    adc_oneshot_unit_handle_t adc_handle;
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = POT_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, POT_ADC_CHANNEL, &channel_config));

    return adc_handle;
}

void app_main(void)
{
    adc_oneshot_unit_handle_t pot_adc = pot_adc_init();
    servo_init();
    ESP_LOGI(TAG, "Керування серво з потенціометра на GPIO1");

    int last_logged_angle = -1;
    int log_counter = 0;

    while (1) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(pot_adc, POT_ADC_CHANNEL, &raw));

        const int pot_angle = POT_MIN_ANGLE_DEG +
                              (raw * (POT_MAX_ANGLE_DEG - POT_MIN_ANGLE_DEG)) / ADC_MAX_RAW;

        int servo_angle = pot_angle;
        if (servo_angle < SERVO_MIN_ANGLE_DEG) {
            servo_angle = SERVO_MIN_ANGLE_DEG;
        } else if (servo_angle > SERVO_MAX_ANGLE_DEG) {
            servo_angle = SERVO_MAX_ANGLE_DEG;
        }

        const int duty = DUTY_MIN +
                         ((servo_angle - SERVO_MIN_ANGLE_DEG) * (DUTY_MAX - DUTY_MIN)) /
                         (SERVO_MAX_ANGLE_DEG - SERVO_MIN_ANGLE_DEG);
        servo_set_duty(duty);

        if (++log_counter >= 10) {
            log_counter = 0;
            const int angle_from_left = servo_angle - SERVO_MIN_ANGLE_DEG;
            if (angle_from_left != last_logged_angle) {
                ESP_LOGI(TAG, "Відхилення від крайнього лівого положення: %d град",
                         angle_from_left);
                last_logged_angle = angle_from_left;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}