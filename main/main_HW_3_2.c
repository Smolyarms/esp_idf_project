#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHANNEL     ADC_CHANNEL_6   // GPIO 6
#define MAX_RAW_VALUE   4095
#define MAX_VOLTAGE_MV  3300

void app_main(void)
{
    adc_oneshot_unit_handle_t adc1_handle;               
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    adc_oneshot_new_unit(&init_config, &adc1_handle);    

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &chan_config);

    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    bool cali_enabled = (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK);

    // Таблиця 
    printf("RAW   U_manual(mV)   U_cali(mV)   Error(%%)\n");
    printf("------------------------------------------\n\n");

    int last_printed_raw = -100;

    while (1) {
        int adc_raw = 0;

        if (adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw) == ESP_OK) {
            
            if (abs(adc_raw - last_printed_raw) > 5) {
                last_printed_raw = adc_raw;

                float voltage_formula = ((float)adc_raw / (float)MAX_RAW_VALUE) * (float)MAX_VOLTAGE_MV;

                int voltage_cali = 0;
                if (cali_enabled) {
                    adc_cali_raw_to_voltage(cali_handle, adc_raw, &voltage_cali);
                } else {
                    voltage_cali = (int)voltage_formula; 
                }

                float error_percent = 0.0;
                if (voltage_cali > 0) {
                    error_percent = (fabsf(voltage_formula - (float)voltage_cali) / (float)voltage_cali) * 100.0;
                } else if (voltage_cali == 0 && voltage_formula > 0) {
                    error_percent = 100.0; // Захист від ділення на нуль
                }

                // Виведення даних
                // printf("%4d      %7.1f         %4d        %4.2f\n\n", 
                //        adc_raw, voltage_formula, voltage_cali, error_percent);

                //Teleplot графік
                printf(">RAW:%d\n", adc_raw);
                printf(">U_manual:%.1f\n", voltage_formula);
                printf(">U_cali:%d\n", voltage_cali);
                printf(">Error:%.2f\n", error_percent);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}