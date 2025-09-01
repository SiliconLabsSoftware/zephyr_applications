/***************************************************************************//**
 * @file throughput_central.c
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided \'as-is\', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 *******************************************************************************
 * # EXPERIMENTAL QUALITY
 * This code has not been formally tested and is provided as-is. It is not
 * suitable for production environments. In addition, this code will not be
 * maintained and there may be no bug maintenance planned for these resources.
 * Silicon Labs may update projects from time to time.
 ******************************************************************************/
#include <stdlib.h>
#include <assert.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "bt_event.h"

#include "throughput_common.h"
#include "throughput_ui_types.h"
#include "throughput_ui.h"
#include "throughput_gattdb.h"
#include "throughput_central_interface.h"
#include "throughput_central.h"

LOG_MODULE_REGISTER(throughput_central, 3);

/*******************************************************************************
 *******************************  DEFINITIONS   ********************************
 ******************************************************************************/
// Timeout for result indication in seconds
#define THROUGHPUT_CENTRAL_RESULT_TIMEOUT                1.0f

// Maximum data size
#define THROUGHPUT_CENTRAL_DATA_SIZE_MAX                 255

// Number of remote characteristics
#define THROUGHPUT_CENTRAL_CHARACTERISTICS_COUNT         4

// connection parameters
#define CONN_MIN_CE_LENGTH                               0
#define CONN_MAX_CE_LENGTH                               0x7FFF

// Hardware clock ticks that equal one second
#define HW_TICKS_PER_SECOND                              32768

#define TRANSMISSION_OFF                                 0

#define UUID_LEN                                         16

#define CONFIG_TX_POWER_MIN                              -100

#define EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE         (1 << 0)
#define EXTSIGNAL_EVENT_TRANSMISSION_START               (1 << 1)
#define EXTSIGNAL_EVENT_TRANSMISSION_STOP                (1 << 2)

#define LOG_NL                                           "\n"

/*******************************************************************************
 *****************************  LOCAL VARIABLES   ******************************
 ******************************************************************************/

/// BLE connection handle
static struct bt_conn *ble_conn = NULL;

static struct bt_gatt_discover_params notification_sub_disc_params,
                                      indication_sub_disc_params,
                                      transmission_on_sub_disc_params,
                                      throughput_result_sub_disc_params;

/// Discover Attributes parameters
static struct bt_gatt_subscribe_params notification_sub,
                                       indication_sub,
                                       transmission_on_sub,
                                       throughput_result_sub;
static struct bt_gatt_exchange_params exchange_params;

/// Enabled state
static bool enabled = false;

/// Finish test indicator
static bool finish_test = false;

/// Data for notification
static uint8_t notification_data[THROUGHPUT_CENTRAL_DATA_SIZE_MAX] = { 0 };

/// Data for indication
static uint8_t indication_data[THROUGHPUT_CENTRAL_DATA_SIZE_MAX] = { 0 };

/// Internal state
static throughput_t central_state = { .allowlist.next = NULL };

/// Bit counter variable
static throughput_count_t bytes_received = 0;

/// Operation (indication, notification) counter variable
static throughput_count_t operation_count = 0;

/// Power control status
static bool power_control_enabled = false;

/// Deep sleep enabled
static bool deep_sleep_enabled = THROUGHPUT_CENTRAL_SLEEP_ENABLE;

/// Data size limit for fixed data mode
static uint32_t fixed_data_size = THROUGHPUT_CENTRAL_FIXED_DATA_SIZE;

/// Time limit for fixed time mode
static uint32_t fixed_time = THROUGHPUT_CENTRAL_FIXED_TIME;

/// Finished time
static float finish_time = 0;

/// Throughput calculated after stop
static bool throughput_calculated = false;

/// Counter for checking data
static uint8_t received_counter = 0;

/// Flag for checking counter or accepting remote one
static bool first_packet = true;

static uint32_t  service_handle = 0xFFFFFFFF;
static uint16_t  transmission_handle = 0xFFFF;
static throughput_central_characteristic_found_t characteristic_found;
static action_t action = act_none;
// static uint16_t result_handle = 0xFFFF;

/*******************************************************************************
 ********************************  CONSTANTS   *********************************
 ******************************************************************************/
// Device name to match against scan results.
const char *device_name = "Throughput Test";

// /* Throughput Test Service */
// static struct bt_uuid_128 throughput_test_service_uuid = BT_UUID_INIT_128(
//   BT_UUID_128_ENCODE(0xbbb99e70, 0xfff7, 0x46cf, 0xabc7, 0x2d32c71820f2));

// /* Indications characteristic */
// static struct bt_uuid_128 indication_char_uuid = BT_UUID_INIT_128(
//   BT_UUID_128_ENCODE(0x6109b631, 0xa643, 0x4a51, 0x83d2, 0x2059700ad49f));

// /* Notifications characteristic */
// static struct bt_uuid_128 notification_char_uuid = BT_UUID_INIT_128(
//   BT_UUID_128_ENCODE(0x47b73dd6, 0xdee3, 0x4da1, 0x9be0, 0xf5c539a9a4be));

// /* Transmission ON characteristic */
// static struct bt_uuid_128 transmission_on_char_uuid = BT_UUID_INIT_128(
//   BT_UUID_128_ENCODE(0xbe6b6be1, 0xcd8a, 0x4106, 0x9181, 0x5ffe2bc67718));

// /* Throughput result characteristic */
// static struct bt_uuid_128 throughput_result_char_uuid = BT_UUID_INIT_128(
//   BT_UUID_128_ENCODE(0xadf32227, 0xb00f, 0x400c, 0x9eeb, 0xb903a6cc291b));

/*******************************************************************************
 *******************  FORWARD DECLARATION OF FUNCTIONS   ***********************
 ******************************************************************************/
static void check_received_data(uint8_t *data, uint8_t len);
static void reset_variables(void);
static bool process_scan_response(uint8_t *data, uint16_t data_len);

// static void device_found(const bt_addr_le_t *addr,
//                          int8_t rssi,
//                          uint8_t type,
//                          struct net_buf_simple *ad);

static bool throughput_central_allowlist_apply(const uint8_t *address);
static bool throughput_address_compare(const uint8_t *address1,
                                       const uint8_t *address2);
static void throughput_central_reset(void);

static void handle_throughput_central_start(bool send_transmission_on);
static void handle_throughput_central_stop(bool send_transmission_on);
static void throughput_central_scanning_start(void);
static void throughput_central_scanning_stop(void);

// static ssize_t on_char_read(enum throughput_gattdb_char c,
//                             struct bt_conn *conn,
//                             const struct bt_gatt_attr *attr,
//                             void *buf, uint16_t len,
//                             uint16_t offset)
// {
//   ssize_t bytes_read = 0;

//   switch (c) {
//     case throughput_gattdb_char_transmission_on:
//       break;
//     default:
//       break;
//   }

//   return bytes_read;
// }

static ssize_t on_char_write(enum throughput_gattdb_char c,
                             struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len,
                             uint16_t offset, uint8_t flags)
{
  ssize_t bytes_written = 0;

  switch (c) {
    case throughput_gattdb_char_transmission_on:
      if (((uint8_t *)buf)[0]) {
        bt_evt_external_signal(EXTSIGNAL_EVENT_TRANSMISSION_START);
      } else {
        bt_evt_external_signal(EXTSIGNAL_EVENT_TRANSMISSION_STOP);
      }
      break;

    default:
      break;
  }
  return bytes_written;
}

// Stop scanning
void throughput_central_scanning_stop(void)
{
  int err;

  if (central_state.discovery_state == THROUGHPUT_DISCOVERY_STATE_SCAN) {
    LOG_INF("Scanning stop.");
    err = bt_le_scan_stop();
    if (err) {
      LOG_WRN("Scanning failed to stop (err %d)\n", err);
      return;
    }
    central_state.discovery_state = THROUGHPUT_DISCOVERY_STATE_IDLE;
  }
}

/**************************************************************************//**
 * Event handle.
 *****************************************************************************/
static void discover_attrs_handle(struct bt_conn *conn,
                                  const struct bt_uuid *uuid,
                                  uint16_t handle,
                                  uint16_t perm,
                                  uint16_t value_handle)
{
  (void)perm;
  int err;

  LOG_INF("[ATTRIBUTE] handle %u\n", handle);

  switch (action)
  {
    case act_discover_service:
      if (!bt_uuid_cmp(uuid, &throughput_test_service_uuid.uuid)) {
        LOG_INF("[DISCOVER] throughput_test_service %d", handle);
        err = bt_evt_discover_attr(conn,
                                   BT_GATT_DISCOVER_CHARACTERISTIC,
                                   &indication_char_uuid.uuid,
                                   BT_ATT_FIRST_ATTRIBUTE_HANDLE,
                                   BT_ATT_LAST_ATTRIBUTE_HANDLE);
        if (err) {
          LOG_ERR("Discover failed (err %d)\n", err);
        }
        action = act_discover_characteristics;
      }
      break;

    case act_discover_characteristics:
      if (!bt_uuid_cmp(uuid, &indication_char_uuid.uuid)) {
        characteristic_found.characteristic.indication = 1;
        // indication_char_uuid handle
        indication_sub.value_handle = value_handle;
        LOG_INF("[DISCOVER] indication_char handle: %d", handle);
        err = bt_evt_discover_attr(conn,
                                   BT_GATT_DISCOVER_CHARACTERISTIC,
                                   &notification_char_uuid.uuid,
                                   BT_ATT_FIRST_ATTRIBUTE_HANDLE,
                                   BT_ATT_LAST_ATTRIBUTE_HANDLE);
        if (err) {
          LOG_ERR("Discover failed (err %d)\n", err);
        }
      } else if (!bt_uuid_cmp(uuid, &notification_char_uuid.uuid)) {
        characteristic_found.characteristic.notification = 1;
        // notification_char_uuid handle
        notification_sub.value_handle = value_handle;
        LOG_INF("[DISCOVER] notification_char handle: %d", handle);
        err = bt_evt_discover_attr(conn,
                                   BT_GATT_DISCOVER_CHARACTERISTIC,
                                   &transmission_on_char_uuid.uuid,
                                   BT_ATT_FIRST_ATTRIBUTE_HANDLE,
                                   BT_ATT_LAST_ATTRIBUTE_HANDLE);
        if (err) {
          LOG_ERR("Discover failed (err %d)\n", err);
        }
      } else if (!bt_uuid_cmp(uuid, &transmission_on_char_uuid.uuid)) {
        characteristic_found.characteristic.transmission_on = 1;
        // transmission_on_char_uuid handle
        transmission_handle = value_handle;
        transmission_on_sub.value_handle = value_handle;
        LOG_INF("Discover transmission_on_char handle %d", handle);
        err = bt_evt_discover_attr(conn,
                                   BT_GATT_DISCOVER_CHARACTERISTIC,
                                   &throughput_result_char_uuid.uuid,
                                   BT_ATT_FIRST_ATTRIBUTE_HANDLE,
                                   BT_ATT_LAST_ATTRIBUTE_HANDLE);
        if (err) {
          LOG_ERR("Discover failed (err %d)\n", err);
        }
      } else if (!bt_uuid_cmp(uuid, &throughput_result_char_uuid.uuid)) {
        characteristic_found.characteristic.result = 1;
        // transmission_on_char_uuid handle
        throughput_result_sub.value_handle = value_handle;
        LOG_INF("Discover throughput_result_char handle %d", handle);
      }

      if (characteristic_found.all == THROUGHPUT_CENTRAL_CHARACTERISTICS_ALL) {
        LOG_INF("Subcribe to characteristic");
        action = act_enable_notification;
        bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
      }
      break;

    default:
      break;
  }
}

static void throughput_central_event_handle(struct bt_evt *evt)
{
  int err = 0;

  switch (evt->id) {
    case bt_evt_le_scan_report_id:
      char addr_str[BT_ADDR_LE_STR_LEN];

      if (ble_conn) {
        break;
      }

      /* We're only interested in connectable events */
      if ((evt->data.le_scan_report.type != BT_GAP_ADV_TYPE_ADV_IND)
          && (evt->data.le_scan_report.type
              != BT_GAP_ADV_TYPE_ADV_DIRECT_IND)) {
        break;
      }

      if ((central_state.discovery_state == THROUGHPUT_DISCOVERY_STATE_SCAN)
          & process_scan_response(evt->data.le_scan_report.buf.data,
                                  evt->data.le_scan_report.buf.len)) {
        bt_addr_le_to_str(&(evt->data.le_scan_report.addr),
                          addr_str,
                          sizeof(addr_str));
        LOG_INF("Device found: %s (RSSI %d)\n",
                addr_str,
                evt->data.le_scan_report.rssi);
        // Apply allowlist filtering
        if (false == throughput_central_allowlist_apply(
              evt->data.le_scan_report.addr.a.val)) {
          return;
        }

        throughput_central_scanning_stop();

        // Open the connection
        err = bt_conn_le_create(&(evt->data.le_scan_report.addr),
                                BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT,
                                &ble_conn);
        if (err) {
          LOG_INF("Create conn to %s failed (%d)\n", addr_str, err);
          throughput_central_scanning_start();
          break;
        }
        central_state.discovery_state = THROUGHPUT_DISCOVERY_STATE_CONN;
      } else {
        waiting_indication();
      }
      break;

    case  bt_evt_connection_opened_id:
    {
      char addr[BT_ADDR_LE_STR_LEN];

      if (evt->data.connection_opened.conn != ble_conn) {
        break;
      }

      bt_addr_le_to_str(bt_conn_get_dst(evt->data.connection_opened.conn),
                        addr,
                        sizeof(addr));

      err = evt->data.connection_opened.err;
      if (err) {
        printk("Failed to connect to %s (%u)\n", addr, err);

        bt_conn_unref(ble_conn);
        ble_conn = NULL;

        throughput_central_scanning_start();
        break;
      }
      LOG_INF("Connected: %s", addr);

      struct bt_conn_info info;
      err = bt_conn_get_info(evt->data.connection_opened.conn, &info);
      if (err == 0) {
        central_state.pdu_size = info.le.data_len->tx_max_len;
        central_state.mtu_size = bt_gatt_get_mtu(
          evt->data.connection_opened.conn);
        LOG_INF("[CONN] pdu_size = %d", central_state.pdu_size);
        LOG_INF("[CONN] mtu_size = %d", central_state.mtu_size);

        central_state.interval = info.le.interval;
        central_state.connection_responder_latency = info.le.latency;
        central_state.connection_timeout = info.le.timeout;

        central_state.phy = info.le.phy->tx_phy;
      } else {
        LOG_ERR("Get connection information failed (err %d)", err);
      }

      err = bt_conn_le_data_len_update(evt->data.connection_opened.conn,
                                       BT_LE_DATA_LEN_PARAM_MAX);
      if (err) {
        LOG_ERR("[CONN] LE data length update request failed: %d", err);
      }

      err = bt_evt_exchange_mtu(evt->data.connection_opened.conn,
                                &exchange_params);
      if (err) {
        LOG_ERR("[CONN] MTU exchange failed (err %d)", err);
      }

      err = bt_conn_le_phy_update(evt->data.connection_opened.conn,
                                  BT_CONN_LE_PHY_PARAM_2M);
      if (err) {
        LOG_ERR("[CONN] Phy update request failed (err %d)", err);
      }

      central_state.state = THROUGHPUT_STATE_CONNECTED;
      LOG_INF("[STATUS] CONNECTED");
      throughput_central_on_state_change(central_state.state);

#if defined(CONFIG_BT_TRANSMIT_POWER_CONTROL)
      // Set remote connection power reporting - needed for Power Control
      err = bt_conn_le_set_tx_power_report_enable(ble_conn, true,
                                                  power_control_enabled);
      if (err) {
        LOG_ERR(" Set power reporting failed (err %d)", err);
      }
#endif /* CONFIG_BT_TRANSMIT_POWER_CONTROL */
      central_state.discovery_state = THROUGHPUT_DISCOVERY_STATE_SERVICE;
      throughput_central_on_discovery_state_change(central_state.discovery_state);

      // Subscribe to service provided by the peripheral
      err = bt_evt_discover_attr(evt->data.connection_opened.conn,
                                 BT_GATT_DISCOVER_PRIMARY,
                                 &throughput_test_service_uuid.uuid,
                                 BT_ATT_FIRST_ATTRIBUTE_HANDLE,
                                 BT_ATT_LAST_ATTRIBUTE_HANDLE);
      if (err) {
        LOG_ERR("Discover failed (err %d)\n", err);
      }
      action = act_discover_service;
    }
    break;

    case  bt_evt_connection_closed_id:
      LOG_INF("Disconnected (reason %u)", evt->data.connection_closed.reason);
      if (evt->data.connection_closed.conn != ble_conn) {
        return;
      }

      bt_conn_unref(ble_conn);
      ble_conn = NULL;

      if (central_state.state == THROUGHPUT_STATE_UNINITALIZING) {
        enabled = false;
        central_state.state = THROUGHPUT_STATE_UNINITALIZED;
        LOG_INF("[STATUS] UNINITALIZED");
        // Delete the connection, reset variables, stop timers
        throughput_central_reset();
      } else {
        central_state.state = THROUGHPUT_STATE_DISCONNECTED;
        LOG_INF("[STATUS] DISCONNECTED");
        // Delete the connection, reset variables, stop timers
        throughput_central_reset();
        throughput_central_scanning_start();
      }
      throughput_central_on_state_change(central_state.state);
      break;

    case bt_evt_gatt_discover_attrs_id:
      discover_attrs_handle(evt->data.gatt_discover_attrs.conn,
                            &(evt->data.gatt_discover_attrs.uuid),
                            evt->data.gatt_discover_attrs.handle,
                            evt->data.gatt_discover_attrs.perm,
                            evt->data.gatt_discover_attrs.value_handle);
      break;

    case bt_evt_gatt_characteristic_subscribe_status_id:
      if (evt->data.gatt_characteristic_subscribe_status.err == 0) {
        switch (action) {
          case act_enable_notification:
            // Notifications turned on, turn on indication
            central_state.notifications = BT_GATT_CCC_NOTIFY;
            action = act_enable_indication;
            throughput_central_on_notification_change(
              central_state.notifications);
            bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
            break;
          case act_enable_indication:
            central_state.indications = BT_GATT_CCC_INDICATE;
            throughput_central_on_indication_change(central_state.indications);
            action = act_enable_transmission_notification;
            bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
            break;

          case act_enable_transmission_notification:
            // Subscribe to peripheral result.
            action = act_subscribe_result;
            bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
            break;

          case act_subscribe_result:
            action = act_none;
            central_state.state = THROUGHPUT_STATE_SUBSCRIBED;
            LOG_INF("[STATUS] SUBSCRIBED");
            throughput_central_on_state_change(central_state.state);
            // Start RSSI refresh timer
            timer_refresh_rssi_start();
            break;
          default:
            break;
        }
      }
      break;

    case bt_evt_le_param_updated_id:
      central_state.interval = evt->data.le_param_updated.interval;
      central_state.connection_responder_latency =
        evt->data.le_param_updated.latency;
      central_state.connection_timeout = evt->data.le_param_updated.timeout;
      throughput_central_on_connection_timings_change(central_state.interval,
                                                      central_state.connection_responder_latency,
                                                      central_state.connection_timeout);
      break;

    case bt_evt_le_phy_updated_id:
      LOG_INF("bt_evt_le_phy_updated_id");

      central_state.phy = evt->data.le_phy_updated.tx_phy;
      throughput_central_on_phy_change(central_state.phy);
      break;

    case bt_evt_exchange_mtu_id:
      LOG_INF("bt_evt_exchange_mtu_id");
      central_state.mtu_size = bt_gatt_get_mtu(
        evt->data.le_data_len_updated.conn);
      throughput_central_on_connection_settings_change(central_state.pdu_size,
                                                       central_state.mtu_size);
      break;

    case bt_evt_le_data_len_updated_id:
      LOG_INF("bt_evt_le_data_len_updated_id");
      central_state.pdu_size = evt->data.le_data_len_updated.tx_max_len;
      central_state.mtu_size = bt_gatt_get_mtu(
        evt->data.le_data_len_updated.conn);
      throughput_central_on_connection_settings_change(central_state.pdu_size,
                                                       central_state.mtu_size);
      break;

#if defined(CONFIG_BT_TRANSMIT_POWER_CONTROL)
    case bt_evt_tx_power_report_id:
      LOG_INF("bt_evt_tx_power_report_id");
      central_state.phy = evt->data.tx_power_report.phy;
      central_state.tx_power = evt->data.tx_power_report.tx_power_level;
      break;
#endif /* CONFIG_BT_TRANSMIT_POWER_CONTROL */

    case bt_evt_gatt_characteristic_notification_id:
      if (evt->data.gatt_characteristic_notification.value_handle
          == notification_sub.value_handle) {
        // Check data for loss or error
        check_received_data(
          evt->data.gatt_characteristic_notification.value.data,
          evt->data.gatt_characteristic_notification.value.len);
        // Count bytes and operation
        bytes_received += evt->data.gatt_characteristic_notification.value.len;
        if (central_state.data_size
            != evt->data.gatt_characteristic_notification.value.len) {
          central_state.data_size =
            evt->data.gatt_characteristic_notification.value.len;
          throughput_central_on_data_size_change(central_state.data_size);
        }
        operation_count++;
        // Fixed data mode
        if ((central_state.mode == THROUGHPUT_MODE_FIXED_LENGTH)
            && (bytes_received >= (fixed_data_size))) {
          finish_test = true;
        }
      } else if (evt->data.gatt_characteristic_notification.value_handle
                 == transmission_on_sub.value_handle) {
        if (evt->data.gatt_characteristic_notification.value.data[0]) {
          bt_evt_external_signal(EXTSIGNAL_EVENT_TRANSMISSION_START);
        } else {
          bt_evt_external_signal(EXTSIGNAL_EVENT_TRANSMISSION_STOP);
        }
      }
      break;

    case bt_evt_gatt_characteristic_indication_id:
      if (evt->data.gatt_characteristic_indication.value_handle
          == indication_sub.value_handle) {
        // Check data for loss or error
        check_received_data(evt->data.gatt_characteristic_indication.value.data,
                            evt->data.gatt_characteristic_indication.value.len);
        // Count bytes and operation
        bytes_received += evt->data.gatt_characteristic_indication.value.len;
        if (central_state.data_size
            != evt->data.gatt_characteristic_indication.value.len) {
          central_state.data_size =
            evt->data.gatt_characteristic_indication.value.len;
          throughput_central_on_data_size_change(central_state.data_size);
        }
        operation_count++;
        // Fixed data mode
        if ((central_state.mode == THROUGHPUT_MODE_FIXED_LENGTH)
            && (bytes_received >= (fixed_data_size))) {
          finish_test = true;
        }
      } else if (evt->data.gatt_characteristic_indication.value_handle
                 == throughput_result_sub.value_handle) {
        // Responder sends indication about result after each test. Data is
        //   uint8array LSB first.
        // memcpy(&results.throughput_peripheral_side,
        //   evt->data.evt_gatt_characteristic_value.value.data, 4);
        handle_throughput_central_stop(false);
      }
      break;

    case bt_evt_system_external_signal_id:
      LOG_INF("bt_evt_system_external_signal_id");
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_TRANSMISSION_START) {
        handle_throughput_central_start(false);
      } else if (evt->data.system_external_signal.extsignals
                 & EXTSIGNAL_EVENT_TRANSMISSION_STOP) {
        finish_test = true;
      }
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE) {
        switch (action) {
          case act_enable_notification:
            err = bt_evt_gatt_subscribe_for_notification(ble_conn,
                                                         &notification_sub,
                                                         &notification_sub_disc_params);
            if (err) {
              LOG_ERR("[SUBSCRIBE] notification_sub failed (err %d)\n", err);
            }
            break;
          case act_enable_indication:
            err = bt_evt_gatt_subscribe_for_indication(ble_conn,
                                                       &indication_sub,
                                                       &indication_sub_disc_params);
            if (err) {
              LOG_ERR("[SUBSCRIBE] indication_sub failed (err %d)\n", err);
            }
            break;

          case act_enable_transmission_notification:
            err = bt_evt_gatt_subscribe_for_notification(ble_conn,
                                                         &transmission_on_sub,
                                                         &transmission_on_sub_disc_params);
            if (err) {
              LOG_ERR("[SUBSCRIBE] transmission_on_sub failed (err %d)\n", err);
            }
            break;

          case act_subscribe_result:
            err = bt_evt_gatt_subscribe_for_indication(ble_conn,
                                                       &throughput_result_sub,
                                                       &throughput_result_sub_disc_params);
            if (err) {
              LOG_ERR("[SUBSCRIBE] throughput_result_sub failed (err %d)\n",
                      err);
            }
            break;
          default:
            break;
        }

        // Wait and retry when queue is being full
        if (err == -ENOMEM) {
          k_sleep(K_MSEC(100));
          bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
        }
      }
      break;
    default:
      break;
  }
}

static void throughput_central_event_poll(void)
{
  struct bt_evt event;

  int err = bt_evt_wait_event(&event, K_NO_WAIT);

  if (!err) {
    throughput_central_event_handle(&event);
  }
}

// Start scanning
static void throughput_central_scanning_start(void)
{
  int err;
  struct bt_le_scan_param scan_param = {
    // .type       = BT_LE_SCAN_TYPE_ACTIVE,
    .type = BT_LE_SCAN_TYPE_PASSIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = 16,
    .window = 16,
  };

  LOG_INF("Scanning started...");

  // Reset found characteristics
  characteristic_found.all = 0;

  // Reset discovery state
  central_state.discovery_state = THROUGHPUT_DISCOVERY_STATE_SCAN;

  // Start scanning - looking for peripheral devices
  err = bt_evt_le_scan_start(&scan_param);
  if (err) {
    LOG_WRN("Scanning failed to start (err %d)\n", err);
    return;
  }
}

// Start reception
static void handle_throughput_central_start(bool send_transmission_on)
{
  uint8_t value;
  int err;

  // Set test type
  value = central_state.test_type;

  // Clear results
  central_state.throughput = 0;
  central_state.count = 0;
  central_state.packet_error = 0;
  central_state.packet_lost = 0;

  // Clear counters
  bytes_received = 0;
  operation_count = 0;

  received_counter = 0;
  first_packet = true;

  throughput_calculated = false;

  // Stop RSSI refresh timer
  timer_refresh_rssi_stop();

  if (send_transmission_on) {
    LOG_INF("call bt_gatt_write_without_response()");
    // This triggers the data transmission on remote side
    err = bt_gatt_write_without_response(ble_conn,
                                         transmission_handle,
                                         &value, 1,
                                         false);
    if (err != 0) {
      LOG_WRN("bt_gatt_write_without_response failed: %d", err);
    }
  }
  // Set state and call back
  central_state.state = THROUGHPUT_STATE_TEST;
  throughput_central_on_state_change(central_state.state);
  throughput_central_on_start();
  LOG_INF("[STATUS] TEST");

  // Start timer
  timer_start();
}

// Restart scanning
void throughput_central_scanning_restart(void)
{
  int err;

  bool close_required = (central_state.state != THROUGHPUT_STATE_DISCONNECTED);

  if (!enabled) {
    return;
  }
  if (close_required) {
    err = bt_conn_disconnect(ble_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    assert(err);
  } else {
    throughput_central_scanning_stop();
  }

  memset(notification_data, 0, THROUGHPUT_CENTRAL_DATA_SIZE_MAX);
  memset(indication_data, 0, THROUGHPUT_CENTRAL_DATA_SIZE_MAX);

  central_state.state = THROUGHPUT_STATE_DISCONNECTED;
  central_state.client_conf_flag = 0;

  // Clear results
  central_state.throughput = 0;
  central_state.count = 0;
  central_state.packet_error = 0;
  central_state.packet_lost = 0;

  reset_variables();

  if (!close_required) {
    throughput_central_scanning_start();
  }

  // Notify application
  throughput_central_on_state_change(central_state.state);
#ifdef SL_CATALOG_THROUGHPUT_UI_PRESENT
  throughput_ui_set_all(central_state);
#endif // SL_CATALOG_THROUGHPUT_UI_PRESENT
}

// Finish reception
static void handle_throughput_central_stop(bool send_transmission_on)
{
  int err;
  uint8_t value = TRANSMISSION_OFF;
  float time_elapsed = 0.0f;

  // Calculate throughput
  if (!throughput_calculated) {
    time_elapsed = throughput_central_calculate(NULL);
    throughput_calculated = true;
  }

  if (send_transmission_on) {
    finish_time = time_elapsed;
    // Triggers the data transmission end on remote
    err = bt_gatt_write_without_response(ble_conn,
                                         transmission_handle,
                                         &value, 1,
                                         false);
    if (err != 0) {
      LOG_WRN("bt_gatt_write_without_response failed: %d", err);
    }
  } else {
    finish_test = false;
    finish_time = 0;

    central_state.count = operation_count;

    throughput_central_on_finish(central_state.throughput,
                                 central_state.count,
                                 central_state.packet_lost,
                                 central_state.packet_error,
                                 central_state.time);
    central_state.state = THROUGHPUT_STATE_SUBSCRIBED;
    LOG_INF("[STATUS] SUBSCRIBED");
    throughput_central_on_state_change(central_state.state);

    // Start RSSI refresh timer
    timer_refresh_rssi_start();
  }
}

bool throughput_central_decode_address(char *addess_str, uint8_t *address)
{
  (void) addess_str;
  (void) address;
  int retval;
  unsigned int address_cache[ADR_LEN];
  uint8_t i;

  retval = sscanf(addess_str, "%2X:%2X:%2X:%2X:%2X:%2X",
                  &address_cache[5],
                  &address_cache[4],
                  &address_cache[3],
                  &address_cache[2],
                  &address_cache[1],
                  &address_cache[0]);

  if (retval == ADR_LEN) {
    for (i = 0; i < ADR_LEN; i++) {
      address[i] = (uint8_t)(address_cache[i]);
    }
    return true;
  } else {
    return false;
  }
}

bool throughput_central_allowlist_clear(void)
{
  // throughput_allowlist_t *current_entry;
  // throughput_allowlist_t *entry_cache;

  // current_entry = central_state.allowlist.next;

  // while (current_entry != NULL) {
  //   entry_cache = current_entry->next;
  //   sl_free(current_entry);
  //   current_entry = entry_cache;
  // }

  // central_state.allowlist.next = NULL;
  return true;
}

bool throughput_central_allowlist_add(uint8_t *address)
{
  // throughput_allowlist_t *current_entry;
  // throughput_allowlist_t *next_entry;
  bool ret_val = false;
  // uint8_t addr_type = 0;

  // current_entry = &central_state.allowlist;

  // //Search for the last entry in the list
  // while (current_entry != NULL) {
  //   // break if the address is already in the list
  //   if (throughput_address_compare(current_entry->address.val, address)) {
  //     break;
  //   }
  //   if (current_entry->next == NULL) { //This is the last entry in the list
  //     //Allocate memory for the next one.
  //     next_entry = sl_malloc(sizeof(throughput_allowlist_t));
  //     if (next_entry == NULL) {
  //       ret_val = false;
  //     } else {
  //       //Set the pointer to the next entry
  //       current_entry->next = next_entry;
  //       //Add the new address
  //       LOG_INF("Adding address to the allowlist
  //   %02X:%02X:%02X:%02X:%02X:%02X\n",
  //                 address[5],
  //                 address[4],
  //                 address[3],
  //                 address[2],
  //                 address[1],
  //                 address[0]);
  //       memcpy(&current_entry->address.val, address, 6);
  //       current_entry->address_type = addr_type;
  //       next_entry->next = NULL;
  //       ret_val = true;
  //     }
  //     break;
  //   } else {
  //     current_entry = current_entry->next;
  //   }
  // }
  return ret_val;
}

float throughput_central_calculate(throughput_value_t *throughput)
{
  float time_elapsed;

  time_elapsed = timer_end();
  central_state.time = (throughput_time_t)time_elapsed;
  central_state.throughput = (throughput_value_t)((float)bytes_received
                                                  * 8
                                                  / time_elapsed);
  if (throughput != NULL) {
    *throughput = central_state.throughput;
  }

  return time_elapsed;
}

/**************************************************************************//**
 * Process step for throughput central.
 *****************************************************************************/
void throughput_central_step(void)
{
  // poll for events
  if (enabled) {
    throughput_central_event_poll();
  }

  if (enabled && (central_state.state == THROUGHPUT_STATE_TEST)) {
    if (central_state.mode == THROUGHPUT_MODE_FIXED_TIME) {
      if (timer_end() >= fixed_time) {
        finish_test = true;
      }
    }
    // Test should be finished
    if (finish_test) {
      if (finish_time == 0) {
        handle_throughput_central_stop(true);
      } else {
        // Check timeout for result
        if ((timer_end() - finish_time) > THROUGHPUT_CENTRAL_RESULT_TIMEOUT) {
          handle_throughput_central_stop(false);
        }
      }
    }
  }
}

/**************************************************************************//**
 * Enable receiver.
 *****************************************************************************/
void throughput_central_enable(void)
{
  uint8_t address[ADR_LEN];

  // throughput_gattdb_register_char_read_cb(on_char_read);
  throughput_gattdb_register_char_write_cb(on_char_write);
  // throughput_gattdb_register_ccc_changed_cb(ccc_changed);

  // Enable UI
  throughput_ui_init();

  memset(notification_data, 0, THROUGHPUT_CENTRAL_DATA_SIZE_MAX);
  memset(indication_data, 0, THROUGHPUT_CENTRAL_DATA_SIZE_MAX);

  central_state.role = THROUGHPUT_ROLE_CENTRAL;
  central_state.state = THROUGHPUT_STATE_DISCONNECTED;

  central_state.test_type = THROUGHPUT_CENTRAL_TEST_TYPE;

  central_state.mode = THROUGHPUT_CENTRAL_MODE_DEFAULT;
  central_state.tx_power = THROUGHPUT_CENTRAL_TX_POWER;
  central_state.mtu_size = THROUGHPUT_CENTRAL_DATA_SIZE_MAX;

  central_state.client_conf_flag = 0;

  central_state.connection_interval_min =
    THROUGHPUT_CENTRAL_CONNECTION_INTERVAL_MIN;
  central_state.connection_interval_max =
    THROUGHPUT_CENTRAL_CONNECTION_INTERVAL_MAX;
  central_state.connection_responder_latency =
    THROUGHPUT_CENTRAL_CONNECTION_RESPONDER_LATENCY;
  central_state.connection_timeout = THROUGHPUT_CENTRAL_CONNECTION_TIMEOUT;

  // central_state.scan_phy = THROUGHPUT_DEFAULT_SCAN_PHY;

  // Clear results
  central_state.throughput = 0;
  central_state.count = 0;
  central_state.packet_error = 0;
  central_state.packet_lost = 0;

  if (THROUGHPUT_CENTRAL_POWER_CONTROL_ENABLE) {
    power_control_enabled = true;
  } else {
    power_control_enabled = false;
  }

  reset_variables();

#if defined(THROUGHPUT_CENTRAL_ALLOWLIST_ENABLE) \
  && THROUGHPUT_CENTRAL_ALLOWLIST_ENABLE == 1
#if defined(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_1_ENABLE) \
  && THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_1_ENABLE == 1
  throughput_central_decode_address(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_1,
                                    address);
  throughput_central_allowlist_add(address);
#endif // THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_1_ENABLE
#if defined(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_2_ENABLE) \
  && THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_2_ENABLE == 1
  throughput_central_decode_address(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_2,
                                    address);
  throughput_central_allowlist_add(address);
#endif // THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_2_ENABLE
#if defined(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_3_ENABLE) \
  && THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_3_ENABLE == 1
  throughput_central_decode_address(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_3,
                                    address);
  throughput_central_allowlist_add(address);
#endif // THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_3_ENABLE
#if defined(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_4_ENABLE) \
  && THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_4_ENABLE == 1
  throughput_central_decode_address(THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_4,
                                    address);
  throughput_central_allowlist_add(address);
#endif // THROUGHPUT_CENTRAL_ALLOWLIST_SLOT_4_ENABLE
#endif // THROUGHPUT_CENTRAL_ALLOWLIST_ENABLE

  (void) address;

  // Notify application
  throughput_central_on_state_change(central_state.state);
  throughput_central_on_role_set(central_state.role);

  // Init bluetooth event module
  bt_evt_init();

  // Start scanning
  throughput_central_scanning_start();

  enabled = true;
}

/**************************************************************************//**
 * Disable receiver.
 *****************************************************************************/
int throughput_central_disable(void)
{
  int err = 0;

  if (central_state.state == THROUGHPUT_STATE_TEST) {
    return -1;
  }

  // Stop scanning
  throughput_central_scanning_stop();

  if (central_state.state != THROUGHPUT_STATE_DISCONNECTED) {
    err = bt_conn_disconnect(ble_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    central_state.state = THROUGHPUT_STATE_UNINITALIZING;
  } else {
    central_state.state = THROUGHPUT_STATE_UNINITALIZED;
    enabled = false;
    throughput_central_reset();
  }

  throughput_central_on_state_change(central_state.state);
  return err;
}

/**************************************************************************//**
 * Set receiver mode.
 *****************************************************************************/
int throughput_central_set_mode(throughput_mode_t mode,
                                uint32_t amount)
{
  int res = 0;

  if (enabled && (central_state.state != THROUGHPUT_STATE_TEST)) {
    if (mode == THROUGHPUT_MODE_FIXED_LENGTH) {
      fixed_data_size = amount;
    } else if (mode == THROUGHPUT_MODE_FIXED_TIME) {
      fixed_time = amount;
    }
    central_state.mode = mode;
  } else {
    res = -1;
  }
  return res;
}

/**************************************************************************//**
 * Set data sizes for reception.
 *****************************************************************************/
int throughput_central_set_mtu_size(uint8_t mtu)
{
  int res = 0;
  if (enabled && (central_state.state != THROUGHPUT_STATE_TEST)) {
    central_state.mtu_size = mtu;
    // res = sl_bt_gatt_server_set_max_mtu(central_state.mtu_size,
    //   &(central_state.mtu_size));
  } else {
    res = -1;
  }
  return res;
}

/**************************************************************************//**
 * Set transmission power.
 *****************************************************************************/
int throughput_central_set_tx_power(throughput_tx_power_t tx_power,
                                    bool power_control,
                                    bool deep_sleep)
{
  int res = 0;
  if (enabled && (central_state.state != THROUGHPUT_STATE_TEST)) {
    central_state.tx_power_requested = tx_power;
    power_control_enabled = power_control;
    deep_sleep_enabled = deep_sleep;
    throughput_central_scanning_restart();
  } else {
    res = -1;
  }
  return res;
}

/**************************************************************************//**
 * Set connection parameters.
 *****************************************************************************/
int throughput_central_set_connection_parameters(throughput_time_t min_interval,
                                                 throughput_time_t max_interval,
                                                 throughput_time_t latency,
                                                 throughput_time_t timeout)
{
  int res = 0;
  if (enabled
      && ((central_state.state == THROUGHPUT_STATE_SUBSCRIBED)
          || (central_state.state == THROUGHPUT_STATE_CONNECTED))) {
    central_state.connection_interval_min = min_interval;
    central_state.connection_interval_max = max_interval;
    central_state.connection_responder_latency = latency;
    central_state.connection_timeout = timeout;

    // Set connection parameters for this connection.
    // res = sl_bt_connection_set_parameters(connection_handle_central,
    //         central_state.connection_interval_min,
    //         central_state.connection_interval_max,
    //         central_state.connection_responder_latency,
    //         central_state.connection_timeout,
    //         CONN_MIN_CE_LENGTH,
    //         CONN_MAX_CE_LENGTH);
  } else {
    res = -1;
  }
  return res;
}

/**************************************************************************//**
 * Set PHY used for scanning.
 *****************************************************************************/
int throughput_central_set_scan_phy(throughput_scan_phy_t phy)
{
  int res = 0;
  if (enabled && (central_state.state == THROUGHPUT_STATE_DISCONNECTED)) {
    throughput_central_scanning_stop();
    central_state.scan_phy = phy;
    throughput_central_scanning_start();
  } else {
    res = -1;
  }
  return res;
}

/**************************************************************************//**
 * Set PHY used for the connection.
 *****************************************************************************/
int throughput_central_set_connection_phy(throughput_phy_t phy)
{
  int res = -1;
  // uint8_t accepted_phy = (uint8_t)phy;
  if (enabled
      && ((central_state.state == THROUGHPUT_STATE_CONNECTED)
          || (central_state.state == THROUGHPUT_STATE_SUBSCRIBED))) {
    // if (phy == sl_bt_gap_phy_coding_500k_coded) {
    //   accepted_phy = sl_bt_gap_phy_coded;
    // }
    // res = sl_bt_connection_set_preferred_phy(connection_handle_central,
    //                                          phy,
    //                                          accepted_phy);
  }
  return res;
}

/**************************************************************************//**
 * Set type of transmission.
 *****************************************************************************/
int throughput_central_set_type(throughput_notification_t type)
{
  int err = 0;
  if (enabled && (central_state.state != THROUGHPUT_STATE_TEST)) {
    central_state.test_type = type;
    if ((central_state.test_type != BT_GATT_CCC_INDICATE)
        && (central_state.test_type != BT_GATT_CCC_NOTIFY)) {
      err = -1;
    }
  } else {
    err = -1;
  }
  return err;
}

/**************************************************************************//**
 * Start transmission on remote side.
 *****************************************************************************/
int throughput_central_start(void)
{
  int err = 0;
  if (enabled && (central_state.state == THROUGHPUT_STATE_SUBSCRIBED)) {
    handle_throughput_central_start(true);
  } else {
    err = -1;
  }
  return err;
}

/**************************************************************************//**
 * Stop transmission on remote side.
 *****************************************************************************/
int throughput_central_stop(void)
{
  int err = 0;
  if (enabled && (central_state.state == THROUGHPUT_STATE_TEST)) {
    finish_test = true;
  } else {
    err = -1;
  }
  return err;
}

/*******************************************************************************
 **************************   LOCAL FUNCTIONS   ********************************
 ******************************************************************************/
static void check_received_data(uint8_t *data, uint8_t len)
{
  uint8_t counter;

  if (len == 0) {
    central_state.packet_error++;
    return;
  }

  counter = data[0];

  if (first_packet) {
    // Accept remote counter
    received_counter = counter;
    first_packet = false;
  } else {
    // Increment local counter
    received_counter = (received_counter + 1) % 100;
  }
  if (counter != received_counter) {
    // Lost packet is found
    uint8_t lost_packet_count = 0;
    // Calculate lost packet count
    if (counter > received_counter) {
      lost_packet_count = counter - received_counter;
    } else if (counter < received_counter) {
      lost_packet_count = 100 + counter - received_counter;
    }
    central_state.packet_lost += lost_packet_count;
    // Sync to remote counter
    received_counter = counter;
  }

  // Check data for bit errors
  for (int i = 1; i < len; i++) {
    if (data[i] != (uint8_t) 'a' + (uint8_t) ((i - 1) % 26)) {
      central_state.packet_error++;
      break;
    }
  }
}

static void reset_variables(void)
{
  ble_conn = NULL;

  service_handle = 0xFFFFFFFF;
  transmission_handle = 0xFFFF;
  characteristic_found.all = 0;

  bytes_received = 0;
  operation_count = 0;

  received_counter = 0;
  first_packet = true;

  central_state.notifications = 0;
  central_state.indications = 0;

  central_state.throughput = 0;
  central_state.throughput_peripheral_side = 0;
  central_state.count = 0;
  central_state.packet_error = 0;
  central_state.packet_lost = 0;
}

static bool throughput_central_allowlist_apply(const uint8_t *address)
{
  bool ret_val = false;
  throughput_allowlist_t *current_entry = &central_state.allowlist;

  if (current_entry->next == NULL) {
    ret_val = true;
  }

  // Search for the last entry in the list
  while (current_entry->next != NULL) {
    if (true
        == throughput_address_compare(current_entry->address.val, address)) {
      ret_val = true;
      break;
    }
    current_entry = current_entry->next;
  }

  return ret_val;
}

static bool throughput_address_compare(const uint8_t *address1,
                                       const uint8_t *address2)
{
  bool ret_val = false;

  if (memcmp(address1, address2, ADR_LEN) == 0) {
    ret_val = true;
  }

  return ret_val;
}

static void throughput_central_reset(void)
{
  // Stop RSSI refresh timer
  timer_refresh_rssi_stop();

  ble_conn = NULL;
  central_state.discovery_state = THROUGHPUT_DISCOVERY_STATE_IDLE;

  // Reset variables
  reset_variables();
}

static bool process_scan_response(uint8_t *data, uint16_t data_len)
{
  int i = 0;
  bool device_name_match = false;
  uint8_t adv_length;
  uint8_t adv_type;

  while (i < (data_len - 1)) {
    adv_length = data[i];
    adv_type = data[i + 1];

    /* Type 0x09 = Complete Local Name, 0x08 Shortened Name */
    if (adv_type == 0x09) {
      /* Check if device name is Throughput Test */
      if (memcmp(data + i + 2, device_name, strlen(device_name)) == 0) {
        device_name_match = true;
        break;
      }
    }

    /* Jump to next AD record */
    i = i + adv_length + 1;
  }

  return (device_name_match);
}

/*******************************************************************************
 **************************** CLI RELATED FUNCTIONS ****************************
 ******************************************************************************/
#ifdef CONFIG_SHELL
void cli_throughput_central_stop(const struct shell *sh,
                                 size_t argc,
                                 char **argv)
{
  int err;
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  if (central_state.state == THROUGHPUT_STATE_TEST) {
    err = throughput_central_stop();
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Central not in test state");
  }
}

void cli_throughput_central_start(const struct shell *sh,
                                  size_t argc,
                                  char **argv)
{
  int err;
  uint8_t test;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }

  if (central_state.state == THROUGHPUT_STATE_SUBSCRIBED) {
    test = atoi(argv[1]);
    err = throughput_central_set_type((throughput_notification_t)test);
    if (err == 0) {
      err = throughput_central_start();
      if (err == 0) {
        shell_print(sh, "OK");
      } else {
        shell_print(sh, "ERROR");
      }
    } else {
      shell_print(sh, "Central start failed");
    }
  } else {
    shell_print(sh, "Central not in subcribed state");
  }
}

void cli_throughput_central_status(const struct shell *sh,
                                   size_t argc,
                                   char **argv)
{
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  switch (central_state.state) {
    case THROUGHPUT_STATE_CONNECTED:
      shell_print(sh, THROUGHPUT_UI_STATE_CONNECTED_TEXT);
      break;
    case THROUGHPUT_STATE_DISCONNECTED:
      shell_print(sh, THROUGHPUT_UI_STATE_DISCONNECTED_TEXT);
      break;
    case THROUGHPUT_STATE_SUBSCRIBED:
      shell_print(sh, THROUGHPUT_UI_STATE_SUBSCRIBED_TEXT);
      break;
    case THROUGHPUT_STATE_TEST:
      shell_print(sh, THROUGHPUT_UI_STATE_TEST_TEXT);
      break;
    default:
      shell_print(sh, THROUGHPUT_UI_STATE_UNKNOWN_TEXT);
      break;
  }
  if (central_state.role == THROUGHPUT_ROLE_PERIPHERAL) {
    shell_print(sh, THROUGHPUT_UI_ROLE_PERIPHERAL_TEXT);
  } else {
    shell_print(sh, THROUGHPUT_UI_ROLE_CENTRAL_TEXT);
  }
  shell_print(sh, "OK");
}

void cli_throughput_central_mode_set(const struct shell *sh,
                                     size_t argc,
                                     char **argv)
{
  int err;
  uint8_t mode;
  uint32_t amount;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }

  if (central_state.state != THROUGHPUT_STATE_TEST) {
    mode = atoi(argv[1]);
    amount = atoi(argv[2]);
    err = throughput_central_set_mode((throughput_mode_t)mode, amount);
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Central not in test state");
  }
}

void cli_throughput_central_mode_get(const struct shell *sh,
                                     size_t argc,
                                     char **argv)
{
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  shell_print(sh, "mode settings");
  shell_print(sh, "%d %u %u",
              (int)central_state.mode,
              (uint32_t)fixed_data_size,
              (uint32_t)fixed_time);
}

void cli_throughput_central_tx_power_set(const struct shell *sh,
                                         size_t argc,
                                         char **argv)
{
  int err;
  int16_t power;
  uint8_t control, sleep;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }

  if (central_state.state != THROUGHPUT_STATE_TEST) {
    power = atoi(argv[1]);
    control = atoi(argv[2]);
    sleep = atoi(argv[3]);
    err = throughput_central_set_tx_power(power,
                                          control,
                                          sleep);
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Central not in test state");
  }
}

void cli_throughput_central_tx_power_get(const struct shell *sh,
                                         size_t argc,
                                         char **argv)
{
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  shell_print(sh, "TX power settings");
  shell_print(sh, "%d %d %d",
              (int)central_state.tx_power,
              (int)power_control_enabled,
              (int)deep_sleep_enabled);
}

void cli_throughput_central_data_set(const struct shell *sh,
                                     size_t argc,
                                     char **argv)
{
  uint8_t mtu;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  if (central_state.state != THROUGHPUT_STATE_TEST) {
    mtu = atoi(argv[1]);
    central_state.mtu_size = mtu;
    shell_print(sh, "OK");
    throughput_central_scanning_restart();
  } else {
    shell_print(sh, "ERROR");
  }
}

void cli_throughput_central_data_get(const struct shell *sh,
                                     size_t argc,
                                     char **argv)
{
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  shell_print(sh, "data");
  shell_print(sh, "%d",
              (int)central_state.mtu_size);
}

void cli_throughput_central_phy_scan_set(const struct shell *sh,
                                         size_t argc,
                                         char **argv)
{
  uint8_t phy_scan;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  if (central_state.state != THROUGHPUT_STATE_TEST) {
    phy_scan = atoi(argv[1]);
    central_state.scan_phy = (throughput_scan_phy_t)phy_scan;
    throughput_central_scanning_restart();
    shell_print(sh, "OK");
  } else {
    shell_print(sh, "ERROR");
  }
}

void cli_throughput_central_phy_conn_set(const struct shell *sh,
                                         size_t argc,
                                         char **argv)
{
  uint8_t phy_conn;
  int err;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  if (central_state.state != THROUGHPUT_STATE_TEST) {
    phy_conn = atoi(argv[1]);
    if (central_state.state == THROUGHPUT_STATE_DISCONNECTED) {
      central_state.phy = (throughput_phy_t)phy_conn;
      shell_print(sh, "OK");
    } else {
      err = throughput_central_set_connection_phy((throughput_phy_t)phy_conn);
      if (err == 0) {
        shell_print(sh, "OK");
      } else {
        shell_print(sh, "ERROR");
      }
    }
  } else {
    shell_print(sh, "Central not in test state");
  }
}

void cli_throughput_central_phy_get(const struct shell *sh,
                                    size_t argc,
                                    char **argv)
{
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  shell_print(sh, "phy settings");
  shell_print(sh, "%d %d",
              (int)central_state.scan_phy,
              (int)central_state.phy);
}

void cli_throughput_central_connection_set(const struct shell *sh,
                                           size_t argc,
                                           char **argv)
{
  throughput_time_t connection_interval_min;
  throughput_time_t connection_interval_max;
  throughput_time_t connection_responder_latency;
  throughput_time_t connection_timeout;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  if (central_state.state != THROUGHPUT_STATE_TEST) {
    connection_interval_min = atoi(argv[1]);
    connection_interval_max = atoi(argv[2]);
    connection_responder_latency = atoi(argv[3]);
    connection_timeout = atoi(argv[4]);
    central_state.connection_interval_min = connection_interval_min;
    central_state.connection_interval_max = connection_interval_max;
    central_state.connection_responder_latency = connection_responder_latency;
    central_state.connection_timeout = connection_timeout;
    shell_print(sh, "OK");
    throughput_central_scanning_restart();
  } else {
    shell_print(sh, "ERROR");
  }
}

void cli_throughput_central_connection_get(const struct shell *sh,
                                           size_t argc,
                                           char **argv)
{
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  shell_print(sh, "connection settings");
  shell_print(sh, "%u %u %u %u",
              (uint32_t)central_state.connection_interval_min,
              (uint32_t)central_state.connection_interval_max,
              (uint32_t)central_state.connection_responder_latency,
              (uint32_t)central_state.connection_timeout);
}

void cli_throughput_central_allowlist_add(const struct shell *sh,
                                          size_t argc,
                                          char **argv)
{
  bool ret;
  uint8_t address[ADR_LEN];
  char *address_str;

  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  if (central_state.state != THROUGHPUT_STATE_TEST) {
    address_str = argv[1];
    if (throughput_central_decode_address(address_str, address)) {
      ret = throughput_central_allowlist_add(address);
      if (ret) {
        shell_print(sh, "OK");
        throughput_central_scanning_restart();
      } else {
        shell_print(sh, "ERROR");
      }
    } else {
      shell_print(sh, "Allowlist addr decode fail");
    }
  } else {
    shell_print(sh, "Central not in test state");
  }
}

void cli_throughput_central_allowlist_clear(const struct shell *sh,
                                            size_t argc,
                                            char **argv)
{
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  if (central_state.state != THROUGHPUT_STATE_TEST) {
    throughput_central_allowlist_clear();
    shell_print(sh, "OK");
    throughput_central_scanning_restart();
  } else {
    shell_print(sh, "ERROR");
  }
}

void cli_throughput_central_allowlist_get(const struct shell *sh,
                                          size_t argc,
                                          char **argv)
{
  throughput_allowlist_t *allowlist = &central_state.allowlist;
  if (!enabled) {
    shell_print(sh, "Central not enabled");
    return;
  }
  shell_print(sh, "allowlist");
  shell_print(sh, "---------------------");
  shell_print(sh, "|      ADDRESS      |");
  shell_print(sh, "---------------------");
  while (allowlist->next != NULL) {
    shell_print(sh, "| %02x:%02x:%02x:%02x:%02x:%02x |",
                allowlist->address.val[5],
                allowlist->address.val[4],
                allowlist->address.val[3],
                allowlist->address.val[2],
                allowlist->address.val[1],
                allowlist->address.val[0]);
    allowlist = allowlist->next;
  }
  shell_print(sh, "---------------------");
}

#endif

/*******************************************************************************
 *********************** CALLBACK WEAK IMPLEMENTATIONS *************************
 ******************************************************************************/

/**************************************************************************//**
 * Weak implementation of callback to handle role settings.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_role_set(
  throughput_role_t role)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_role(role);
  throughput_ui_update();
#else
  if (role == THROUGHPUT_ROLE_PERIPHERAL) {
    printk(THROUGHPUT_UI_ROLE_PERIPHERAL_TEXT);
  } else {
    printk(THROUGHPUT_UI_ROLE_CENTRAL_TEXT);
  }
  printk(LOG_NL);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle state change.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_state_change(
  throughput_state_t state)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_state(state);
  throughput_ui_update();
#else
  switch (state) {
    case THROUGHPUT_STATE_CONNECTED:
      printk(THROUGHPUT_UI_STATE_CONNECTED_TEXT);
      break;
    case THROUGHPUT_STATE_DISCONNECTED:
      printk(THROUGHPUT_UI_STATE_DISCONNECTED_TEXT);
      break;
    case THROUGHPUT_STATE_SUBSCRIBED:
      printk(THROUGHPUT_UI_STATE_SUBSCRIBED_TEXT);
      break;
    case THROUGHPUT_STATE_TEST:
      printk(THROUGHPUT_UI_STATE_TEST_TEXT);
      break;
    case THROUGHPUT_STATE_UNINITALIZED:
      printk(THROUGHPUT_UI_STATE_UNINITIALIZED_TEXT);
      break;
    case THROUGHPUT_STATE_UNINITALIZING:
      printk(THROUGHPUT_UI_STATE_UNINITIALIZING_TEXT);
      break;
    default:
      printk(THROUGHPUT_UI_STATE_UNKNOWN_TEXT);
      break;
  }
  printk(LOG_NL);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle mode change.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_mode_change(
  throughput_mode_t mode)
{
#ifdef THROUGHPUT_UI_PRESENT
  (void) mode;
#else
  (void) mode;
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle transmission start event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_start(void)
{
#ifdef THROUGHPUT_UI_PRESENT
  // Do nothing
#else
  // Do something on start
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle transmission finished event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_finish(
  throughput_value_t throughput,
  throughput_count_t count,
  throughput_count_t lost,
  throughput_count_t error,
  throughput_time_t time)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_throughput(throughput);
  throughput_ui_set_count(count);
  throughput_ui_update();
#else
  printk(THROUGHPUT_UI_TH_FORMAT LOG_NL, ((int)throughput));
  printk(THROUGHPUT_UI_CNT_FORMAT LOG_NL, ((int)count));
#endif
  printk(THROUGHPUT_UI_LOST_FORMAT LOG_NL, ((int)lost));
  printk(THROUGHPUT_UI_ERROR_FORMAT LOG_NL, ((int)error));
  printk(THROUGHPUT_UI_TIME_FORMAT LOG_NL, ((int)time));
}

/**************************************************************************//**
 * Weak implementation of callback to handle tx power changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_transmit_power_change(
  throughput_tx_power_t power)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_tx_power(power);
  throughput_ui_update();
#else
  printk(THROUGHPUT_UI_TX_POWER_FORMAT LOG_NL, ((int)power));
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle RSSI changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_rssi_change(
  throughput_rssi_t rssi)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_rssi(rssi);
  throughput_ui_update();
#else
  printk(THROUGHPUT_UI_RSSI_FORMAT LOG_NL, ((int)rssi));
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle phy changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_phy_change(
  throughput_phy_t phy)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_phy(phy);
  throughput_ui_update();
#else
  switch (phy) {
    case sl_bt_gap_phy_coding_1m_uncoded:
      printk(THROUGHPUT_UI_PHY_1M_TEXT);
      break;
    case sl_bt_gap_phy_coding_2m_uncoded:
      printk(THROUGHPUT_UI_PHY_2M_TEXT);
      break;
    case sl_bt_gap_phy_coding_125k_coded:
      printk(THROUGHPUT_UI_PHY_CODED_125K_TEXT);
      break;
    case sl_bt_gap_phy_coding_500k_coded:
      printk(THROUGHPUT_UI_PHY_CODED_500K_TEXT);
      break;
    default:
      printk(THROUGHPUT_UI_PHY_UNKNOWN_TEXT);
      break;
  }
  printk(LOG_NL);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle notification changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_notification_change(
  throughput_notification_t notification)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_notifications(notification);
  throughput_ui_update();
#else

  switch (notification) {
    case BT_GATT_CCC_NOTIFY:
      printk(THROUGHPUT_UI_NOTIFY_YES_TEXT);
      break;
    default:
      printk(THROUGHPUT_UI_NOTIFY_NO_TEXT);
      break;
  }
  printk(LOG_NL);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle indication changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_indication_change(
  throughput_notification_t indication)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_indications(indication);
  throughput_ui_update();
#else
  switch (indication) {
    case BT_GATT_CCC_INDICATE:
      printk(THROUGHPUT_UI_INDICATE_YES_TEXT);
      break;
    default:
      printk(THROUGHPUT_UI_INDICATE_NO_TEXT);
      break;
  }
  printk(LOG_NL);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle result indication changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_result_indication_change(
  throughput_notification_t result)
{
#ifdef THROUGHPUT_UI_PRESENT
  (void) result;
#else
  switch (result) {
    case BT_GATT_CCC_INDICATE:
      printk(THROUGHPUT_UI_RESULT_YES_TEXT);
      break;
    default:
      printk(THROUGHPUT_UI_RESULT_NO_TEXT);
      break;
  }
  printk(LOG_NL);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle connection parameter changes.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_data_size_change(
  throughput_pdu_size_t data_size)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_data_size(data_size);
#else
  printk(THROUGHPUT_UI_DATA_SIZE_FORMAT LOG_NL, (int)data_size);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle connection parameter changes.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_connection_settings_change(
  throughput_pdu_size_t pdu,
  throughput_mtu_size_t mtu)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_pdu_size(pdu);
  throughput_ui_set_mtu_size(mtu);
  throughput_ui_update();
#else
  printk(THROUGHPUT_UI_PDU_SIZE_FORMAT LOG_NL, (int)pdu);
  printk(THROUGHPUT_UI_MTU_SIZE_FORMAT LOG_NL, (int)mtu);
#endif
}

/**************************************************************************//**
 * Weak implementation of callback to handle connection timing changes.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_connection_timings_change(
  throughput_time_t interval,
  throughput_time_t latency,
  throughput_time_t timeout)
{
#ifdef THROUGHPUT_UI_PRESENT
  throughput_ui_set_connection_interval(interval);
  throughput_ui_update();
#else
  printk(THROUGHPUT_UI_INTERVAL_FORMAT LOG_NL, (int)((float)interval * 1.25));
#endif
  printk(THROUGHPUT_UI_LATENCY_FORMAT LOG_NL, (int)latency);
  printk(THROUGHPUT_UI_TIMEOUT_FORMAT LOG_NL, (int)timeout);
}

/**************************************************************************//**
 * Weak implementation of callback to handle characteristic found event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_characteristics_found(
  throughput_central_characteristic_found_t characteristics)
{
  printk(THROUGHPUT_UI_CHARACTERISTICS_FOUND_FORMAT LOG_NL,
         THROUGHPUT_UI_YESNO(characteristics.characteristic.notification),
         THROUGHPUT_UI_YESNO(characteristics.characteristic.indication),
         THROUGHPUT_UI_YESNO(characteristics.characteristic.transmission_on),
         THROUGHPUT_UI_YESNO(characteristics.characteristic.result)
         );
}

/**************************************************************************//**
 * Weak implementation of callback to handle discovery state change.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_central_on_discovery_state_change(
  throughput_discovery_state_t state)
{
  switch (state) {
    case THROUGHPUT_DISCOVERY_STATE_IDLE:
      printk(THROUGHPUT_UI_DISCOVERY_STATE_IDLE_TEXT);
      break;
    case THROUGHPUT_DISCOVERY_STATE_CONN:
      printk(THROUGHPUT_UI_DISCOVERY_STATE_CONN_TEXT);
      break;
    case THROUGHPUT_DISCOVERY_STATE_SCAN:
      printk(THROUGHPUT_UI_DISCOVERY_STATE_SCAN_TEXT);
      break;
    case THROUGHPUT_DISCOVERY_STATE_SERVICE:
      printk(THROUGHPUT_UI_DISCOVERY_STATE_SERVICE_TEXT);
      break;
    case THROUGHPUT_DISCOVERY_STATE_CHARACTERISTICS:
      printk(THROUGHPUT_UI_DISCOVERY_STATE_CHARACTERISTICS_TEXT);
      break;
    case THROUGHPUT_DISCOVERY_STATE_FINISHED:
      printk(THROUGHPUT_UI_DISCOVERY_STATE_FINISHED_TEXT);
      break;
    default:
      printk(THROUGHPUT_UI_DISCOVERY_STATE_UNKNOWN_TEXT);
      break;
  }
  printk(LOG_NL);
}
