/***************************************************************************//**
 * @file throughput_peripheral.c
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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "bt_event.h"

#include "throughput_ui_types.h"
#include "throughput_ui.h"
#include "throughput_gattdb.h"
#include "throughput_peripheral.h"

LOG_MODULE_REGISTER(throughput_peripheral, 3);

/*******************************************************************************
 *******************************  DEFINITIONS   ********************************
 ******************************************************************************/

#define DEVICE_NAME                                         "Throughput Test"
#define DEVICE_NAME_LEN                                     (sizeof(DEVICE_NAME) \
                                                             - 1)

// Size of the arrays for sending and receiving
#define THROUGHPUT_TX_DATA_SIZE                             255
// Refresh RSSI timer period
#define THROUGHPUT_TX_REFRESH_TIMER_PERIOD                  1000
// Hardware clock ticks that equal one second
#define HW_TICKS_PER_SECOND                                 (uint16_t)(32768)
// GATT operation header byte count
#define INDICATION_GATT_HEADER                              3
// GATT operation header byte count
#define NOTIFICATION_GATT_HEADER                            3
// Header byte count
#define L2CAP_HEADER                                        4
// Indication timeout period
#define THROUGHPUT_TX_INDICATION_TIMEOUT                    500
// Minimum TX power
#define CONFIG_TX_POWER_MIN                                 -100

#define EXTSIGNAL_EVENT_REFRESH_CONNECTED_STATE             (1 << 0)
#define EXTSIGNAL_EVENT_TRANSMISSION_ON_CHAR_WRITE          (1 << 1)
#define EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE            (1 << 2)
#define EXTSIGNAL_EVENT_PHY_STATE_CCC_CHANGED               (1 << 3)
#define EXTSIGNAL_EVENT_MTU_SIZE_CCC_CHANGED                (1 << 4)
#define EXTSIGNAL_EVENT_PDU_SIZE_CCC_CHANGED                (1 << 5)
#define EXTSIGNAL_EVENT_THROUGHPUT_NOTIFICATION_CCC_CHANGED (1 << 6)
#define EXTSIGNAL_EVENT_THROUGHPUT_INDICATION_CCC_CHANGED   (1 << 7)
#define EXTSIGNAL_EVENT_REFRESH_TIMER_RISE                  (1 << 8)

#define LOG_NL                                              "\n"

/*******************************************************************************
 ********************************  CONSTANTS   *********************************
 ******************************************************************************/
/// Transmission on
static const uint8_t TRANSMISSION_ON = 1;

/// Transmission off
static const uint8_t TRANSMISSION_OFF = 0;

/* Advertisement data */
static const struct bt_data ad[] = {
  BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
  BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)
};

/*******************************************************************************
 *****************************  LOCAL VARIABLES   ******************************
 ******************************************************************************/

/// BLE connection handle
static struct bt_conn *ble_conn;

/// Characteristic handles
// static uint16_t notifications_handle = 0xFFFF;
// static uint16_t indications_handle = 0xFFFF;
// static uint16_t transmission_handle = 0xFFFF;

/// Discover Attributes parameters
static struct bt_gatt_discover_params notification_sub_disc_params,
                                      indication_sub_disc_params,
                                      transmission_on_sub_disc_params;

static struct bt_gatt_subscribe_params notification_sub,
                                       indication_sub,
                                       transmission_on_sub;
static struct bt_gatt_exchange_params exchange_params;
// static struct bt_uuid_128 discover_uuid = BT_UUID_INIT_128(0);
// static const struct bt_uuid *ccc_uuid = BT_UUID_GATT_CCC;

static struct bt_gatt_indicate_params indication_indicate_params,
                                      throughput_result_indicate_params;

/// Internal state
static throughput_t peripheral_state;

/// Transmission on
static uint8_t transmission_on;

/// Indication state for result
static throughput_notification_t result_indicated = 0;

/// Indication state for transmission state
static throughput_notification_t transmission_indicated = 0;

/// Enabled state
static bool enabled = false;

/// Finish test indicator
static bool finish_test = false;

/// Send transmission state
static bool send_transmission_state = false;

/// Data size for indication
static uint16_t indication_data_size = 0;

/// Data size for notification
static uint16_t notification_data_size = 0;

/// Data for notification
static uint8_t notification_data[THROUGHPUT_TX_DATA_SIZE] = { 0 };

/// Data for indication
static uint8_t indication_data[THROUGHPUT_TX_DATA_SIZE] = { 0 };

/// Time storage variable
static throughput_count_t time_start = 0;

/// Time elapsed since start
static throughput_count_t time_elapsed = 0;

/// Bit counter variable
static throughput_count_t bytes_sent = 0;

/// Operation (indication, notification) counter variable
static throughput_count_t operation_count = 0;

/// Data size limit for fixed data mode
static uint32_t fixed_data_size = THROUGHPUT_PERIPHERAL_FIXED_DATA_SIZE;

/// Time limit for fixed time mode
static uint32_t fixed_time = THROUGHPUT_PERIPHERAL_FIXED_TIME;

/// Deep sleep enabled
static bool deep_sleep_enabled = THROUGHPUT_PERIPHERAL_TX_SLEEP_ENABLE;

/// Flag for send timer
static bool send_timer_rised = false;

/// Flag for indication timer
static bool indication_timer_rised = false;

/// Flag for indication
static bool indication_sent = false;

/// Flag for finish notification
static bool notification_sent = false;

/// Flag for indication confirmation
static bool indication_confirmed = false;

// /// Power control status
static bool power_control_enabled = false;

/// Requested notification data size
static uint8_t requested_notification_size =
  THROUGHPUT_PERIPHERAL_DATA_TRANSFER_SIZE_NOTIFICATIONS;

/// Requested indication data size
static uint8_t requested_indication_size =
  THROUGHPUT_PERIPHERAL_DATA_TRANSFER_SIZE_INDICATIONS;

/// Counter for checking data
static uint8_t received_counter = 0;

/// Flag for checking counter or accepting remote one
static bool first_packet = true;

/// Indicates that the test is from central to peripheral
static bool central_test = false;

/// Stores the found characteristics
static throughput_peripheral_characteristic_found_t characteristic_found;

/// Stores the throughput information service ccc
static throughput_information_service_ccc_enable_t ccc_enable;

/// Actions for the state machine
static action_t action = act_none;

/// Send counter for package identification
static uint8_t send_counter = 0;

/*******************************************************************************
 **************************   LOCAL FUNCTIONS   ********************************
 ******************************************************************************/
// static ssize_t transmission_on_char_write(struct bt_conn *conn,
//                                           const struct bt_gatt_attr *attr,
//                                           const void *buf, uint16_t len,
//                                           uint16_t offset, uint8_t flags);

static void throughput_peripheral_on_send_timer_rise(struct k_timer *timer);
static void throughput_peripheral_on_refresh_timer_rise(struct k_timer *timer);
static void throughput_peripheral_on_indication_timer_rise(
  struct k_timer *timer);

static void throughput_peripheral_calculate_data_size(void);
static void throughput_peripheral_advertising_start(void);
static void throughput_peripheral_calculate_notification_size(void);
static void throughput_peripheral_calculate_indication_size(void);

static void throughput_peripheral_generate_notifications_data(void);
static void throughput_peripheral_generate_indications_data(void);

static void handle_throughput_peripheral_start(bool send_transmission_on);
static void handle_throughput_peripheral_stop(bool send_transmission_on);
static void throughput_peripheral_send_notification(void);
static void throughput_peripheral_send_indication(void);

static void check_received_data(uint8_t *data, uint8_t len);

/// RSSI refresh timer
static K_TIMER_DEFINE(refresh_timer,
                      throughput_peripheral_on_refresh_timer_rise,
                      NULL);

/// Send timer
static K_TIMER_DEFINE(send_timer, throughput_peripheral_on_send_timer_rise,
                      NULL);

/// Indication timer
static K_TIMER_DEFINE(indication_timer,
                      throughput_peripheral_on_indication_timer_rise,
                      NULL);

// -----------------------------------------------------------------------------
//                       Marcos
// -----------------------------------------------------------------------------

/*******************************************************************************
 **************************   LOCAL FUNCTIONS   ********************************
 ******************************************************************************/
static const char *uuid_to_str(const struct bt_uuid *uuid)
{
  static char uuid_str[40];

  bt_uuid_to_str(uuid, uuid_str, sizeof(uuid_str));
  return uuid_str;
}

// static bool attr_check_match(const struct bt_gatt_attr *attr,
//                              const struct bt_uuid *uuid)
// {
//   const struct bt_gatt_attr *found_attr;

//   found_attr = bt_gatt_find_by_uuid(throughput_test_svc.attrs,
//                                     throughput_test_svc.attr_count,
//                                     uuid);
//   return found_attr && (attr->handle == found_attr->handle);
// }

#if defined(CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL)
static int hci_set_tx_power(uint8_t handle_type,
                            uint16_t handle,
                            int8_t tx_pwr_lvl)
{
  struct bt_hci_cp_vs_write_tx_power_level *cp;
  struct bt_hci_rp_vs_write_tx_power_level *rp;
  struct net_buf *buf, *rsp = NULL;
  int err;

  buf = bt_hci_cmd_alloc(K_MSEC(100));
  if (!buf) {
    LOG_ERR("Unable to allocate command buffer\n");
    return -1;
  }

  cp = net_buf_add(buf, sizeof(*cp));
  cp->handle = sys_cpu_to_le16(handle);
  cp->handle_type = handle_type;
  cp->tx_power_level = tx_pwr_lvl;

  err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL,
                             buf, &rsp);
  if (err) {
    uint8_t reason = rsp
                     ?((struct bt_hci_rp_vs_write_tx_power_level *)
                       rsp->data)->status : 0;
    LOG_ERR("Set Tx power err: %d reason 0x%02x\n", err, reason);
    return err;
  }

  rp = (void *)rsp->data;
  LOG_INF("Actual Tx Power: %d\n", rp->selected_tx_power);

  net_buf_unref(rsp);

  return 0;
}

static int hci_get_tx_power(uint8_t handle_type,
                            uint16_t handle,
                            int8_t *tx_pwr_lvl)
{
  struct bt_hci_cp_vs_read_tx_power_level *cp;
  struct bt_hci_rp_vs_read_tx_power_level *rp;
  struct net_buf *buf, *rsp = NULL;
  int err;

  *tx_pwr_lvl = 0xFF;
  buf = bt_hci_cmd_alloc(K_MSEC(100));
  if (!buf) {
    LOG_ERR("Unable to allocate command buffer\n");
    return -1;
  }

  cp = net_buf_add(buf, sizeof(*cp));
  cp->handle = sys_cpu_to_le16(handle);
  cp->handle_type = handle_type;

  err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_READ_TX_POWER_LEVEL,
                             buf, &rsp);
  if (err) {
    uint8_t reason = rsp
                     ?((struct bt_hci_rp_vs_read_tx_power_level *)
                       rsp->data)->status : 0;
    LOG_ERR("Read Tx power err: %d reason 0x%02x\n", err, reason);
    return err;
  }

  rp = (void *)rsp->data;
  *tx_pwr_lvl = rp->tx_power_level;

  net_buf_unref(rsp);
  return 0;
}

static int set_tx_power(int8_t tx_pwr_lvl)
{
  int err;
  uint16_t conn_handle;

  if (!ble_conn) {
    LOG_INF("Set Tx power level to %d\n", tx_pwr_lvl);
    return hci_set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV,
                            0, tx_pwr_lvl);
  } else {
    err = bt_hci_get_conn_handle(ble_conn,
                                 &conn_handle);
    if (err) {
      LOG_ERR("No connection handle (err %d)\n", err);
      return err;
    } else {
      LOG_INF("Set Tx power level to %d\n", tx_pwr_lvl);
      return hci_set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN,
                              conn_handle, tx_pwr_lvl);
    }
  }
}

static int get_tx_power(int8_t *tx_pwr_lvl)
{
  int err;
  uint16_t conn_handle;
  int8_t txp_get = 0;

  if (!ble_conn) {
    err = hci_get_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV,
                           0, &txp_get);
    LOG_INF("Get Tx power level -> TXP = %d", txp_get);
    *tx_pwr_lvl = txp_get;
    return err;
  } else {
    err = bt_hci_get_conn_handle(ble_conn,
                                 &conn_handle);
    if (err) {
      LOG_ERR("No connection handle (err %d)\n", err);
      return err;
    } else {
      err = hci_get_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN,
                             conn_handle, &txp_get);
      LOG_INF("Get Tx power level -> TXP = %d", txp_get);
      *tx_pwr_lvl = txp_get;
      return err;
    }
  }
}

#else
static int set_tx_power(int8_t tx_pwr_lvl)
{
  (void)tx_pwr_lvl;

  LOG_ERR("Set Tx power is not supported.");
  return -1;
}

static int get_tx_power(int8_t *tx_pwr_lvl)
{
  (void)tx_pwr_lvl;

  LOG_ERR("Get Tx power is not supported.");
  return -1;
}

#endif // CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL

#if defined(CONFIG_BT_CTLR_CONN_RSSI)
static int read_conn_rssi(struct bt_conn *conn, int8_t *rssi)
{
  struct net_buf *buf, *rsp = NULL;
  struct bt_hci_cp_read_rssi *cp;
  struct bt_hci_rp_read_rssi *rp;
  uint16_t conn_handle;
  int err;

  err = bt_hci_get_conn_handle(ble_conn,
                               &conn_handle);
  if (err) {
    LOG_ERR("No connection handle (err %d)\n", err);
    return err;
  }

  buf = bt_hci_cmd_alloc(K_MSEC(100));
  if (!buf) {
    LOG_ERR("Unable to allocate command buffer\n");
    return -1;
  }

  cp = net_buf_add(buf, sizeof(*cp));
  cp->handle = sys_cpu_to_le16(conn_handle);

  err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
  if (err) {
    uint8_t reason = rsp
                     ?((struct bt_hci_rp_read_rssi *)rsp->data)->status : 0;
    LOG_ERR("Read RSSI err: %d reason 0x%02x\n", err, reason);
    return err;
  }

  rp = (void *)rsp->data;
  *rssi = rp->rssi;

  net_buf_unref(rsp);
  return 0;
}

#else
static int read_conn_rssi(struct bt_conn *conn, int8_t *rssi)
{
  (void)conn;
  (void)rssi;

  LOG_ERR("Read RSSI is not supported.");
  return -1;
}

#endif // CONFIG_BT_CTLR_CONN_RSSI

static ssize_t on_char_read(enum throughput_gattdb_char c,
                            struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len,
                            uint16_t offset)
{
  ssize_t bytes_read;

  switch (c) {
    case throughput_gattdb_char_transmission_on:
      bytes_read = bt_gatt_attr_read(conn, attr, buf, len, offset,
                                     &transmission_on, sizeof(transmission_on));
      LOG_INF("[READ] transmission_on, value=%d, size=%d",
              transmission_on,
              bytes_read);
      break;
    case throughput_gattdb_char_connection_phy:
      bytes_read = bt_gatt_attr_read(conn, attr, buf, len, offset,
                                     &peripheral_state.phy, 1);
      LOG_INF("[READ] connection_phy, value=%d, size=%d",
              peripheral_state.phy, bytes_read);
      break;
    case throughput_gattdb_char_connection_interval:
      bytes_read = bt_gatt_attr_read(conn, attr, buf, len, offset,
                                     &peripheral_state.interval, 4);
      LOG_INF("[READ] connection_interval, value=%d, size=%d",
              peripheral_state.interval, bytes_read);
      break;
    case throughput_gattdb_char_responder_latency:
      bytes_read = bt_gatt_attr_read(conn,
                                     attr,
                                     buf,
                                     len,
                                     offset,
                                     &peripheral_state.connection_responder_latency,
                                     4);
      LOG_INF("[READ] connection_responder_latency, value=%d, size=%d",
              peripheral_state.connection_responder_latency, bytes_read);
      break;
    case throughput_gattdb_char_supervision_timeout:
      bytes_read = bt_gatt_attr_read(conn, attr, buf, len, offset,
                                     &peripheral_state.connection_timeout, 4);
      LOG_INF("[READ] connection_timeout, value=%d, size=%d",
              peripheral_state.connection_timeout, bytes_read);
      break;
    case throughput_gattdb_char_pdu_size:
      bytes_read = bt_gatt_attr_read(conn, attr, buf, len, offset,
                                     &peripheral_state.pdu_size, 1);
      LOG_INF("[READ] pdu_size, value=%d, size=%d",
              peripheral_state.pdu_size, bytes_read);
      break;
    case throughput_gattdb_char_mtu_size:
      bytes_read = bt_gatt_attr_read(conn, attr, buf, len, offset,
                                     &peripheral_state.mtu_size, 1);
      LOG_INF("[READ] mtu_size, value=%d, size=%d",
              peripheral_state.mtu_size, bytes_read);
      break;
    default:
      bytes_read = 0;
      break;
  }
  return bytes_read;
}

static ssize_t on_char_write(enum throughput_gattdb_char c,
                             struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len,
                             uint16_t offset, uint8_t flags)
{
  ssize_t bytes_written;

  switch (c) {
    case throughput_gattdb_char_transmission_on:
      LOG_INF("[WRITE] throughput_gattdb_char_transmission_on");
      transmission_on = *(uint8_t *)buf;
      bytes_written = len;
      bt_evt_external_signal(EXTSIGNAL_EVENT_TRANSMISSION_ON_CHAR_WRITE);
      break;
    default:
      bytes_written = 0;
      break;
  }
  return bytes_written;
}

static void ccc_changed(enum throughput_gattdb_char c,
                        const struct bt_gatt_attr *attr,
                        uint16_t value)
{
  (void) attr;

  switch (c) {
    case throughput_gattdb_char_indication:
      if (value == BT_GATT_CCC_INDICATE) {
        peripheral_state.indications = BT_GATT_CCC_INDICATE;
      }
      bt_evt_external_signal(EXTSIGNAL_EVENT_THROUGHPUT_INDICATION_CCC_CHANGED
                             | EXTSIGNAL_EVENT_REFRESH_CONNECTED_STATE);
      LOG_INF("[CCC] indication_ccc_changed: %d", peripheral_state.indications);
      break;
    case throughput_gattdb_char_notification:
      if (value == BT_GATT_CCC_NOTIFY) {
        peripheral_state.notifications = BT_GATT_CCC_NOTIFY;
      }
      bt_evt_external_signal(EXTSIGNAL_EVENT_THROUGHPUT_NOTIFICATION_CCC_CHANGED
                             | EXTSIGNAL_EVENT_REFRESH_CONNECTED_STATE);
      LOG_INF("[CCC] notification_ccc_changed: %d",
              peripheral_state.notifications);
      break;
    case throughput_gattdb_char_transmission_on:
      transmission_indicated = (value == BT_GATT_CCC_NOTIFY);
      bt_evt_external_signal(EXTSIGNAL_EVENT_REFRESH_CONNECTED_STATE);
      LOG_INF("[CCC] transmission_on_ccc_changed: %d", transmission_indicated);
      break;
    case throughput_gattdb_char_throughput_result:
      result_indicated = (value == BT_GATT_CCC_INDICATE);
      bt_evt_external_signal(EXTSIGNAL_EVENT_REFRESH_CONNECTED_STATE);
      LOG_INF("[CCC] throughput_result_ccc_changed: %d", result_indicated);
      break;
    case throughput_gattdb_char_connection_phy:
      ccc_enable.notify.connection_phy = (value == BT_GATT_CCC_NOTIFY);
      bt_evt_external_signal(EXTSIGNAL_EVENT_PHY_STATE_CCC_CHANGED);
      LOG_INF("[CCC] connection_phy_ccc_changed: %d",
              ccc_enable.notify.connection_phy);
      break;
    case throughput_gattdb_char_connection_interval:
      ccc_enable.notify.connection_interval = (value == BT_GATT_CCC_NOTIFY);
      LOG_INF("[CCC] connection_interval_ccc_changed: %d",
              ccc_enable.notify.connection_interval);
      break;
    case throughput_gattdb_char_responder_latency:
      ccc_enable.notify.responder_latency = (value == BT_GATT_CCC_NOTIFY);
      LOG_INF("[CCC] responder_latency_ccc_changed: %d",
              ccc_enable.notify.responder_latency);
      break;
    case throughput_gattdb_char_supervision_timeout:
      ccc_enable.notify.supervision_timeout = (value == BT_GATT_CCC_NOTIFY);
      LOG_INF("[CCC] supervision_timeout_ccc_changed: %d",
              ccc_enable.notify.supervision_timeout);
      break;
    case throughput_gattdb_char_pdu_size:
      ccc_enable.notify.pdu_size = (value == BT_GATT_CCC_NOTIFY);
      LOG_INF("[CCC] pdu_size_ccc_changed: %d", ccc_enable.notify.pdu_size);
      break;
    case throughput_gattdb_char_mtu_size:
      ccc_enable.notify.mtu_size = (value == BT_GATT_CCC_NOTIFY);
      bt_evt_external_signal(EXTSIGNAL_EVENT_MTU_SIZE_CCC_CHANGED);
      LOG_INF("[CCC] mtu_size_ccc_changed: %d", ccc_enable.notify.mtu_size);
      break;
  }
}

/**************************************************************************//**
 * Calculates and sets the indication and notification data size
 *****************************************************************************/
static void throughput_peripheral_calculate_data_size(void)
{
  throughput_peripheral_calculate_indication_size();
  throughput_peripheral_calculate_notification_size();
  if (peripheral_state.test_type & BT_GATT_CCC_INDICATE) {
    peripheral_state.data_size = indication_data_size;
  } else {
    peripheral_state.data_size = notification_data_size;
  }
  throughput_peripheral_on_state_change(peripheral_state.state);
}

/**************************************************************************//**
 * Refresh throughput state
 *****************************************************************************/
static void throughput_peripheral_refresh_connected_state(void)
{
  if (((peripheral_state.notifications == BT_GATT_CCC_NOTIFY)
       || (peripheral_state.indications == BT_GATT_CCC_INDICATE))
      && (result_indicated != 0)
      && (transmission_indicated != 0)) {
    peripheral_state.state = THROUGHPUT_STATE_SUBSCRIBED;
    LOG_INF("[STATUS] SUBSCRIBED");
  } else {
    peripheral_state.state = THROUGHPUT_STATE_CONNECTED;
    LOG_INF("[STATUS] CONNECTED");
  }
}

/**************************************************************************//**
 * Starts advertising on both channels if possible
 *****************************************************************************/
static void throughput_peripheral_advertising_start(void)
{
  int err;

  struct bt_le_adv_param adv_param = {
    .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_SCANNABLE,
    .interval_min = 200,
    .interval_max = 200,
  };
  // Stop running advertising.
  err = bt_le_adv_stop();
  if (err) {
    LOG_ERR(" Stop advertise failed (err %d)", err);
  }

  // Convert power to mdBm
  int8_t tx_power = (((int16_t)peripheral_state.tx_power_requested) * 10);
  err = set_tx_power(tx_power);
  if (err) {
    LOG_ERR(" Set Tx power failed (err %d)", err);
  }
  tx_power = 0;
  err = get_tx_power(&tx_power);
  if (err) {
    tx_power = 0;
    LOG_ERR(" Set Tx power failed (err %d)", err);
  }
  peripheral_state.tx_power = tx_power / 10;
  throughput_peripheral_on_power_change(peripheral_state.tx_power);

  err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
  if (err) {
    LOG_ERR(" Start advertise failed (err %d)", err);
  }
}

/**************************************************************************//**
 * Calculate optimal notification size given current PDU and MTU sizes.
 *****************************************************************************/
static void throughput_peripheral_calculate_notification_size(void)
{
  if ((requested_notification_size == 0)
      || (requested_notification_size
          > (peripheral_state.mtu_size - NOTIFICATION_GATT_HEADER))) {
    if ((peripheral_state.pdu_size != 0) && (peripheral_state.mtu_size != 0)) {
      // Optimally split over multiple over-the-air packets.
      if (peripheral_state.pdu_size <= peripheral_state.mtu_size) {
        notification_data_size = (peripheral_state.pdu_size
                                  - (L2CAP_HEADER + NOTIFICATION_GATT_HEADER))
                                 + ((peripheral_state.mtu_size
                                     - NOTIFICATION_GATT_HEADER
                                     - peripheral_state.pdu_size + (L2CAP_HEADER
                                                                    +
                                                                    NOTIFICATION_GATT_HEADER))
                                    / peripheral_state.pdu_size
                                    * peripheral_state.pdu_size);
      } else {
        // Single over-the-air packet, but accommodate room for headers.
        if ((peripheral_state.pdu_size - peripheral_state.mtu_size)
            <= L2CAP_HEADER) {
          // LL PDU size - (L2CAP+GATT Headers)
          notification_data_size = peripheral_state.pdu_size
                                   - (L2CAP_HEADER + NOTIFICATION_GATT_HEADER);
        } else {
          // Room for the whole MTU, so data payload is MTU - Header of
          //   operation.
          notification_data_size = peripheral_state.mtu_size
                                   - NOTIFICATION_GATT_HEADER;
        }
      }
    }
  } else {
    notification_data_size = requested_notification_size;
  }
  LOG_INF("notification_data_size: %d", notification_data_size);
}

/**************************************************************************//**
 * Calculate indication size given current MTU size.
 *****************************************************************************/
static void throughput_peripheral_calculate_indication_size(void)
{
  // MTU - 3B for indication GATT operation header.
  if ((requested_indication_size == 0)
      || (requested_indication_size
          > (peripheral_state.mtu_size - INDICATION_GATT_HEADER))) {
    // If larger than max, use max for operation.
    indication_data_size = peripheral_state.mtu_size - INDICATION_GATT_HEADER;
  } else {
    // If smaller, use given.
    indication_data_size = requested_indication_size;
  }
}

/**************************************************************************//**
 * Reset peripheral configuration
 *****************************************************************************/
static void throughput_peripheral_reset(void)
{
  ble_conn = NULL;

  k_timer_stop(&refresh_timer);
  k_timer_stop(&send_timer);

  peripheral_state.notifications = 0;
  peripheral_state.indications = 0;
  result_indicated = 0;
  transmission_indicated = 0;
}

void transmission_on_char_write_handle(void)
{
  bool start = false;

  if (transmission_on > 0) {
    if (peripheral_state.state == THROUGHPUT_STATE_SUBSCRIBED) {
      peripheral_state.test_type = 0;
      if (peripheral_state.notifications && peripheral_state.indications) {
        if ((peripheral_state.notifications & BT_GATT_CCC_NOTIFY)
            && (transmission_on & BT_GATT_CCC_NOTIFY)) {
          peripheral_state.test_type = BT_GATT_CCC_NOTIFY;
        } else if ((peripheral_state.indications & BT_GATT_CCC_INDICATE)
                   && (transmission_on & BT_GATT_CCC_INDICATE)) {
          peripheral_state.test_type = BT_GATT_CCC_INDICATE;
        }
      } else if (peripheral_state.indications & BT_GATT_CCC_INDICATE) {
        peripheral_state.test_type = BT_GATT_CCC_INDICATE;
      } else if (peripheral_state.notifications & BT_GATT_CCC_NOTIFY) {
        peripheral_state.test_type = BT_GATT_CCC_NOTIFY;
      }
      if (peripheral_state.test_type & BT_GATT_CCC_INDICATE) {
        throughput_peripheral_generate_indications_data();
        start = true;
      } else if (peripheral_state.test_type & BT_GATT_CCC_NOTIFY) {
        throughput_peripheral_generate_notifications_data();
        start = true;
      }
      if (start) {
        handle_throughput_peripheral_start(false);
      }
    }
  } else {
    if (peripheral_state.state == THROUGHPUT_STATE_TEST) {
      handle_throughput_peripheral_stop(false);
    }
  }
}

static int throughput_information_notify(struct bt_conn *conn,
                                         const struct bt_uuid *uuid,
                                         const void *data, uint16_t len)
{
  if (!conn) {
    LOG_ERR("Invalid connection handle");
    return -1;
  }
  return bt_gatt_notify_uuid(conn,
                             uuid,
                             throughput_information_svc.attrs,
                             data,
                             len);
}

static int throughput_test_notify(struct bt_conn *conn,
                                  const struct bt_uuid *uuid,
                                  const void *data, uint16_t len)
{
  if (!conn) {
    LOG_ERR("Invalid connection handle");
    return -1;
  }
  return bt_gatt_notify_uuid(conn,
                             uuid,
                             throughput_test_svc.attrs,
                             data,
                             len);
}

static void indication_ccc_confirmed(struct bt_conn *conn,
                                     struct bt_gatt_indicate_params *params,
                                     uint8_t err)
{
  if (err == 0) {
    indication_confirmed = true;
  } else {
    LOG_ERR("[INDICATION] confirm error (err = %d)", err);
  }
}

static void throughput_result_ccc_confirmed(struct bt_conn *conn,
                                            struct bt_gatt_indicate_params *params,
                                            uint8_t err)
{
  if (err == 0) {
    indication_confirmed = true;
    LOG_INF("[INDICATION] throughput_result confirm OK");
  } else {
    LOG_ERR("[INDICATION] throughput_result confirm error (err = %d)", err);
  }
}

static int throughput_test_indicate(struct bt_conn *conn,
                                    const struct bt_uuid *uuid,
                                    struct bt_gatt_indicate_params *indicate_params,
                                    bt_gatt_indicate_func_t callback,
                                    const void *data,
                                    uint16_t len)
{
  struct bt_gatt_attr *attr;

  if (!conn) {
    LOG_ERR("Invalid connection handle");
    return -1;
  }
  attr = bt_gatt_find_by_uuid(throughput_test_svc.attrs,
                              throughput_test_svc.attr_count,
                              uuid);
  if (!attr) {
    LOG_ERR("UUID: %s not found", uuid_to_str(uuid));
    return -1;
  }
  indicate_params->attr = attr;
  indicate_params->data = data;
  indicate_params->len = len;
  indicate_params->func = callback;

  return bt_gatt_indicate(conn, indicate_params);
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
        transmission_on_sub.value_handle = value_handle;
        LOG_INF("Discover transmission_on_char handle %d", handle);
      }

      if (characteristic_found.all == 7) {
        LOG_INF("Subcribe to characteristic");
        action = act_enable_notification;
        bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
      }
      break;

    default:
      break;
  }
}

static void throughput_peripheral_event_handle(struct bt_evt *evt)
{
  int err = 0;

  switch (evt->id) {
    case  bt_evt_connection_opened_id:
      if (peripheral_state.state == THROUGHPUT_STATE_DISCONNECTED) {
        err = evt->data.connection_opened.err;
        if (err) {
          LOG_WRN("Connection failed (err %u)", err);
        } else {
          LOG_INF("Connected");
          if (!ble_conn) {
            // Store the connection and disable advertising
            ble_conn = bt_conn_ref(evt->data.connection_opened.conn);
            // ble_conn = evt->data.connection_opened.conn;
            bt_le_adv_stop();

            struct bt_conn_info info;
            err = bt_conn_get_info(evt->data.connection_opened.conn, &info);
            if (err == 0) {
              peripheral_state.pdu_size = info.le.data_len->tx_max_len;
              peripheral_state.mtu_size = bt_gatt_get_mtu(
                evt->data.connection_opened.conn);
              throughput_peripheral_calculate_data_size();
              LOG_INF("[CONN] pdu_size = %d", peripheral_state.pdu_size);
              LOG_INF("[CONN] mtu_size = %d", peripheral_state.mtu_size);

              peripheral_state.interval = info.le.interval;
              peripheral_state.connection_responder_latency = info.le.latency;
              peripheral_state.connection_timeout = info.le.timeout;

              peripheral_state.phy = info.le.phy->tx_phy;
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

            peripheral_state.state = THROUGHPUT_STATE_CONNECTED;
            throughput_peripheral_refresh_connected_state();

            // Refresh timer is periodic timer
            k_timer_start(&refresh_timer,
                          K_MSEC(THROUGHPUT_TX_REFRESH_TIMER_PERIOD),
                          K_MSEC(THROUGHPUT_TX_REFRESH_TIMER_PERIOD));

#if defined(CONFIG_BT_TRANSMIT_POWER_CONTROL)
            // Set remote connection power reporting - needed for Power Control
            err = bt_conn_le_set_tx_power_report_enable(
              evt->data.connection_opened.conn,
              true,
              power_control_enabled);
            if (err) {
              LOG_ERR(" Set power reporting failed (err %d)", err);
            }
#endif /* CONFIG_BT_TRANSMIT_POWER_CONTROL */
            // Discover & subscribe to service provided by the mobile app
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
        }
      }
      break;

    case  bt_evt_connection_closed_id:
      if (ble_conn) {
        bt_conn_unref(ble_conn);
        ble_conn = NULL;
        if (peripheral_state.state == THROUGHPUT_STATE_UNINITALIZING) {
          enabled = false;
          // Delete the connection, reset variables
          throughput_peripheral_reset();

          peripheral_state.state = THROUGHPUT_STATE_UNINITALIZED;
        } else {
          peripheral_state.state = THROUGHPUT_STATE_DISCONNECTED;
          // Delete the connection, reset variables and start advertising
          throughput_peripheral_reset();
          throughput_peripheral_advertising_start();
        }
      }
      LOG_INF("Disconnected (reason %u)", evt->data.connection_closed.reason);
      throughput_peripheral_on_state_change(peripheral_state.state);
      break;

    case bt_evt_le_param_updated_id:
      peripheral_state.interval = evt->data.le_param_updated.interval;
      peripheral_state.connection_responder_latency =
        evt->data.le_param_updated.latency;
      peripheral_state.connection_timeout = evt->data.le_param_updated.timeout;

      if (ccc_enable.notify.connection_interval) {
        LOG_INF("[NOTIFY] connection_interval: %d",
                peripheral_state.interval);
        err = throughput_information_notify(evt->data.le_param_updated.conn,
                                            &connection_interval_char_uuid.uuid,
                                            &peripheral_state.interval, 4);
        if (err != 0) {
          LOG_WRN("Notify failed (err %d)", err);
        }
      }
      if (ccc_enable.notify.supervision_timeout) {
        LOG_INF("[NOTIFY] supervision_timeout: %d",
                peripheral_state.connection_timeout);
        err = throughput_information_notify(evt->data.le_param_updated.conn,
                                            &supervision_timeout_char_uuid.uuid,
                                            &peripheral_state.connection_timeout,
                                            4);
        if (err != 0) {
          LOG_WRN("Notify failed (err %d)", err);
        }
      }
      if (ccc_enable.notify.responder_latency) {
        LOG_INF("[NOTIFY] responder_latency: %d",
                peripheral_state.connection_responder_latency);
        err = throughput_information_notify(evt->data.le_param_updated.conn,
                                            &responder_latency_char_uuid.uuid,
                                            &peripheral_state.connection_responder_latency,
                                            4);
        if (err != 0) {
          LOG_WRN("Notify failed (err %d)", err);
        }
      }

      throughput_peripheral_on_connection_settings_change(
        peripheral_state.interval,
        peripheral_state.pdu_size,
        peripheral_state.mtu_size,
        peripheral_state.data_size);
      break;

    case bt_evt_le_phy_updated_id:
      peripheral_state.phy = evt->data.le_phy_updated.tx_phy;
      if (ccc_enable.notify.connection_phy) {
        LOG_INF("[NOTIFY] connection_phy: %d", peripheral_state.phy);
        err = throughput_information_notify(evt->data.le_phy_updated.conn,
                                            &connection_phy_char_uuid.uuid,
                                            &peripheral_state.phy, 1);
        if (err != 0) {
          LOG_WRN("Notify failed (err %d)", err);
        }
      }
      throughput_peripheral_on_phy_change(peripheral_state.phy);
      break;

    case bt_evt_le_data_len_updated_id:
      if (peripheral_state.pdu_size
          != evt->data.le_data_len_updated.tx_max_len) {
        LOG_INF("tx_max_len %d", evt->data.le_data_len_updated.tx_max_len);
        peripheral_state.pdu_size = evt->data.le_data_len_updated.tx_max_len;
        throughput_peripheral_calculate_data_size();
        LOG_INF("[CONN] pdu_size = %d", peripheral_state.pdu_size);
        LOG_INF("[CONN] mtu_size = %d", peripheral_state.mtu_size);
        if (ccc_enable.notify.pdu_size) {
          LOG_INF("[NOTIFY] pdu_size");
          err = throughput_information_notify(
            evt->data.le_data_len_updated.conn,
            &pdu_size_char_uuid.uuid,
            &peripheral_state.pdu_size,
            1);
          if (err != 0) {
            LOG_WRN("Notify failed (err %d)", err);
          }
        }
        throughput_peripheral_on_connection_settings_change(
          peripheral_state.interval,
          peripheral_state.pdu_size,
          peripheral_state.mtu_size,
          peripheral_state.data_size);
      }
      break;

#if defined(CONFIG_BT_TRANSMIT_POWER_CONTROL)
    case bt_evt_tx_power_report_id:
      peripheral_state.tx_power = evt->data.tx_power_report.tx_power_level;
      peripheral_state.phy = evt->data.tx_power_report.phy;

      if (peripheral_state.state != THROUGHPUT_STATE_TEST) {
        // peripheral_state.tx_power = evt->data.tx_power_report.tx_power_level;
        throughput_peripheral_on_power_change(peripheral_state.tx_power);
      }

      if (ccc_enable.notify.connection_phy) {
        LOG_INF("[NOTIFY] connection_phy: %d", peripheral_state.phy);
        err = throughput_information_notify(evt->data.tx_power_report.conn,
                                            &connection_phy_char_uuid.uuid,
                                            &peripheral_state.phy, 1);
        if (err != 0) {
          LOG_WRN("Notify failed (err %d)", err);
        }
      }
      break;
#endif /* CONFIG_BT_TRANSMIT_POWER_CONTROL */

    case bt_evt_exchange_mtu_id:
      peripheral_state.mtu_size = bt_gatt_get_mtu(evt->data.exchange_mtu.conn);
      throughput_peripheral_calculate_data_size();
      LOG_INF("[CONN] pdu_size = %d", peripheral_state.pdu_size);
      LOG_INF("[CONN] mtu_size = %d", peripheral_state.mtu_size);

      if (ccc_enable.notify.mtu_size) {
        LOG_INF("[NOTIFY] mtu_size: %d", peripheral_state.mtu_size);
        err = throughput_information_notify(evt->data.exchange_mtu.conn,
                                            &mtu_size_char_uuid.uuid,
                                            &peripheral_state.mtu_size, 1);
        if (err != 0) {
          LOG_WRN("Notify failed (err %d)", err);
        }
      }
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
            action = act_enable_indication;
            bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
            break;
          case act_enable_indication:
            action = act_enable_transmission_notification;
            bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
            break;
          case act_enable_transmission_notification:
            // Clear the display
            throughput_ui_set_throughput(0);
            throughput_ui_set_count(0);
            throughput_ui_update();
            action = act_none;
            break;
          default:
          case act_none:
            break;
        }
      }
      break;

    case bt_evt_gatt_characteristic_notification_id:
      if (evt->data.gatt_characteristic_notification.value_handle
          == notification_sub.value_handle) {
        // Check data for loss or error
        check_received_data(
          evt->data.gatt_characteristic_notification.value.data,
          evt->data.gatt_characteristic_notification.value.len);
        // Count bytes and operation
        bytes_sent += evt->data.gatt_characteristic_notification.value.len;
        operation_count++;
      } else if (evt->data.gatt_characteristic_notification.value_handle
                 == transmission_on_sub.value_handle) {
        if (evt->data.gatt_characteristic_notification.value.data[0]) {
          LOG_INF("[MODE] Central test");
          central_test = true;
          handle_throughput_peripheral_start(false);
        } else {
          handle_throughput_peripheral_stop(false);
          central_test = false;
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
        bytes_sent += evt->data.gatt_characteristic_indication.value.len;
        operation_count++;
      }
      break;

    case bt_evt_system_external_signal_id:
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_REFRESH_CONNECTED_STATE) {
        throughput_peripheral_refresh_connected_state();
      }
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_REFRESH_TIMER_RISE) {
        if (ble_conn && (peripheral_state.state != THROUGHPUT_STATE_TEST)) {
          int8_t rssi;

          err = read_conn_rssi(ble_conn, &rssi);
          if (err == 0) {
            peripheral_state.rssi = rssi;
            throughput_peripheral_on_rssi_change(peripheral_state.rssi);
          } else {
            LOG_WRN("Failed to get RSSI (err = %d)", err);
          }
        }
      }
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_TRANSMISSION_ON_CHAR_WRITE) {
        LOG_INF("[WRITE] transmission_on = %d", transmission_on);
        transmission_on_char_write_handle();
      }
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_PHY_STATE_CCC_CHANGED) {
        if (ccc_enable.notify.connection_phy) {
          LOG_INF("[NOTIFY] connection_phy: %d", peripheral_state.phy);
          err = throughput_information_notify(ble_conn,
                                              &connection_phy_char_uuid.uuid,
                                              &peripheral_state.phy, 1);
          if (err != 0) {
            LOG_WRN("Notify failed (err %d)", err);
          }
        }
      }
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_MTU_SIZE_CCC_CHANGED) {
        if (ccc_enable.notify.mtu_size) {
          LOG_INF("[NOTIFY] mtu_size: %d", peripheral_state.mtu_size);
          err = throughput_information_notify(ble_conn,
                                              &mtu_size_char_uuid.uuid,
                                              &peripheral_state.mtu_size, 1);
          if (err != 0) {
            LOG_WRN("Notify failed (err %d)", err);
          }
        }
      }
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_THROUGHPUT_NOTIFICATION_CCC_CHANGED) {
        throughput_peripheral_on_notification_change(
          peripheral_state.notifications);
      }
      if (evt->data.system_external_signal.extsignals
          & EXTSIGNAL_EVENT_THROUGHPUT_INDICATION_CCC_CHANGED) {
        throughput_peripheral_on_indication_change(peripheral_state.indications);
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
              break;
            }
            break;

          default:
            break;
        }

        // Wait and retry when queue is full
        if (err == -ENOMEM) {
          k_sleep(K_MSEC(500));
          bt_evt_external_signal(EXTSIGNAL_EVENT_CHARACTERISTIC_SUBSCRIBE);
        }
      }
      break;

    default:
      break;
  }
}

static void throughput_peripheral_event_poll(void)
{
  struct bt_evt event;

  int err = bt_evt_wait_event(&event, K_NO_WAIT);

  if (!err) {
    throughput_peripheral_event_handle(&event);
  }
}

/**************************************************************************//**
 * Starts throughput test.
 *****************************************************************************/
static void handle_throughput_peripheral_start(bool send_transmission_on)
{
  int err;

  // Clear transmission variables
  bytes_sent = 0;
  send_counter = 0;
  peripheral_state.throughput = 0;
  peripheral_state.count = 0;
  operation_count = 0;

  // Clear reception variables
  received_counter = 0;
  first_packet = true;
  peripheral_state.packet_error = 0;
  peripheral_state.packet_lost = 0;

  // Clear flags
  indication_timer_rised = false;
  send_timer_rised = false;

  indication_sent = false;
  indication_confirmed = false;

  // Stop timers
  k_timer_stop(&indication_timer);
  k_timer_stop(&send_timer);
  k_timer_stop(&refresh_timer);

  // Generate data to send
  if (peripheral_state.test_type & BT_GATT_CCC_NOTIFY) {
    throughput_peripheral_generate_notifications_data();
  }
  if (peripheral_state.test_type & BT_GATT_CCC_INDICATE) {
    throughput_peripheral_generate_indications_data();
  }
  if (send_transmission_on) {
    err = throughput_test_notify(ble_conn,
                                 &transmission_on_char_uuid.uuid,
                                 &TRANSMISSION_ON, 1);
    if (err != 0) {
      LOG_WRN("Notify failed (err %d)", err);
    }
  }

  if (peripheral_state.mode == THROUGHPUT_MODE_FIXED_TIME) {
    LOG_INF("[MODE] Fixed time: %d", fixed_time);
    k_timer_start(&send_timer, K_MSEC(fixed_time), K_NO_WAIT);
  }

  peripheral_state.state = THROUGHPUT_STATE_TEST;
  throughput_peripheral_on_state_change(peripheral_state.state);
  throughput_peripheral_on_start();

  // Start timer
  time_start = k_uptime_get_32();

  if (central_test) {
    LOG_INF("[CENTRAL] Test started");
  } else {
    LOG_INF("[PERIPHERAL] Test started");
  }
}

/**************************************************************************//**
 * Finishes throughput test.
 *****************************************************************************/
static void handle_throughput_peripheral_stop(bool send_transmission_on)
{
  int err;

  // If first called finish
  if (peripheral_state.state != THROUGHPUT_STATE_TEST_FINISH) {
    // Set state to finish
    peripheral_state.state = THROUGHPUT_STATE_TEST_FINISH;
    LOG_INF("[STATUS] TEST_FINISH");
    // Test type off state
    peripheral_state.test_type = 0;

    // stop timer
    k_timer_stop(&indication_timer);

    send_transmission_state = send_transmission_on;

    notification_sent = false;
    indication_sent = false;
    indication_confirmed = false;
    send_timer_rised = false;
    indication_timer_rised = false;

    finish_test = false;
    LOG_INF("Test stopped");
  }

  if (send_transmission_on && !notification_sent) {
    // Send out notification
    err = throughput_test_notify(ble_conn,
                                 &transmission_on_char_uuid.uuid,
                                 &TRANSMISSION_OFF, 1);
    if (err == 0) {
      notification_sent = true;
      indication_sent = false;
    }
  } else if (!send_transmission_on || notification_sent) {
    if (!indication_sent) {
      // Get elapsed time
      time_elapsed = k_uptime_get_32() - time_start;
      peripheral_state.count = operation_count;

      peripheral_state.time = (throughput_time_t)(time_elapsed);

      // Calculate throughput
      peripheral_state.throughput =
        (throughput_value_t)((float)bytes_sent * 8 * 1000
                             / ((float)time_elapsed));

      indication_confirmed = false;
      indication_timer_rised = false;

      err = throughput_test_indicate(ble_conn,
                                     &throughput_result_char_uuid.uuid,
                                     &indication_indicate_params,
                                     throughput_result_ccc_confirmed,
                                     &(peripheral_state.throughput),
                                     sizeof(peripheral_state.throughput));
      if (err == 0) {
        indication_sent = true;
        k_timer_start(&indication_timer,
                      K_MSEC(THROUGHPUT_TX_INDICATION_TIMEOUT),
                      K_NO_WAIT);
      }
    } else {
      if (indication_confirmed || indication_timer_rised) {
        // Set mode
        throughput_peripheral_refresh_connected_state();

        notification_sent = false;
        indication_sent = false;
        indication_confirmed = false;
        send_timer_rised = false;
        indication_timer_rised = false;

        // Start refresh timer
        // Refresh timer is periodic timer
        k_timer_start(&refresh_timer,
                      K_MSEC(THROUGHPUT_TX_REFRESH_TIMER_PERIOD),
                      K_MSEC(THROUGHPUT_TX_REFRESH_TIMER_PERIOD));

        // Indicate the state change
        if (central_test) {
          throughput_peripheral_on_finish(peripheral_state.throughput,
                                          peripheral_state.count);
        } else {
          throughput_peripheral_on_finish_reception(peripheral_state.throughput,
                                                    peripheral_state.count,
                                                    peripheral_state.packet_lost,
                                                    peripheral_state.packet_error,
                                                    peripheral_state.time);
        }
      }
    }
  }
}

/**************************************************************************//**
 * Sends out single notification for the test.
 *****************************************************************************/
static void throughput_peripheral_send_notification(void)
{
  int err;

  if (finish_test) {
    handle_throughput_peripheral_stop(true);
  } else {
    if (send_timer_rised) {
      send_timer_rised = false;
      handle_throughput_peripheral_stop(true);
    } else {
      err = throughput_test_notify(ble_conn,
                                   &notification_char_uuid.uuid,
                                   notification_data, notification_data_size);
      if (err == 0) {
        bytes_sent += (notification_data_size);
        operation_count++;
        throughput_peripheral_generate_notifications_data();
        if ((peripheral_state.mode == THROUGHPUT_MODE_FIXED_LENGTH)
            && (bytes_sent >= (fixed_data_size))) {
          handle_throughput_peripheral_stop(true);
        }
      }
    }
  }
}

/**************************************************************************//**
 * Sends out single indication for the test.
 *****************************************************************************/
static void throughput_peripheral_send_indication(void)
{
  int err;

  if (indication_sent) {
    if (indication_confirmed) {
      // move on.
      bytes_sent += (indication_data_size);
      operation_count++;

      k_timer_stop(&indication_timer);

      indication_sent = false;
      indication_confirmed = false;

      if ((peripheral_state.mode == THROUGHPUT_MODE_FIXED_LENGTH)
          && (bytes_sent >= (fixed_data_size))) {
        handle_throughput_peripheral_stop(true);
      } else if (send_timer_rised) {
        send_timer_rised = false;
        handle_throughput_peripheral_stop(true);
      }
    } else {
      if (indication_timer_rised) {
        handle_throughput_peripheral_stop(true);
      }
    }
  } else {
    // No indication sent, send it out
    if (finish_test) {
      handle_throughput_peripheral_stop(true);
    } else {
      throughput_peripheral_generate_indications_data();

      indication_confirmed = false;

      k_timer_stop(&indication_timer);

      err = throughput_test_indicate(ble_conn,
                                     &indication_char_uuid.uuid,
                                     &throughput_result_indicate_params,
                                     indication_ccc_confirmed,
                                     indication_data,
                                     indication_data_size);

      if (err == 0) {
        indication_sent = true;
        k_timer_start(&indication_timer,
                      K_MSEC(THROUGHPUT_TX_INDICATION_TIMEOUT),
                      K_NO_WAIT);
      }
    }
  }
}

/**************************************************************************//**
 * Function to generate payload
 *****************************************************************************/
static void throughput_peripheral_generate_notifications_data(void)
{
  notification_data[0] = send_counter;
  for (int i = 1; i < notification_data_size; i++) {
    notification_data[i] = (uint8_t) 'a' + (uint8_t) ((i - 1) % 26);
  }
  send_counter = (send_counter + 1) % 100;
}

/**************************************************************************//**
 * Function to generate payload
 *****************************************************************************/
static void throughput_peripheral_generate_indications_data(void)
{
  indication_data[0] = send_counter;
  for (int i = 1; i < indication_data_size; i++) {
    indication_data[i] = (uint8_t) 'a' + (uint8_t) ((i - 1) % 26);
  }
  send_counter = (send_counter + 1) % 100;
}

/**************************************************************************//**
 * Send timer callback.
 *****************************************************************************/
static void throughput_peripheral_on_send_timer_rise(struct k_timer *timer)
{
  (void) timer;
  send_timer_rised = true;
}

/**************************************************************************//**
 * Refresh RSSI timer callback.
 *****************************************************************************/
static void throughput_peripheral_on_refresh_timer_rise(struct k_timer *timer)
{
  (void) timer;
  bt_evt_external_signal(EXTSIGNAL_EVENT_REFRESH_TIMER_RISE);
}

/**************************************************************************//**
 * Indication timer callback.
 *****************************************************************************/
static void throughput_peripheral_on_indication_timer_rise(
  struct k_timer *timer)
{
  (void) timer;
  indication_timer_rised = true;
}

/***************************************************************************//**
 * Checks received data for lost or error packages
 * @param[in] data received data
 * @param[in] len length of the data
 ******************************************************************************/
static void check_received_data(uint8_t *data, uint8_t len)
{
  uint8_t counter;

  if (len == 0) {
    peripheral_state.packet_error++;
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
    peripheral_state.packet_lost += lost_packet_count;
    // Sync to remote counter
    received_counter = counter;
  }

  // Check data for bit errors
  for (int i = 1; i < len; i++) {
    if (data[i] != (uint8_t) 'a' + (uint8_t) ((i - 1) % 26)) {
      peripheral_state.packet_error++;
      break;
    }
  }
}

/*******************************************************************************
 **************************   GLOBAL FUNCTIONS   *******************************
 ******************************************************************************/
void throughput_peripheral_enable(void)
{
  int err;

  throughput_gattdb_register_char_read_cb(on_char_read);
  throughput_gattdb_register_char_write_cb(on_char_write);
  throughput_gattdb_register_ccc_changed_cb(ccc_changed);

  // Enable UI
  throughput_ui_init();

  memset(notification_data, 0, THROUGHPUT_TX_DATA_SIZE);
  memset(indication_data, 0, THROUGHPUT_TX_DATA_SIZE);

  peripheral_state.role = THROUGHPUT_ROLE_PERIPHERAL;
  peripheral_state.state = THROUGHPUT_STATE_DISCONNECTED;
  peripheral_state.mode = THROUGHPUT_PERIPHERAL_MODE_DEFAULT;
  peripheral_state.tx_power = THROUGHPUT_PERIPHERAL_TX_POWER;
  peripheral_state.rssi = 0;
  peripheral_state.phy = BT_CONN_LE_TX_POWER_PHY_1M;
  peripheral_state.interval = 0;
  peripheral_state.pdu_size = 0;
  peripheral_state.mtu_size = THROUGHPUT_PERIPHERAL_MTU_SIZE;
  peripheral_state.data_size = 0;
  peripheral_state.notifications = 0;
  peripheral_state.indications = 0;
  peripheral_state.throughput = 0;
  peripheral_state.count = 0;
  peripheral_state.packet_error = 0;
  peripheral_state.packet_lost = 0;

  peripheral_state.test_type = 0;

  if (THROUGHPUT_PERIPHERAL_TX_POWER_CONTROL_ENABLE) {
    power_control_enabled = true;
  } else {
    power_control_enabled = false;
  }

  // Convert power to mdBm
  int8_t tx_power = (((int16_t)peripheral_state.tx_power_requested) * 10);
  err = set_tx_power(tx_power);
  if (err) {
    LOG_ERR(" Set Tx power failed (err %d)", err);
  }
  tx_power = 0;
  err = get_tx_power(&tx_power);
  if (err) {
    tx_power = 0;
    LOG_ERR(" Set Tx power failed (err %d)", err);
  }
  peripheral_state.tx_power = tx_power / 10;
  throughput_peripheral_on_power_change(peripheral_state.tx_power);

  // Init bluetooth event module
  bt_evt_init();

  // Start advertising
  throughput_peripheral_advertising_start();

  enabled = true;

  throughput_ui_set_all(peripheral_state);
  central_test = false;
}

/**************************************************************************//**
 * Disables the transmission.
 *****************************************************************************/
int throughput_peripheral_disable(void)
{
  int err = 0;

  if (peripheral_state.state == THROUGHPUT_STATE_TEST) {
    LOG_WRN("INVALID STATE");
    return -1;
  }

  err = bt_le_adv_stop();
  if (err) {
    LOG_WRN("Stop advertising failed (err %d)", err);
    return err;
  }

  if (peripheral_state.state != THROUGHPUT_STATE_DISCONNECTED) {
    if (ble_conn) {
      // err = bt_gatt_unsubscribe(ble_conn, &notification_sub);
      // if (err != 0) {
      //   LOG_WRN("Unsubscribe failed (err %d)", err);
      // }
      // err = bt_gatt_unsubscribe(ble_conn, &indication_sub);
      // if (err != 0) {
      //   LOG_WRN("Unsubscribe failed (err %d)", err);
      // }
      // err = bt_gatt_unsubscribe(ble_conn, &transmission_on_sub);
      // if (err != 0) {
      //   LOG_WRN("Unsubscribe failed (err %d)", err);
      // }

      err = bt_conn_disconnect(ble_conn,
                               BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      if (err != 0) {
        LOG_WRN("Disconnect failed (err %d)", err);
      }
    }
    peripheral_state.state = THROUGHPUT_STATE_UNINITALIZING;
    LOG_INF("[STATUS] UNINITALIZING");
  } else {
    peripheral_state.state = THROUGHPUT_STATE_UNINITALIZED;
    LOG_INF("[STATUS] UNINITALIZED");
    enabled = false;
    throughput_peripheral_reset();
  }
  throughput_gattdb_register_char_read_cb(NULL);
  throughput_gattdb_register_char_write_cb(NULL);
  throughput_gattdb_register_ccc_changed_cb(NULL);
  throughput_peripheral_on_state_change(peripheral_state.state);
  return err;
}

/**************************************************************************//**
 * Process step for throughput peripheral.
 *****************************************************************************/
void throughput_peripheral_step(void)
{
  // poll for events
  if (enabled) {
    throughput_peripheral_event_poll();
  }

  // Return early, if the central started a test
  if (central_test) {
    return;
  }
  if (peripheral_state.state == THROUGHPUT_STATE_TEST) {
    if (peripheral_state.test_type & BT_GATT_CCC_INDICATE) {
      throughput_peripheral_send_indication();
    }
    if (peripheral_state.test_type & BT_GATT_CCC_NOTIFY) {
      throughput_peripheral_send_notification();
    }
  } else if (peripheral_state.state == THROUGHPUT_STATE_TEST_FINISH) {
    handle_throughput_peripheral_stop(send_transmission_state);
  }
}

/**************************************************************************//**
 * Starts the the transmission.
 *****************************************************************************/
int throughput_peripheral_start(throughput_notification_t type)
{
  int err = 0;

  if (enabled && (peripheral_state.state == THROUGHPUT_STATE_SUBSCRIBED)) {
    if ((peripheral_state.indications & BT_GATT_CCC_INDICATE)
        && (peripheral_state.notifications & BT_GATT_CCC_NOTIFY)
        && (type != 0)) {
      peripheral_state.test_type = BT_GATT_CCC_NOTIFY;
    }

    if ((type == BT_GATT_CCC_INDICATE)
        && (peripheral_state.indications & BT_GATT_CCC_INDICATE)) {
      peripheral_state.test_type = BT_GATT_CCC_INDICATE;
    } else if ((type == BT_GATT_CCC_NOTIFY)
               && (peripheral_state.notifications & BT_GATT_CCC_NOTIFY)) {
      peripheral_state.test_type = BT_GATT_CCC_NOTIFY;
    }

    if (peripheral_state.test_type != 0) {
      if (peripheral_state.test_type & BT_GATT_CCC_NOTIFY) {
        peripheral_state.data_size = notification_data_size;
      }
      if (peripheral_state.test_type & BT_GATT_CCC_INDICATE) {
        peripheral_state.data_size = indication_data_size;
      }
      throughput_peripheral_on_connection_settings_change(
        peripheral_state.interval,
        peripheral_state.pdu_size,
        peripheral_state.mtu_size,
        peripheral_state.data_size);
      handle_throughput_peripheral_start(true);
    } else {
      err = -1;
      LOG_WRN("Invalid test type: %d", peripheral_state.test_type);
    }
  } else {
    err = -1;
    LOG_WRN("Not in subscribed state");
  }
  return err;
}

/**************************************************************************//**
 * Stops the the transmission.
 *****************************************************************************/
int throughput_peripheral_stop(void)
{
  int err = 0;

  if (enabled && (peripheral_state.state == THROUGHPUT_STATE_TEST)) {
    finish_test = true;
  } else {
    err = -1;
  }
  return err;
}

/**************************************************************************//**
 * Sets the the transmission mode.
 *****************************************************************************/
int throughput_peripheral_set_mode(throughput_mode_t mode,
                                   uint32_t amount)
{
  int err = 0;
  if (enabled && (peripheral_state.state != THROUGHPUT_STATE_TEST)) {
    if (mode == THROUGHPUT_MODE_FIXED_LENGTH) {
      fixed_data_size = amount;
    } else if (mode == THROUGHPUT_MODE_FIXED_TIME) {
      fixed_time = amount;
    }
    peripheral_state.mode = mode;
  } else {
    err = -1;
  }
  return err;
}

/**************************************************************************//**
 * Sets the the transmission sizes.
 *****************************************************************************/
int throughput_peripheral_set_data_size(uint8_t mtu,
                                        uint8_t ind_data,
                                        uint8_t not_data)
{
  int err = 0;
  if (enabled && (peripheral_state.state != THROUGHPUT_STATE_TEST)) {
    peripheral_state.mtu_size = mtu;
    // err = sl_bt_gatt_server_set_max_mtu(peripheral_state.mtu_size,
    //   &(peripheral_state.mtu_size));

    if (err == 0) {
      requested_indication_size = ind_data;
      requested_notification_size = not_data;
      throughput_peripheral_calculate_data_size();
      // Reconnect if required
      if (peripheral_state.state != THROUGHPUT_STATE_DISCONNECTED) {
        err = bt_conn_disconnect(ble_conn,
                                 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err != 0) {
          LOG_WRN("Disconnect failed (err %d)", err);
        }
      }
    }
  } else {
    err = -1;
  }
  return err;
}

/**************************************************************************//**
 * Sets the transmission power.
 *****************************************************************************/
int throughput_peripheral_set_tx_power(throughput_tx_power_t tx_power,
                                       bool power_control,
                                       bool deep_sleep)
{
  int err = 0;
  if (enabled && (peripheral_state.state != THROUGHPUT_STATE_TEST)) {
    peripheral_state.tx_power_requested = tx_power;
    deep_sleep_enabled = deep_sleep;

    if (power_control) {
      power_control_enabled = 1;
    } else {
      power_control_enabled = 0;
    }

    // Reconnect if required
    if (peripheral_state.state != THROUGHPUT_STATE_DISCONNECTED) {
      // Close connection and apply power
      err = bt_conn_disconnect(ble_conn,
                               BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      if (err != 0) {
        LOG_WRN("Disconnect failed (err %d)", err);
      }
    } else {
      // Restart advertising and apply power
      throughput_peripheral_advertising_start();
    }
  } else {
    err = -1;
  }
  return err;
}

/*******************************************************************************
 **************************** CLI RELATED FUNCTIONS ****************************
 ******************************************************************************/
#ifdef CONFIG_SHELL
void cli_throughput_peripheral_stop(const struct shell *sh,
                                    size_t argc,
                                    char **argv)
{
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  int err;

  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }
  if (peripheral_state.state == THROUGHPUT_STATE_TEST) {
    err = throughput_peripheral_stop();
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Peripheral not in test state");
  }
}

void cli_throughput_peripheral_start(const struct shell *sh,
                                     size_t argc,
                                     char **argv)
{
  ARG_UNUSED(argc);
  int err;
  int type;

  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }

  if (peripheral_state.state == THROUGHPUT_STATE_SUBSCRIBED) {
    type = atoi(argv[1]);
    err = throughput_peripheral_start((throughput_notification_t)type);
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Peripheral not in test state");
  }
}

void cli_throughput_peripheral_status(const struct shell *sh,
                                      size_t argc,
                                      char **argv)
{
  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }
  switch (peripheral_state.state) {
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

  if (peripheral_state.role == THROUGHPUT_ROLE_PERIPHERAL) {
    shell_print(sh, THROUGHPUT_UI_ROLE_PERIPHERAL_TEXT);
  } else {
    shell_print(sh, THROUGHPUT_UI_ROLE_CENTRAL_TEXT);
  }

  shell_print(sh, "OK");
}

void cli_throughput_peripheral_mode_set(const struct shell *sh,
                                        size_t argc,
                                        char **argv)
{
  uint8_t mode;
  uint32_t amount;
  int err;

  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }

  if (peripheral_state.state != THROUGHPUT_STATE_TEST) {
    mode = atoi(argv[1]);
    amount = atoi(argv[2]);
    err = throughput_peripheral_set_mode((throughput_mode_t)mode, amount);
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Peripheral not in test state");
  }
}

void cli_throughput_peripheral_mode_get(const struct shell *sh,
                                        size_t argc,
                                        char **argv)
{
  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }
  shell_print(sh, "cli_throughput_peripheral_mode_get\n");
  shell_print(sh, "%d %u %u\n",
              (int)peripheral_state.mode,
              (uint32_t)fixed_data_size,
              (uint32_t)fixed_time);
}

void cli_throughput_peripheral_tx_power_set(const struct shell *sh,
                                            size_t argc,
                                            char **argv)
{
  int err;
  int16_t power;
  uint8_t control, sleep;

  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }
  if (peripheral_state.state != THROUGHPUT_STATE_TEST) {
    power = atoi(argv[1]);
    control = atoi(argv[2]);
    sleep = atoi(argv[3]);
    err = throughput_peripheral_set_tx_power(power,
                                             control,
                                             sleep);
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Peripheral not in test state");
  }
}

void cli_throughput_peripheral_tx_power_get(const struct shell *sh,
                                            size_t argc,
                                            char **argv)
{
  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }
  shell_print(sh, "cli_throughput_tx_power_get\n");
  shell_print(sh, "%d %d\n",
              (int)peripheral_state.tx_power,
              (int)power_control_enabled);
}

void cli_throughput_peripheral_data_set(const struct shell *sh,
                                        size_t argc,
                                        char **argv)
{
  uint8_t mtu, ind_data, not_data;
  int err;

  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }

  if (peripheral_state.state != THROUGHPUT_STATE_TEST) {
    mtu = atoi(argv[1]);
    ind_data = atoi(argv[2]);
    not_data = atoi(argv[3]);
    err = throughput_peripheral_set_data_size(mtu,
                                              ind_data,
                                              not_data);
    if (err == 0) {
      shell_print(sh, "OK");
    } else {
      shell_print(sh, "ERROR");
    }
  } else {
    shell_print(sh, "Peripheral not in test state");
  }
}

void cli_throughput_peripheral_data_get(const struct shell *sh,
                                        size_t argc,
                                        char **argv)
{
  if (!enabled) {
    shell_print(sh, "Peripheral not enabled");
    return;
  }
  shell_print(sh, "cli_throughput_peripheral_data_get\n");
  shell_print(sh, "%d %d %d\n",
              (int)peripheral_state.mtu_size,
              (int)indication_data_size,
              (int)notification_data_size);
}

#endif

/*******************************************************************************
 *********************** CALLBACK WEAK IMPLEMENTATIONS *************************
 ******************************************************************************/

/**************************************************************************//**
 * Weak implementation of callback to handle role settings.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_role_set(
  throughput_role_t role)
{
  throughput_ui_set_role(role);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle state change.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_state_change(
  throughput_state_t state)
{
  throughput_ui_set_state(state);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle mode change.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_mode_change(
  throughput_mode_t mode)
{
  (void) mode;
}

/**************************************************************************//**
 * Weak implementation of callback to handle transmission start event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_start(void)
{
  // Do nothing
}

/**************************************************************************//**
 * Weak implementation of callback to handle transmission finished event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_finish(
  throughput_value_t throughput,
  throughput_count_t count)
{
  throughput_ui_set_throughput(throughput);
  throughput_ui_set_count(count);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle reception finished event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_finish_reception(
  throughput_value_t throughput,
  throughput_count_t count,
  throughput_count_t lost,
  throughput_count_t error,
  throughput_time_t time)
{
  throughput_peripheral_on_finish(throughput, count);
  printk(THROUGHPUT_UI_LOST_FORMAT LOG_NL, ((int)lost));
  printk(THROUGHPUT_UI_ERROR_FORMAT LOG_NL, ((int)error));
  printk(THROUGHPUT_UI_TIME_FORMAT LOG_NL, ((int)time));
}

/**************************************************************************//**
 * Weak implementation of callback to handle TX power changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_power_change(
  throughput_tx_power_t power)
{
  throughput_ui_set_tx_power(power);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle RSSI changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_rssi_change(
  throughput_rssi_t rssi)
{
  throughput_ui_set_rssi(rssi);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle phy changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_phy_change(
  throughput_phy_t phy)
{
  throughput_ui_set_phy(phy);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle connection settings changes.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_connection_settings_change(
  throughput_time_t interval,
  throughput_pdu_size_t pdu,
  throughput_mtu_size_t mtu,
  throughput_data_size_t data)
{
  throughput_ui_set_connection_interval(interval);
  throughput_ui_set_pdu_size(pdu);
  throughput_ui_set_mtu_size(mtu);
  throughput_ui_set_data_size(data);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle notification changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_notification_change(
  throughput_notification_t notification)
{
  throughput_ui_set_notifications(notification);
  throughput_ui_update();
}

/**************************************************************************//**
 * Weak implementation of callback to handle indication changed event.
 *****************************************************************************/
__attribute__ ((weak)) void throughput_peripheral_on_indication_change(
  throughput_notification_t indication)
{
  throughput_ui_set_indications(indication);
  throughput_ui_update();
}
