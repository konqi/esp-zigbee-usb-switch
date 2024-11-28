#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CONFIG_FREERTOS_HZ 100 /* see sdkconfig, default is 100 */

#define GPIO_OUTPUT_LEVEL_ON 0
#define GPIO_OUTPUT_LEVEL_OFF 1

    esp_err_t toggle_driver_gpio_init(uint8_t gpio_pin);
    esp_err_t toggle_gpio(uint8_t gpio_pin, uint16_t durationTicksMs);

#ifdef __cplusplus
} // extern "C"
#endif