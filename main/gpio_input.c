#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_input.h"

static const char *TAG = "GPIO_DEBOUNCED_INPUT";

static uint8_t gpio_count = 0;
static gpio_input_debounce_config_t *internal_config = NULL; /* pointer to array */
static debounced_input_callback callback = NULL;

TaskHandle_t loop_task_handle = NULL;

static void IRAM_ATTR gpio_interrupt_handler(void *arg)
{
    gpio_input_debounce_config_t *input_debounce_helper = arg;
    input_debounce_helper->interrupt_cnt++;
    input_debounce_helper->last_state = gpio_get_level(input_debounce_helper->gpio_num);
    input_debounce_helper->debounce_timeout = xTaskGetTickCountFromISR();
}

static const uint16_t debounce_loop_delay_ticks = 50 / portTICK_PERIOD_MS;
static void debounce_gpio_input_loop_task()
{
    for (;;)
    {
        for (int i = 0; i < gpio_count; ++i)
        {
            gpio_input_debounce_config_t *gpio_helper = &(internal_config[i]);
            int gpio_num = gpio_helper->gpio_num;
            gpio_input_state_t gpio_state = gpio_get_level(gpio_num);

            if ( // interrupt triggerd
                gpio_helper->interrupt_cnt != 0 &&
                // the input is still in the same state
                gpio_state == gpio_helper->last_state &&
                // check debounce time has expired
                (xTaskGetTickCount() - gpio_helper->debounce_timeout > DEBOUNCE_TICKS))
            {
                // gpio_state is stable
                ESP_LOGD(TAG, "gpio %i apprears to be stable in state %i", gpio_num, gpio_state);

                // trigger callback
                callback(gpio_num, gpio_state);

                // reset
                gpio_helper->interrupt_cnt = 0;
            }

            if ( // no other interrupt was triggered (button still pressed)
                gpio_helper->interrupt_cnt == 0 &&
                // long enough for a long press
                (xTaskGetTickCount() - gpio_helper->debounce_timeout > LONG_PRESS_TICKS) &&
                // only trigger once
                (xTaskGetTickCount() - gpio_helper->debounce_timeout < LONG_PRESS_TICKS + debounce_loop_delay_ticks))
            {
                ESP_LOGV(TAG, "long stable state on gpio %i in state %i", gpio_num, gpio_state);

                // trigger callback
                callback(gpio_num, gpio_state + 2);
            }
        }

        vTaskDelay(debounce_loop_delay_ticks);
    }
}

static esp_err_t configure_gpio_inputs(int gpio_nums[])
{
    gpio_config_t io_conf = {};
    uint64_t pin_bit_mask = 0;

    /* construct bitmask to configure potentially multiple gpios */
    for (int i = 0; i < gpio_count; ++i)
    {
        pin_bit_mask |= (1ULL << gpio_nums[i]);
    }
    /* interrupt on all edges */
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = pin_bit_mask;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;

    /* configure GPIO with the given settings */
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Unable to configure GPIOs.");

    return ESP_OK;
}

static void clean_up()
{
    if (gpio_count > 0)
    {
        // free previously allocated memory
        free(internal_config);
    }
    if (loop_task_handle != NULL)
    {
        // remove previously started task
        vTaskDelete(loop_task_handle);
    }
}

esp_err_t gpio_debounce_input_init(int gpio_debounce_inputs[], int number_of_inputs, debounced_input_callback cb)
{
    clean_up();

    callback = cb;
    gpio_count = number_of_inputs;
    // allocate memory for configuration
    ESP_LOGI(TAG, "Allocating %i bytes of memory (size: %i, times: %i)", sizeof(gpio_input_debounce_config_t) * gpio_count, sizeof(gpio_input_debounce_config_t), gpio_count);
    internal_config = malloc(sizeof(gpio_input_debounce_config_t) * gpio_count);

    ESP_RETURN_ON_ERROR(configure_gpio_inputs(gpio_debounce_inputs), TAG, "Cannot configure GPIOs");

    xTaskCreate(debounce_gpio_input_loop_task, "gpio_input_debounce", 4096, NULL, 10, &loop_task_handle);

    ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT), TAG, "Cannot install ISR service.");

    for (int i = 0; i < gpio_count; i++)
    {
        // Add gpio isr handlers with
        int gpio_num = gpio_debounce_inputs[i];
        internal_config[i].gpio_num = gpio_num;
        internal_config[i].debounce_timeout = 0;
        internal_config[i].last_state = gpio_get_level(gpio_num);
        internal_config[i].interrupt_cnt = 0;

        // trigger callback with initial value
        // if you think about enableing this, take a look at the gpio_read_once() function
        // callback(gpio_num, internal_config[i].last_state);

        ESP_RETURN_ON_ERROR(
            gpio_isr_handler_add(gpio_num, gpio_interrupt_handler, (void *)&(internal_config[i])),
            TAG,
            "Cannot add isr handler for gpio %i", gpio_num);
    }

    return ESP_OK;
}

/**
 * Reads all configured inputs once and triggers a callback on each.
 * The value in the callback is poentially a snapshot that needs debouncing.
 */
void gpio_read_once()
{
    for (int i = 0; i < gpio_count; i++)
    {
        // Add gpio isr handlers with
        int gpio_num = internal_config[i].gpio_num;
        gpio_input_state_t last_state = gpio_get_level(gpio_num);

        // trigger callback with initial value
        callback(gpio_num, last_state);
    }
}
