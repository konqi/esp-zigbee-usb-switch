/*
 * Main entry file for zigbee-usb-switch project
 * based on the espressif zigbee home automation (ha) light on/off example
 */

#include "esp_zigbee_core.h"
#include "light_driver.h"
#include "gpio_input.h"
#include "toggle.h"
#include "zcl_utility.h"

/* Zigbee configuration */
#define INSTALLCODE_POLICY_ENABLE false /* enable the install code policy for security */
#define ED_AGING_TIMEOUT ESP_ZB_ED_AGING_TIMEOUT_64MIN
#define ED_KEEP_ALIVE 3000                                               /* 3000 millisecond */
#define HA_ESP_LIGHT_ENDPOINT 10                                         /* esp light bulb device endpoint, used to process light controlling commands */
#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK /* Zigbee primary channel mask use in the example */

/* GPIO Inputs configuration */
/* user should configure which I/O port as toggle switch input, default is GPIO9 */
#define GPIO_INPUT_IO_TOGGLE_SWITCH_1 GPIO_NUM_18
#define GPIO_INPUT_IO_TOGGLE_SWITCH_2 GPIO_NUM_19
#define GPIO_OUTPUT_IO_TOGGLE_SWITCH GPIO_NUM_20

/* Basic manufacturer information */
#define ESP_MANUFACTURER_NAME "\x05" \
                              "KONQI" /* Customized manufacturer name */
#define ESP_MODEL_IDENTIFIER "\x11" \
                             "zigbee-usb-switch" /* Customized model identifier */

#define ESP_ZB_ZED_CONFIG()                               \
    {                                                     \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,             \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
        .nwk_cfg.zed_cfg = {                              \
            .ed_timeout = ED_AGING_TIMEOUT,               \
            .keep_alive = ED_KEEP_ALIVE,                  \
        },                                                \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()       \
    {                                       \
        .radio_mode = ZB_RADIO_MODE_NATIVE, \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                          \
    {                                                         \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, \
    }
