#pragma once

#include "esp_err.h"
#include "esp_zigbee_core.h"

/**
 * @brief Log the OTA state of the currently running partition.
 *
 * @param prefix  Label printed before the state line (e.g. "Boot before confirm").
 */
void ota_log_partition_state(const char *prefix);

/**
 * @brief Set the OTA query interval on the device's Zigbee endpoint.
 *        Call this after joining or rejoining the network.
 *
 * @param endpoint  Zigbee endpoint ID that hosts the OTA upgrade client cluster.
 */
void ota_configure_query_interval(uint8_t endpoint);

/**
 * @brief On startup, mark the running image as valid if it is in PENDING_VERIFY state.
 *        Also calls zb_osif_bootloader_report_successful_loading() unconditionally so
 *        the ZBOSS stack knows the image loaded cleanly.
 */
void ota_confirm_image_if_pending(void);

/**
 * @brief Handle the ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID action callback.
 *        Logs the status and, on FINISH, schedules the reboot task.
 *
 * @param message  Pointer to esp_zb_zcl_ota_upgrade_value_message_t.
 */
void ota_handle_upgrade_value(const void *message);

/**
 * @brief Handle the ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID action callback.
 *        Logs the query response.
 *
 * @param message  Pointer to esp_zb_zcl_ota_upgrade_query_image_resp_message_t.
 */
void ota_handle_query_image_resp(const void *message);
