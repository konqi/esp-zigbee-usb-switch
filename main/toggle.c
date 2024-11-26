#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "toggle.h"

static const char *TAG = "TOGGLE_DRIVER";

/**
 * @brief init GPIO configuration as well as isr
 *
 * @param button_num            number of button pair.
 */
esp_err_t toggle_driver_gpio_init(uint8_t gpio_pin)
{
    uint64_t pin_bit_mask = 0;
    pin_bit_mask |= (1ULL << gpio_pin);

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = pin_bit_mask;
    io_conf.mode = GPIO_MODE_OUTPUT_OD;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Unable to configure pin %i", gpio_pin);
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio_pin, GPIO_OUTPUT_LEVEL_OFF), TAG, "Unable to set gpio value for pin %i", gpio_pin);

    return ESP_OK;
}

static void oneshot_timer_callback(void *arg)
{
    uint8_t gpio_pin = (uint32_t)arg;
    ESP_LOGI(TAG, "Oneshot timer callback");
    ESP_ERROR_CHECK(gpio_set_level(gpio_pin, GPIO_OUTPUT_LEVEL_OFF));
}

static esp_err_t schedule_off(uint8_t gpio_pin, uint16_t durationMs)
{
    uint32_t gpio_pin32 = gpio_pin;
    const esp_timer_create_args_t oneshot_timer_args = {
        .callback = &oneshot_timer_callback,
        /* argument specified here will be passed to timer callback function */
        .arg = (void *)gpio_pin32,
        .name = "one-shot"};
    esp_timer_handle_t oneshot_timer;
    // TODO: check esp_timer_is_active, then esp_timer_restart
    // might need to put the timer for each GPIO into a structure
    ESP_RETURN_ON_ERROR(esp_timer_create(&oneshot_timer_args, &oneshot_timer), TAG, "Unable to create timer");

    /* Start the timers */
    return esp_timer_start_once(oneshot_timer, durationMs * 1000);
}

esp_err_t toggle_gpio(uint8_t gpio_pin, uint16_t durationMs)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio_pin, GPIO_OUTPUT_LEVEL_ON), TAG, "Unable to set gpio value for pin %i", gpio_pin);
    ESP_RETURN_ON_ERROR(schedule_off(gpio_pin, durationMs), TAG, "Cannot schedule gpio toggle reversal");

    return ESP_OK;
}
