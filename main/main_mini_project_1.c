#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

#define ENC_A_PIN               5
#define ENC_B_PIN               6
#define ENC_SW_PIN              7
#define BUZZER_PIN              2
#define SERVO_PIN               1

#define SYSTEM_TICK_MS          10      
static const char *TAG = "safe_safe";

#define NOTE_E4  330
#define NOTE_A4  440
#define NOTE_D5  587
#define NOTE_C5  523
#define NOTE_A3  220
#define NOTE_C4  262
#define NOTE_B3  247
#define NOTE_B4  494
#define NOTE_E5  659
#define REST     0

// Мелодія сигналізації
const int melody[] = {
  NOTE_A4, NOTE_E5, NOTE_A4, NOTE_E5,
  NOTE_B4, NOTE_E5, NOTE_B4, NOTE_E5,
  NOTE_C5, NOTE_E5, NOTE_C5, NOTE_E5,
  NOTE_D5, NOTE_E5, NOTE_D5, NOTE_E5
};

// Тривалість кожної ноти в мілісекундах
const int durations[] = {
  600, 1200, 600, 1200,
  600, 1200, 600, 1200,
  600, 1200, 600, 1200,
  600, 1200, 600, 1200
};

#define MELODY_LENGTH (sizeof(melody) / sizeof(melody[0]))

// Мелодія успіху
const int melody_f1[] = {
    NOTE_A3, NOTE_A3, NOTE_A3,
    NOTE_E4,
    REST,
    NOTE_E4, NOTE_E4,
    NOTE_C4,
    NOTE_B3
};

const int durations_f1[] = {
    300, 300, 200,
    400,
    350,
    350, 200,
    350, 350
};

#define MELODY_F1_LENGTH (sizeof(melody_f1) / sizeof(melody_f1[0]))

#define CODE_LENGTH             4
static int entered_code[CODE_LENGTH] = {0, 0, 0, 0}; 
static int code_index = 0;                             
static int current_digit = 0;              

#define MAX_ATTEMPTS            3
static const int SECRET_CODE[CODE_LENGTH] = {0, 2, 0, 3}; // Правильний код для перевірки
static int remaining_attempts = MAX_ATTEMPTS;          // Лічильник спроб (3)
static bool safe_blocked = false;

// Ініціалізація звуку та сервопривода
static void peripheral_init(void)
{
    // 1. Конфігурація таймера для зумера
    ledc_timer_config_t buzzer_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 4000,  
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&buzzer_timer));

    ledc_channel_config_t buzzer_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BUZZER_PIN,
        .duty           = 0, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&buzzer_channel));

    // 2. Конфігурація таймера для сервопривода
    ledc_timer_config_t servo_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        .duty_resolution  = LEDC_TIMER_14_BIT,
        .freq_hz          = 50,  
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&servo_timer));

    ledc_channel_config_t servo_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_1,
        .timer_sel      = LEDC_TIMER_1,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SERVO_PIN,
        .duty           = 0, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&servo_channel));
}

// Керування сервоприводом
static void set_servo_angle(int angle)
{
    uint32_t duty = 410 + ((angle * 1556) / 180);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

// Функція програвання звуку певної частоти та тривалості
static void buzzer_tone(int frequency, int duration_ms)
{
    if (frequency > 0) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequency);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512); // 50% заповнення ШІМ
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    } else {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0); // Вимкнути звук
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Пауза між нотами, щоб звук не зливався
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(30)); 
}

// Програвання мелодії успіху
static void play_success_melody(void)
{
    for (int i = 0; i < MELODY_F1_LENGTH; i++) {
        buzzer_tone(melody_f1[i], durations_f1[i]);
    }
}

// Запуск сирени "Твої очі ліхтарики"
static void play_alarm_melody(void)
{
    ESP_LOGE(TAG, "!!! LOOP !!! Вже не вибратись");
    
    // Граємо мелодію по колу, поки пристрій увімкнений
    while (1) {
        for (int i = 0; i < MELODY_LENGTH; i++) {
            buzzer_tone(melody[i], durations[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 секунда паузи між повторами пісні
    }
}

// Перевірка введеного коду
static void check_pin_code(void)
{
    bool match = true;
    for (int i = 0; i < CODE_LENGTH; i++) {
        if (entered_code[i] != SECRET_CODE[i]) {
            match = false;
            break;
        }
    }

    if (match) {
        ESP_LOGW(TAG, "ПРАВИЛЬНИЙ КОД! Сейф відкрито! Спроби відновлено.");
        remaining_attempts = MAX_ATTEMPTS;
        // Відкриваємо на 90 градусів
        set_servo_angle(90);

        // Граємо мелодію успіху
        play_success_melody();

        // Чекаємо 3 секунди
        vTaskDelay(pdMS_TO_TICKS(3000));

        // Автоматично закриваємо на 0 градусів
        set_servo_angle(0);
    } else {
        remaining_attempts--;
        ESP_LOGE(TAG, "НЕВІРНИЙ КОД! Залишилось спроб: %d", remaining_attempts);
        
        if (remaining_attempts <= 0) {
            safe_blocked = true;
        }
    }
}


static void hardware_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENC_A_PIN) | (1ULL << ENC_B_PIN) | (1ULL << ENC_SW_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE 
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    peripheral_init();
    
    // Встановлюємо сервопривід у закрите положення на старті
    set_servo_angle(0);
}

void app_main(void)
{
    hardware_init();
    
    int last_a_state = gpio_get_level(ENC_A_PIN); 
    int last_b_state = gpio_get_level(ENC_B_PIN);
    
    int current_direction = 0;  
    int last_direction = 0;     
    uint64_t last_reverse_time = 0;
    bool step_authorized = false; 

    int sw_stable_ticks = 0;
    bool button_pressed = false;

    printf("%d\n", current_digit);

    while (1) {
        // Якщо сейф заблоковано — вмикаємо сирену
        if (safe_blocked) {
            play_alarm_melody();
        }

        int current_a_state = gpio_get_level(ENC_A_PIN);
        int current_b_state = gpio_get_level(ENC_B_PIN);

        // 1. ОБРОБКА ОБЕРТАННЯ ТА НАПРЯМКУ
        if (!step_authorized && (current_a_state == 0 || current_b_state == 0)) {
            step_authorized = true; 

            if (current_a_state == 0 && last_a_state == 1) {
                current_direction = 1;
            } else if (current_b_state == 0 && last_b_state == 1) {
                current_direction = -1;
            }
        }

        if (step_authorized && current_a_state == 1 && current_b_state == 1) {
            step_authorized = false; 
            bool is_reversed = false;

            if (last_direction != 0 && current_direction != last_direction) {
                uint64_t now = esp_timer_get_time();
                if (now - last_reverse_time > 150000) {
                    last_reverse_time = now;
                    is_reversed = true;
                }
            }

            if (is_reversed) {
                printf(">%d\n", current_digit);
                
                entered_code[code_index] = current_digit;
                code_index++;
                current_digit = 0; 

                if (code_index >= CODE_LENGTH) {
                    ESP_LOGI(TAG, "ПІН-код повністю зібрано: [%d, %d, %d, %d]", 
                             entered_code[0], entered_code[1], entered_code[2], entered_code[3]);
                    
                    // Перевіряємо введений пін
                    check_pin_code();
                    
                    code_index = 0;
                    last_direction = 0; 
                    if (!safe_blocked) {
                        printf("%d\n", current_digit); 
                    }
                } else {
                    printf("%d\n", current_digit); 
                }
            } else {
                current_digit++;
                if (current_digit > 9) {
                    current_digit = 0;
                }
                printf("%d\n", current_digit);
            }

            last_direction = current_direction;
        }

        last_a_state = current_a_state;
        last_b_state = current_b_state;


        // 2. ОБРОБКА КНОПКИ (RESET ЯК ХИБНА СПРОБА)
        int sw_state = gpio_get_level(ENC_SW_PIN);

        if (sw_state == 0) { 
            sw_stable_ticks++;
            if (sw_stable_ticks >= 3 && !button_pressed) {
                button_pressed = true;
                
                // Скидаємо прогрес введення поточного коду
                code_index = 0;
                current_digit = 0;
                last_direction = 0; 
                for (int i = 0; i < CODE_LENGTH; i++) {
                    entered_code[i] = 0;
                }

                // Мінус одна спробу введення
                remaining_attempts--;
                ESP_LOGW(TAG, "Введення ПІН-коду скинуто кнопкою RESET! Спроба зарахована як хибна. Залишилось: %d", remaining_attempts);
                
                if (remaining_attempts <= 0) {
                    safe_blocked = true;
                } else {
                    printf("%d\n", current_digit); 
                }
            }
        } else {
            sw_stable_ticks = 0;
            if (button_pressed) {
                button_pressed = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SYSTEM_TICK_MS));
    }
}