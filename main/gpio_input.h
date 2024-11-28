#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CONFIG_FREERTOS_HZ 100     /* see sdkconfig, default is 100 */
#define CONFIG_LOG_MAXIMUM_LEVEL 3 /* maximum log verbosity, default is 3 */

#define ESP_INTR_FLAG_DEFAULT 0

#define COUNT_ARRAY_ELEMENTS(ARRAY_TYPE) (sizeof(ARRAY_TYPE) / sizeof(ARRAY_TYPE[0]))
#define DEBOUNCE_TICKS 25
    typedef void (*debounced_input_callback)(int gpio_num, int value);

    typedef struct gpio_input_debounce_config_t
    {
        gpio_num_t gpio_num;
        bool last_state;
        uint32_t interrupt_cnt;
        uint32_t debounce_timeout;
    } gpio_input_debounce_config_t;

    esp_err_t gpio_debounce_input_init(int gpio_debounce_inputs[], int number_of_inputs, debounced_input_callback cb);
#ifdef __cplusplus
} // extern "C"
#endif