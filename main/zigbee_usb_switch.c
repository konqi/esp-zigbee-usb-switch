/*
 * This code is based on the espressif zigbee home automation (ha) light on/off example
 * The LED ON/OFF functionality is simply left in here, because it's a great way to
 * determine if the zigbee communication works at all.
 */
#include "zigbee_usb_switch.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_ota.h"
#include "zcl/esp_zigbee_zcl_ota.h"

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile light (End Device) source code.
#endif

static const char *TAG = "ESP_ZB_USB_SWITCH";

#define ZB_INVALID_SHORT_ADDR 0xFFFF
#define ZB_STEERING_RETRY_BASE_DELAY_MS 2000
#define ZB_STEERING_RETRY_MAX_DELAY_MS 60000
#define ZB_STEERING_RETRY_MAX_BACKOFF_STEPS 5

#define BUILD_MONTH_IS_JAN (__DATE__[0] == 'J' && __DATE__[1] == 'a' && __DATE__[2] == 'n')
#define BUILD_MONTH_IS_FEB (__DATE__[0] == 'F')
#define BUILD_MONTH_IS_MAR (__DATE__[0] == 'M' && __DATE__[2] == 'r')
#define BUILD_MONTH_IS_APR (__DATE__[0] == 'A' && __DATE__[1] == 'p')
#define BUILD_MONTH_IS_MAY (__DATE__[0] == 'M' && __DATE__[2] == 'y')
#define BUILD_MONTH_IS_JUN (__DATE__[0] == 'J' && __DATE__[2] == 'n')
#define BUILD_MONTH_IS_JUL (__DATE__[0] == 'J' && __DATE__[2] == 'l')
#define BUILD_MONTH_IS_AUG (__DATE__[0] == 'A' && __DATE__[1] == 'u')
#define BUILD_MONTH_IS_SEP (__DATE__[0] == 'S')
#define BUILD_MONTH_IS_OCT (__DATE__[0] == 'O')
#define BUILD_MONTH_IS_NOV (__DATE__[0] == 'N')
#define BUILD_MONTH_IS_DEC (__DATE__[0] == 'D')

#define BUILD_MONTH_CH0 \
    (BUILD_MONTH_IS_OCT || BUILD_MONTH_IS_NOV || BUILD_MONTH_IS_DEC ? '1' : '0')

#define BUILD_MONTH_CH1                                  \
    (BUILD_MONTH_IS_JAN ? '1' : BUILD_MONTH_IS_FEB ? '2' \
                            : BUILD_MONTH_IS_MAR   ? '3' \
                            : BUILD_MONTH_IS_APR   ? '4' \
                            : BUILD_MONTH_IS_MAY   ? '5' \
                            : BUILD_MONTH_IS_JUN   ? '6' \
                            : BUILD_MONTH_IS_JUL   ? '7' \
                            : BUILD_MONTH_IS_AUG   ? '8' \
                            : BUILD_MONTH_IS_SEP   ? '9' \
                            : BUILD_MONTH_IS_OCT   ? '0' \
                            : BUILD_MONTH_IS_NOV   ? '1' \
                                                   : '2')

#define BUILD_DAY_CH0 (__DATE__[4] == ' ' ? '0' : __DATE__[4])
#define BUILD_DAY_CH1 (__DATE__[5])

#define BUILD_DATE_YYYYMMDD {__DATE__[7], __DATE__[8], __DATE__[9], __DATE__[10], BUILD_MONTH_CH0, BUILD_MONTH_CH1, BUILD_DAY_CH0, BUILD_DAY_CH1, '\0'}

#ifndef ESP_SW_BUILD_ID
#define ESP_SW_BUILD_ID "0.0.0"
#endif

static uint8_t s_steering_retry_attempt = 0;
static const char s_build_date_code[] = BUILD_DATE_YYYYMMDD;
static const char s_sw_build_id[] = ESP_SW_BUILD_ID;

static void confirm_running_ota_image_if_pending(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        ESP_LOGW(TAG, "Could not get running partition to verify OTA state");
        return;
    }

    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_ret = esp_ota_get_state_partition(running, &ota_state);
    if (state_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to query OTA state for running partition (%s)", esp_err_to_name(state_ret));
        return;
    }

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        esp_err_t mark_ret = esp_ota_mark_app_valid_cancel_rollback();
        if (mark_ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Marked running OTA image valid; rollback canceled");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to mark OTA image valid (%s)", esp_err_to_name(mark_ret));
        }
    }
}

static void fill_zcl_string(uint8_t *zb_str, size_t zb_str_size, const char *text)
{
    if (!zb_str || !text || zb_str_size < 2)
    {
        return;
    }

    size_t max_payload = zb_str_size - 1;
    size_t text_len = strlen(text);
    if (text_len > max_payload)
    {
        text_len = max_payload;
    }

    zb_str[0] = (uint8_t)text_len;
    memcpy(&zb_str[1], text, text_len);
}

static void build_basic_cluster_version_strings(uint32_t file_version, uint8_t *sw_build_id, size_t sw_build_id_size,
                                                uint8_t *date_code, size_t date_code_size)
{
    (void)file_version;
    fill_zcl_string(sw_build_id, sw_build_id_size, s_sw_build_id);
    fill_zcl_string(date_code, date_code_size, s_build_date_code);
}

static const char *ota_status_to_str(esp_zb_zcl_ota_upgrade_status_t status)
{
    switch (status)
    {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        return "start";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
        return "apply";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        return "receive";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
        return "finish";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
        return "abort";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
        return "check";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_OK:
        return "ok";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR:
        return "error";
    case ESP_ZB_ZCL_OTA_UPGRADE_IMAGE_STATUS_NORMAL:
        return "normal";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_BUSY:
        return "busy";
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_SERVER_NOT_FOUND:
        return "server_not_found";
    default:
        return "unknown";
    }
}

static void configure_ota_query_interval(void)
{
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_err_t ota_ret = esp_zb_ota_upgrade_client_query_interval_set(HA_ESP_LIGHT_ENDPOINT, ESP_OTA_QUERY_INTERVAL_MIN);
    esp_zb_lock_release();
    if (ota_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to set OTA query interval (status: %s)", esp_err_to_name(ota_ret));
    }
    else
    {
        ESP_LOGI(TAG, "OTA query interval set to %u minutes", (unsigned int)ESP_OTA_QUERY_INTERVAL_MIN);
    }
}

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
    static bool is_inited = false;

    ESP_RETURN_ON_FALSE(!is_inited, ESP_OK, TAG, "Deferred driver already initialized");

    light_driver_init(LIGHT_DEFAULT_OFF);
    // ESP_RETURN_ON_FALSE(switch_driver_init(button_func_pair, PAIR_SIZE(button_func_pair), zb_buttons_handler), ESP_FAIL, TAG,
    //                     "Failed to initialize switch driver");
    ESP_LOGI(TAG, "Configuring %i pins for input", INPUT_GPIO_LEN);
    ESP_RETURN_ON_ERROR(gpio_debounce_input_init(gpio_inputs, INPUT_GPIO_LEN, debounced_input_handler), TAG, "Failed to initialize debounced inputs.");
    ESP_RETURN_ON_ERROR(toggle_driver_gpio_init(GPIO_OUTPUT_IO_TOGGLE_SWITCH), TAG,
                        "Failed to initialize toggle driver");
    is_inited = true;

    return ESP_OK;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    esp_err_t err = esp_zb_bdb_start_top_level_commissioning(mode_mask);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to start Zigbee commissioning (status: %s)", esp_err_to_name(err));
    }
}

static void schedule_steering_retry(const char *reason)
{
    uint32_t backoff_step = s_steering_retry_attempt;
    if (backoff_step > ZB_STEERING_RETRY_MAX_BACKOFF_STEPS)
    {
        backoff_step = ZB_STEERING_RETRY_MAX_BACKOFF_STEPS;
    }

    uint32_t delay_ms = ZB_STEERING_RETRY_BASE_DELAY_MS << backoff_step;
    if (delay_ms > ZB_STEERING_RETRY_MAX_DELAY_MS)
    {
        delay_ms = ZB_STEERING_RETRY_MAX_DELAY_MS;
    }

    ESP_LOGW(TAG, "Scheduling steering retry #%u in %lu ms (%s)",
             (unsigned int)(s_steering_retry_attempt + 1), (unsigned long)delay_ms,
             reason ? reason : "no reason");

    esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                           ESP_ZB_BDB_MODE_NETWORK_STEERING,
                           delay_ms);

    if (s_steering_retry_attempt < 255)
    {
        s_steering_retry_attempt++;
    }
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
                s_steering_retry_attempt = 0;
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
            else
            {
                uint16_t short_addr = esp_zb_get_short_address();
                if (short_addr == ZB_INVALID_SHORT_ADDR)
                {
                    ESP_LOGW(TAG, "Device rebooted but is not joined yet, starting recovery steering");
                    schedule_steering_retry("reboot without network");
                }
                else
                {
                    s_steering_retry_attempt = 0;
                    ESP_LOGI(TAG, "Device rebooted and restored network (PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                             esp_zb_get_pan_id(), esp_zb_get_current_channel(), short_addr);
                    configure_ota_query_interval();
                }
            }
        }
        else
        {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
            schedule_steering_retry("stack initialization failed");
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK)
        {
            s_steering_retry_attempt = 0;
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());

            configure_ota_query_interval();

            // read values once after zigbee initialization
            gpio_read_once();
        }
        else
        {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            schedule_steering_retry("network steering failed");
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
    {
        const esp_zb_zdo_signal_device_annce_params_t *dev_annce = (const esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        if (dev_annce)
        {
            ESP_LOGI(TAG, "New device commissioned or rejoined (short: 0x%04hx)", dev_annce->device_short_addr);
        }
        break;
    }
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
    {
        const uint8_t *duration = (const uint8_t *)esp_zb_app_signal_get_params(p_sg_p);
        if (duration && *duration)
        {
            ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", esp_zb_get_pan_id(), *duration);
        }
        else
        {
            ESP_LOGI(TAG, "Network(0x%04hx) closed, devices joining not allowed", esp_zb_get_pan_id());
        }
        break;
    }
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
    case ESP_ZB_CORE_SCENES_STORE_SCENE_CB_ID:
    {
        const esp_zb_zcl_store_scene_message_t *scene = (const esp_zb_zcl_store_scene_message_t *)message;
        if (scene->info.status != ESP_ZB_ZCL_STATUS_SUCCESS)
        {
            ESP_LOGW(TAG, "Scenes store callback status not successful (0x%02x)", scene->info.status);
            break;
        }

        uint16_t scene_state = (uint16_t)usb_switch_state;
        uint8_t ext_value[sizeof(uint16_t)] = {
            (uint8_t)(scene_state & 0xFF),
            (uint8_t)((scene_state >> 8) & 0xFF),
        };
        esp_zb_zcl_scenes_extension_field_t ext_field = {
            .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_MULTI_VALUE,
            .length = sizeof(uint16_t),
            .extension_field_attribute_value_list = ext_value,
            .next = NULL,
        };

        esp_err_t scene_ret = esp_zb_zcl_scenes_table_store(HA_ESP_LIGHT_ENDPOINT,
                                                            scene->group_id,
                                                            scene->scene_id,
                                                            0,
                                                            &ext_field);
        ESP_LOGI(TAG, "Stored scene %u/%u with multi-value=%u (%s)",
                 scene->group_id,
                 scene->scene_id,
                 scene_state,
                 scene_ret == ESP_OK ? "ok" : esp_err_to_name(scene_ret));
        break;
    }
    case ESP_ZB_CORE_SCENES_RECALL_SCENE_CB_ID:
    {
        const esp_zb_zcl_recall_scene_message_t *scene = (const esp_zb_zcl_recall_scene_message_t *)message;
        if (scene->info.status != ESP_ZB_ZCL_STATUS_SUCCESS)
        {
            ESP_LOGW(TAG, "Scenes recall callback status not successful (0x%02x)", scene->info.status);
            break;
        }

        const esp_zb_zcl_scenes_extension_field_t *field = scene->field_set;
        while (field)
        {
            if (field->cluster_id == ESP_ZB_ZCL_CLUSTER_ID_MULTI_VALUE &&
                field->extension_field_attribute_value_list &&
                field->length >= sizeof(uint16_t))
            {
                uint16_t desired_state = (uint16_t)field->extension_field_attribute_value_list[0] |
                                         ((uint16_t)field->extension_field_attribute_value_list[1] << 8);
                ESP_LOGI(TAG, "Recall scene %u/%u contains multi-value=%u", scene->group_id, scene->scene_id, desired_state);
                if ((usb_switch_state_t)desired_state != usb_switch_state)
                {
                    ESP_LOGI(TAG, "Applying recalled multi-value state by toggling switch");
                    toggle_gpio(GPIO_OUTPUT_IO_TOGGLE_SWITCH, 200);
                }
                break;
            }
            field = field->next;
        }
        break;
    }
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
    {
        const esp_zb_zcl_ota_upgrade_value_message_t *ota_msg = (const esp_zb_zcl_ota_upgrade_value_message_t *)message;
        ESP_LOGI(TAG,
                 "OTA status=%s (0x%04x), version=0x%08lx, image_type=0x%04x, payload_size=%u",
                 ota_status_to_str(ota_msg->upgrade_status),
                 ota_msg->upgrade_status,
                 (unsigned long)ota_msg->ota_header.file_version,
                 ota_msg->ota_header.image_type,
                 (unsigned int)ota_msg->payload_size);
        break;
    }
    case ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID:
    {
        const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *ota_resp = (const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *)message;
        ESP_LOGI(TAG,
                 "OTA query response status=0x%02x, version=0x%08lx, image_type=0x%04x, size=%lu",
                 ota_resp->query_status,
                 (unsigned long)ota_resp->file_version,
                 ota_resp->image_type,
                 (unsigned long)ota_resp->image_size);
        break;
    }
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
    esp_zb_ota_cluster_cfg_t ota_cfg = {
        .ota_upgrade_file_version = ESP_OTA_FILE_VERSION,
        .ota_upgrade_manufacturer = ESP_OTA_MANUFACTURER_CODE,
        .ota_upgrade_image_type = ESP_OTA_IMAGE_TYPE,
        .ota_min_block_reque = ESP_ZB_OTA_UPGRADE_MIN_BLOCK_PERIOD_DEF_VALUE,
        .ota_upgrade_file_offset = ESP_ZB_ZCL_OTA_UPGRADE_FILE_OFFSET_DEF_VALUE,
        .ota_upgrade_downloaded_file_ver = ESP_ZB_ZCL_OTA_UPGRADE_DOWNLOADED_FILE_VERSION_DEF_VALUE,
        .ota_upgrade_server_id = ESP_ZB_ZCL_OTA_UPGRADE_SERVER_DEF_VALUE,
        .ota_image_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_IMAGE_STATUS_DEF_VALUE,
    };
    esp_zb_attribute_list_t *ota_cluster = esp_zb_ota_cluster_create(&ota_cfg);
    esp_zb_zcl_ota_upgrade_client_variable_t ota_client_data = {
        .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version = 0,
        .max_data_size = 128,
    };
    uint8_t ota_server_endpoint = ESP_ZB_ZCL_OTA_UPGRADE_SERVER_ENDPOINT_DEF_VALUE;
    uint16_t ota_server_addr = ESP_ZB_ZCL_OTA_UPGRADE_SERVER_ADDR_DEF_VALUE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_ota_cluster_add_attr(ota_cluster,
                                                              ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID,
                                                              &ota_client_data));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_ota_cluster_add_attr(ota_cluster,
                                                              ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID,
                                                              &ota_server_endpoint));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_ota_cluster_add_attr(ota_cluster,
                                                              ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID,
                                                              &ota_server_addr));
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
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_cluster_list_add_ota_cluster(cluster_list, ota_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));

    // add endpoint with clusters to list
    esp_zb_endpoint_config_t ep_config = {
        .endpoint = HA_ESP_LIGHT_ENDPOINT,
        .app_device_id = ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_version = 1,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_zb_ep_list_add_ep(endpoint_list, cluster_list, ep_config));

    // add zcl basic cluster
    uint8_t sw_build_id[33] = {0};
    uint8_t date_code[17] = {0};
    build_basic_cluster_version_strings(ESP_OTA_FILE_VERSION, sw_build_id, sizeof(sw_build_id), date_code, sizeof(date_code));

    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = ESP_MANUFACTURER_NAME,
        .model_identifier = ESP_MODEL_IDENTIFIER,
        .date_code = date_code,
        .sw_build_id = sw_build_id,
    };
    esp_zcl_utility_add_ep_basic_manufacturer_info(endpoint_list, HA_ESP_LIGHT_ENDPOINT, &info);

    esp_zb_device_register(endpoint_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_set_secondary_network_channel_set(ESP_ZB_SECONDARY_CHANNEL_MASK);
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
    confirm_running_ota_image_if_pending();
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
