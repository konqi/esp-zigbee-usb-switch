/*
 * This code is based on the espressif zigbee home automation (ha) light on/off example
 * The LED ON/OFF functionality is simply left in here, because it's a great way to
 * determine if the zigbee communication works at all.
 */
#include "zigbee_usb_switch.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile light (End Device) source code.
#endif

static const char *TAG = "ESP_ZB_ON_OFF_LIGHT";

// Note: On my board GPIO_NUM_21 is soldered as input to the external button.
// That experiment didn't work out but I'm too lazy to desolder the IC...
// You don't need to configure GPIO_NUM_21 if it is not connected.
const int gpio_inputs[] = {GPIO_NUM_9, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21};

// this only works as long as gpio_inputs is const
#define INPUT_GPIO_LEN sizeof(gpio_inputs) / sizeof(int)

typedef enum usb_switch_state_enum
{
    CH_1 = 0,
    CH_2 = 1,
    UNKNOWN = 2
} usb_switch_state_t;

static usb_switch_state_t usb_switch_state = UNKNOWN;
static uint8_t reset_counter = 0;

void reset_by_toggle(int gpio_num, gpio_input_state_t value)
{
    if (gpio_num == GPIO_NUM_18 || gpio_num == GPIO_NUM_19)
    {
        if (value == ON_LONG)
        {
            // reset counter
            reset_counter = 0;
        }
        else if (value == ON)
        {
            // increase reset counter
            ++reset_counter;

            if (reset_counter > 10)
            {
                ESP_LOGI(TAG, "Resetting device.");
                esp_zb_factory_reset();
                reset_counter = 0;
            }
        }
    }
}

static void debounced_input_handler(int gpio_num, gpio_input_state_t value)
{
    ESP_LOGI(TAG, "GPIO %i is now %i", gpio_num, value);
    // the following line can be enabled, this effectively creates a flip-flop for the inputs (good for testing connections)
    // toggle_gpio(GPIO_OUTPUT_IO_TOGGLE_SWITCH, 200);

    reset_by_toggle(gpio_num, value);

    if (value == ON)
    {
        usb_switch_state_t new_value = UNKNOWN;
        switch (gpio_num)
        {
        case GPIO_NUM_18:
            new_value = CH_2;
            break;
        case GPIO_NUM_19:
            new_value = CH_1;
            break;
        case GPIO_NUM_9:
            ESP_LOGI(TAG, "Resetting device.");
            esp_zb_factory_reset();
            break;
        default:
            ESP_LOGW(TAG, "Pressing GPIO %i isn't defined.", gpio_num);
            break;
        }

        if (new_value != UNKNOWN)
        {
            usb_switch_state = new_value;
            ESP_LOGI(TAG, "USB Switch state is now %i", new_value);

            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(HA_ESP_LIGHT_ENDPOINT,
                                                                      ESP_ZB_ZCL_CLUSTER_ID_MULTI_VALUE,
                                                                      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                                                      ESP_ZB_ZCL_ATTR_MULTI_VALUE_PRESENT_VALUE_ID,
                                                                      &new_value,
                                                                      false);
            esp_zb_lock_release();
            ESP_LOGI(TAG, "Multistate value updated to %i, status %i", new_value, status);

            // manual report of attribute (not necessary)
            // esp_zb_zcl_report_attr_cmd_t report_attr_cmd = {
            //     .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
            //     .clusterID = ESP_ZB_ZCL_CLUSTER_ID_MULTI_VALUE,
            //     .attributeID = ESP_ZB_ZCL_ATTR_MULTI_VALUE_PRESENT_VALUE_ID,
            //     .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV, // ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
            //     .zcl_basic_cmd.src_endpoint = HA_ESP_LIGHT_ENDPOINT,
            // };

            // esp_zb_lock_acquire(portMAX_DELAY);
            // ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd));
            // esp_zb_lock_release();
            // ESP_LOGI(TAG, "Multistate value reported");
        }
    }
}

static esp_err_t deferred_driver_init(void)
{
    light_driver_init(LIGHT_DEFAULT_OFF);
    // ESP_RETURN_ON_FALSE(switch_driver_init(button_func_pair, PAIR_SIZE(button_func_pair), zb_buttons_handler), ESP_FAIL, TAG,
    //                     "Failed to initialize switch driver");
    ESP_LOGI(TAG, "Configuring %i pins for input", INPUT_GPIO_LEN);
    ESP_RETURN_ON_ERROR(gpio_debounce_input_init(gpio_inputs, INPUT_GPIO_LEN, debounced_input_handler), TAG, "Failed to initialize debounced inputs.");
    ESP_RETURN_ON_ERROR(toggle_driver_gpio_init(GPIO_OUTPUT_IO_TOGGLE_SWITCH), TAG,
                        "Failed to initialize toggle driver");
    return ESP_OK;
}

static esp_err_t bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_ERROR(esp_zb_bdb_start_top_level_commissioning(mode_mask), TAG, "Failed to start Zigbee commissioning");

    return ESP_OK;
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK)
        {
            ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new())
            {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
            else
            {
                ESP_LOGI(TAG, "Device rebooted");
            }
        }
        else
        {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK)
        {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());

            // read values once after zigbee initialization
            gpio_read_once();
        }
        else
        {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    bool light_state = 0;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    if (message->info.dst_endpoint == HA_ESP_LIGHT_ENDPOINT)
    {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_MULTI_VALUE && message->attribute.id == ESP_ZB_ZCL_ATTR_MULTI_VALUE_PRESENT_VALUE_ID)
        {
            // determine value
            uint16_t desired_state = *(uint16_t *)message->attribute.data.value;
            ESP_LOGI(TAG, "Received state change to value %i", desired_state);
            // check if toggle is required
            if (desired_state != usb_switch_state)
            {
                ESP_LOGI(TAG, "Switch not in desired state. Toggeling");
                toggle_gpio(GPIO_OUTPUT_IO_TOGGLE_SWITCH, 200);
            }
        }
        else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF)
        {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL)
            {
                light_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : light_state;
                ESP_LOGI(TAG, "Light sets to %s", light_state ? "On" : "Off");
                light_driver_set_power(light_state);
            }
        }
    }

    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id)
    {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    case ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID:
        esp_zb_zcl_cmd_default_resp_message_t *msg = (esp_zb_zcl_cmd_default_resp_message_t *)message;
        // cmd 10 = report attributes
        ESP_LOGI(TAG, "Received reponse (0x%02x) to cmd(0x%02x) endpoint(%i) cluster(%i) status(0x%02x)",
                 ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID,
                 msg->resp_to_cmd,
                 msg->info.dst_endpoint,
                 msg->info.cluster,
                 msg->info.status);
        break;
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%02x) callback", callback_id);
        break;
    }
    return ret;
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    // create empty endpoint list
    esp_zb_ep_list_t *endpoint_list = esp_zb_ep_list_create();

    // create cluster for on_off_light configuration
    esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
    esp_zb_cluster_list_t *cluster_list = esp_zb_on_off_light_clusters_create(&light_cfg);

    esp_zb_multistate_value_cluster_cfg_t multistate_config = {
        .number_of_states = 2,
        .out_of_service = false,
        .present_value = 0,
        .status_flags = 0,
    };
    esp_zb_attribute_list_t *multistate_cluster = esp_zb_multistate_value_cluster_create(&multistate_config);
    esp_zb_attribute_list_t *attr = multistate_cluster;
    // enable reporting of present value (see https://github.com/espressif/esp-zigbee-sdk/issues/372#issuecomment-2213952627)
    ESP_LOGV(TAG, "0: attributeId(0x%02x) access(0x%02x)", multistate_cluster->attribute.id, multistate_cluster->attribute.access);

    while (attr)
    {
        if (attr->attribute.id == ESP_ZB_ZCL_ATTR_MULTI_VALUE_PRESENT_VALUE_ID)
        {
            ESP_LOGV(TAG, "1: attributeId(0x%02x) access(0x%02x)", attr->attribute.id, attr->attribute.access);
            // by default the attribute is writeable, use only ESP_ZB_ZCL_ATTR_ACCESS_REPORTING | ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY to disable write
            attr->attribute.access = attr->attribute.access | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
            ESP_LOGV(TAG, "2: attributeId(0x%02x) access(0x%02x)", attr->attribute.id, attr->attribute.access);
            break;
        }
        attr = attr->next;
    }

    // TODO: might be necessary to add the cluster to a different endpoint
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_cluster_list_add_multistate_value_cluster(cluster_list, multistate_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // add endpoint with clusters to list
    esp_zb_endpoint_config_t ep_config = {
        .endpoint = HA_ESP_LIGHT_ENDPOINT,
        .app_device_id = ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_version = 1,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_ep_list_add_ep(endpoint_list, cluster_list, ep_config));

    // add zcl basic cluster
    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = ESP_MANUFACTURER_NAME,
        .model_identifier = ESP_MODEL_IDENTIFIER,
    };
    esp_zcl_utility_add_ep_basic_manufacturer_info(endpoint_list, HA_ESP_LIGHT_ENDPOINT, &info);

    esp_zb_device_register(endpoint_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
