#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define BUTTON_1    1
#define BUTTON_2    2
#define BUTTON_3    4
#define BUTTON_4    5

// BUTTON_1
volatile uint32_t counter_btn1 = 0;

// BUTTON_2
volatile uint64_t last_isr_time_btn2 = 0;
volatile bool btn2_pressed_flag = false;
uint32_t counter_btn2 = 0;

// BUTTON_3
volatile bool btn3_event_flag = false; 
uint32_t counter_btn3 = 0;

// BUTTON_4
typedef enum { 
    IDLE = 0, 
    DEBOUNCE_PRESS, 
    HELD, 
    DEBOUNCE_RELEASE 
} btn4_state_t;

uint32_t counter_btn4 = 0;
btn4_state_t btn4_state = IDLE;
uint8_t btn4_ticks = 0;

// ISR BUTTON_1
static void IRAM_ATTR button_1_isr_handler(void* arg)
{
    counter_btn1++;
}

// ISR BUTTON_2
static void IRAM_ATTR button_2_isr_handler(void* arg)
{
    last_isr_time_btn2 = esp_timer_get_time();
    btn2_pressed_flag = true;
}

// ISR BUTTON_3
static void IRAM_ATTR button_3_isr_handler(void* arg)
{
    btn3_event_flag = true;
}

void app_main(void)
{
    // Setup BUTTON_1
    gpio_reset_pin(BUTTON_1);
    gpio_set_direction(BUTTON_1, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_1, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_1, GPIO_INTR_NEGEDGE ); // GPIO_INTR_ANYEDGE якщо потрібно і на відпускання

    // Setup BUTTON_2
    gpio_reset_pin(BUTTON_2);
    gpio_set_direction(BUTTON_2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_2, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_2, GPIO_INTR_NEGEDGE);

    // Setup BUTTON_3
    gpio_reset_pin(BUTTON_3);
    gpio_set_direction(BUTTON_3, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_3, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_3, GPIO_INTR_NEGEDGE);

    // Setup BUTTON_4
    gpio_reset_pin(BUTTON_4);
    gpio_set_direction(BUTTON_4, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_4, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_4, GPIO_INTR_NEGEDGE);

    // BUTTON_1
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(BUTTON_1, button_1_isr_handler, NULL);

    // BUTTON_2
    gpio_isr_handler_add(BUTTON_2, button_2_isr_handler, NULL);

    // BUTTON_3
    gpio_isr_handler_add(BUTTON_3, button_3_isr_handler, NULL);

    // BUTTON_1
    uint32_t last_printed_btn1 = 0;

    // BUTTON_2
    uint64_t last_valid_press_time_btn2 = 0;
    const uint64_t debounce_delay = 50000; // 50 мс
    
    // BUTTON_3
    bool btn3_is_processing = false;

    while (1) {
        // BUTTON_1
        if (counter_btn1 != last_printed_btn1) {
            last_printed_btn1 = counter_btn1;
            printf("BUTTON_1: %lu\n", last_printed_btn1);
        }

        // BUTTON_2
        if (btn2_pressed_flag) {
            btn2_pressed_flag = false;

            if ((last_isr_time_btn2 - last_valid_press_time_btn2) >= debounce_delay) {
                counter_btn2++;
                printf("BUTTON_2: %lu\n", counter_btn2);
                
                last_valid_press_time_btn2 = last_isr_time_btn2;
            }
        }

        // BUTTON_3
        if (btn3_event_flag && !btn3_is_processing) {
            btn3_event_flag = false;
            btn3_is_processing = true;

            vTaskDelay(pdMS_TO_TICKS(25));

            if (gpio_get_level(BUTTON_3) == 0) {
                counter_btn3++;
                printf("BUTTON_3: %lu\n", counter_btn3);

                while (gpio_get_level(BUTTON_3) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }

            btn3_event_flag = false;
            btn3_is_processing = false; 
        }

        // BUTTON_4
        bool pressed4 = (gpio_get_level(BUTTON_4) == 0);
        
        switch (btn4_state) {
            case IDLE:
                if (pressed4) {
                    btn4_state = DEBOUNCE_PRESS;
                    btn4_ticks = 0;
                }
                break;

            case DEBOUNCE_PRESS:
                if (pressed4) {
                    btn4_ticks++;
                    if (btn4_ticks >= 5) { // 50 мс
                        btn4_state = HELD;
                        counter_btn4++;
                        printf("BUTTON_4: %lu\n", counter_btn4);
                    }
                } else {
                    btn4_state = IDLE;
                }
                break;

            case HELD:
                if (!pressed4) {
                    btn4_state = DEBOUNCE_RELEASE;
                    btn4_ticks = 0;
                }
                break;

            case DEBOUNCE_RELEASE:
                if (!pressed4) {
                    btn4_ticks++;
                    if (btn4_ticks >= 5) { // 50 мс
                        btn4_state = IDLE;
                    }
                } else {
                    btn4_state = HELD;
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}