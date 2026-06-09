#include "ota.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_zigbee_ota.h"
#include "zcl/esp_zigbee_zcl_ota.h"
#include "zigbee_usb_switch.h"

static const char *TAG = "OTA";

/* --- ZBOSS OSIF callbacks ---------------------------------------------------
 *
 * These are declared in zb_osif.h but NOT implemented in the zboss library.
 * The ZBOSS OSIF pattern requires the platform to supply the bodies.
 * Including zb_osif.h directly causes compile errors (it uses internal zboss
 * types not visible to user code), so we just implement the symbols directly.
 *
 * zb_ret_t == int32_t; RET_OK == 0; RET_ERROR == -1.
 */

int32_t zb_osif_bootloader_run_after_reboot(void)
{
    /*
     * The OTA apply path in the Zigbee OTA client finalizes and validates
     * the image. Selecting the boot slot here can be wrong if the stack uses
     * a different target/flow, so keep this callback as a success no-op.
     */
    ESP_LOGI(TAG, "osif: run_after_reboot requested");
    return 0; /* RET_OK */
}

void zb_osif_bootloader_report_successful_loading(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "osif: image confirmed valid, rollback cancelled");
    }
    else
    {
        ESP_LOGE(TAG, "osif: mark_app_valid failed: %s", esp_err_to_name(err));
    }
}

/* --- Internal helpers -------------------------------------------------------*/

static const char *ota_img_state_to_str(esp_ota_img_states_t state)
{
    switch (state)
    {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending_verify";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
        return "undefined";
    default:
        return "unknown";
    }
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

static bool s_ota_reboot_scheduled = false;

static void ota_log_partition_details(const char *prefix, const esp_partition_t *partition)
{
    if (!partition)
    {
        ESP_LOGI(TAG, "%s: <none>", prefix);
        return;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(partition, &state);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "%s: '%s' subtype=0x%02x addr=0x%08lx state=%s",
                 prefix,
                 partition->label,
                 partition->subtype,
                 (unsigned long)partition->address,
                 ota_img_state_to_str(state));
        return;
    }

    ESP_LOGI(TAG, "%s: '%s' subtype=0x%02x addr=0x%08lx state_query=%s",
             prefix,
             partition->label,
             partition->subtype,
             (unsigned long)partition->address,
             esp_err_to_name(err));
}

static void ota_log_boot_selection(const char *prefix)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = running ? esp_ota_get_next_update_partition(running) : NULL;
    const esp_partition_t *ota_0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota_1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

    ESP_LOGI(TAG, "%s", prefix);
    ota_log_partition_details("  running", running);
    ota_log_partition_details("  boot", boot);
    ota_log_partition_details("  next_update", next);
    ota_log_partition_details("  ota_0", ota_0);
    ota_log_partition_details("  ota_1", ota_1);
}

static void ota_finish_reboot_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "finish received; activating new partition and rebooting");

    /*
     * The Zigbee OTA library writes and verifies the image (apply status)
     * but does not update otadata to activate the new slot.  We must call
     * esp_ota_set_boot_partition() here, after apply has completed, so the
     * bootloader switches to the newly-written partition on the next boot.
     */
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(running);
    if (update != NULL)
    {
        esp_err_t err = esp_ota_set_boot_partition(update);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "boot partition set to '%s' (0x%08lx)", update->label, (unsigned long)update->address);
        }
        else
        {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition('%s') failed: %s — will still reboot",
                     update->label, esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGE(TAG, "no update partition found; rebooting anyway");
    }

    ota_log_boot_selection("boot selection after activation");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    /* esp_restart() should not return, but keep task contract explicit. */
    vTaskDelete(NULL);
}

/* --- Public API -------------------------------------------------------------*/

void ota_log_partition_state(const char *prefix)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        ESP_LOGW(TAG, "%s: running partition unavailable", prefix);
        return;
    }
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: '%s' subtype=0x%02x state query failed: %s",
                 prefix, running->label, running->subtype, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "%s: '%s' subtype=0x%02x state=%s",
             prefix, running->label, running->subtype, ota_img_state_to_str(state));
}

void ota_configure_query_interval(uint8_t endpoint)
{
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_err_t err = esp_zb_ota_upgrade_client_query_interval_set(endpoint, ESP_OTA_QUERY_INTERVAL_MIN);
    esp_zb_lock_release();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "failed to set OTA query interval: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "query interval set to %u min", (unsigned int)ESP_OTA_QUERY_INTERVAL_MIN);
    }
}

void ota_confirm_image_if_pending(void)
{
    ota_log_boot_selection("boot selection at confirm");

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        ESP_LOGW(TAG, "confirm: running partition unavailable");
        return;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "confirm: state query failed for '%s': %s",
                 running->label, esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "confirm: running='%s' state=%s", running->label, ota_img_state_to_str(state));

    if (state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback();
        if (mark_err == ESP_OK)
        {
            ESP_LOGI(TAG, "confirm: marked valid, rollback cancelled");
        }
        else
        {
            ESP_LOGE(TAG, "confirm: mark_app_valid failed: %s", esp_err_to_name(mark_err));
        }
    }

    zb_osif_bootloader_report_successful_loading();
}

void ota_handle_upgrade_value(const void *message)
{
    const esp_zb_zcl_ota_upgrade_value_message_t *msg =
        (const esp_zb_zcl_ota_upgrade_value_message_t *)message;

    ESP_LOGI(TAG, "status=%s (0x%04x) version=0x%08lx image_type=0x%04x payload=%u B",
             ota_status_to_str(msg->upgrade_status),
             msg->upgrade_status,
             (unsigned long)msg->ota_header.file_version,
             msg->ota_header.image_type,
             (unsigned int)msg->payload_size);

    if (msg->upgrade_status == ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH && !s_ota_reboot_scheduled)
    {
        s_ota_reboot_scheduled = true;
        BaseType_t ret = xTaskCreate(ota_finish_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
        if (ret != pdPASS)
        {
            ESP_LOGE(TAG, "failed to create reboot task");
            s_ota_reboot_scheduled = false;
        }
        else
        {
            ESP_LOGI(TAG, "reboot task scheduled");
        }
    }
}

void ota_handle_query_image_resp(const void *message)
{
    const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *msg =
        (const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *)message;

    ESP_LOGI(TAG, "query response: status=0x%02x version=0x%08lx image_type=0x%04x size=%lu B",
             msg->query_status,
             (unsigned long)msg->file_version,
             msg->image_type,
             (unsigned long)msg->image_size);
}
