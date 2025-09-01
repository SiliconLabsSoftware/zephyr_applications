/***************************************************************************//**
 * @file bt_event.c
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
#include <zephyr/logging/log.h>

#include "bt_event.h"

LOG_MODULE_REGISTER(bt_event, 3);

K_MSGQ_DEFINE(bt_evt_msgq, sizeof(struct bt_evt), 10, 1);

static bool conn_callbacks_registered = false;

static struct bt_gatt_discover_params discover_params;

/*******************************************************************************
 **************************   LOCAL FUNCTIONS   ********************************
 ******************************************************************************/
static const char *uuid_to_str(const struct bt_uuid *uuid)
{
  static char uuid_str[40];

  bt_uuid_to_str(uuid, uuid_str, sizeof(uuid_str));
  return uuid_str;
}

/**************************************************************************//**
 *  BLE connected callback.
 ******************************************************************************/
static void connected(struct bt_conn *connected, uint8_t err)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_connection_opened_id;
  bt_evt.data.connection_opened.conn = connected;
  bt_evt.data.connection_opened.err = err;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

/**************************************************************************//**
 *  BLE disconnected callback.
 ******************************************************************************/
static void disconnected(struct bt_conn *disconn, uint8_t reason)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_connection_closed_id;
  bt_evt.data.connection_closed.conn = disconn;
  bt_evt.data.connection_closed.reason = reason;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

static bool le_param_req(struct bt_conn *conn,
                         struct bt_le_conn_param *param)
{
  LOG_INF("le_param_req");
  return true;
}

static void param_updated(struct bt_conn *conn, uint16_t interval,
                          uint16_t latency, uint16_t timeout)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_le_param_updated_id;
  bt_evt.data.le_param_updated.conn = conn;
  bt_evt.data.le_param_updated.interval = interval;
  bt_evt.data.le_param_updated.latency = latency;
  bt_evt.data.le_param_updated.timeout = timeout;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

static sl_bt_gap_phy_coding_t get_bt_gap_phy_coding(uint8_t phy)
{
  switch (phy) {
    case BT_CONN_LE_TX_POWER_PHY_1M:
      return sl_bt_gap_phy_coding_1m_uncoded;

    case BT_CONN_LE_TX_POWER_PHY_2M:
      return sl_bt_gap_phy_coding_2m_uncoded;

    case BT_CONN_LE_TX_POWER_PHY_CODED_S8:
      return sl_bt_gap_phy_coding_125k_coded;

    case BT_CONN_LE_TX_POWER_PHY_CODED_S2:
      return sl_bt_gap_phy_coding_500k_coded;

    default:
      return 0;
  }
}

static void le_phy_updated(struct bt_conn *conn,
                           struct bt_conn_le_phy_info *param)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_le_phy_updated_id;
  bt_evt.data.le_phy_updated.conn = conn;
  bt_evt.data.le_phy_updated.tx_phy = get_bt_gap_phy_coding(param->tx_phy);
  bt_evt.data.le_phy_updated.rx_phy = get_bt_gap_phy_coding(param->rx_phy);

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

static void le_data_len_updated(struct bt_conn *conn,
                                struct bt_conn_le_data_len_info *info)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_le_data_len_updated_id;
  bt_evt.data.le_data_len_updated.conn = conn;
  bt_evt.data.le_data_len_updated.tx_max_len = info->tx_max_len;
  bt_evt.data.le_data_len_updated.tx_max_time = info->tx_max_time;
  bt_evt.data.le_data_len_updated.tx_max_time = info->rx_max_len;
  bt_evt.data.le_data_len_updated.tx_max_time = info->rx_max_time;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

#if defined(CONFIG_BT_TRANSMIT_POWER_CONTROL)
static void tx_power_report(struct bt_conn *conn,
                            const struct bt_conn_le_tx_power_report *report)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_tx_power_report_id;
  bt_evt.data.tx_power_report.conn = conn;
  bt_evt.data.tx_power_report.conn = conn;
  bt_evt.data.tx_power_report.reason = report->reason;
  bt_evt.data.tx_power_report.phy = get_bt_gap_phy_coding(report->phy);
  bt_evt.data.tx_power_report.tx_power_level = report->tx_power_level;
  bt_evt.data.tx_power_report.tx_power_level_flag = report->tx_power_level_flag;
  bt_evt.data.tx_power_report.delta = report->delta;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

#endif /* CONFIG_BT_TRANSMIT_POWER_CONTROL */

static struct bt_conn_cb conn_callbacks = {
  .connected = connected,
  .disconnected = disconnected,
  .le_param_req = le_param_req,
  .le_param_updated = param_updated,
  .le_phy_updated = le_phy_updated,
  .le_data_len_updated = le_data_len_updated,
#if defined(CONFIG_BT_TRANSMIT_POWER_CONTROL)
  .tx_power_report = tx_power_report,
#endif /* CONFIG_BT_TRANSMIT_POWER_CONTROL */
};

static void mtu_exchange_callback(struct bt_conn *conn, uint8_t err,
                                  struct bt_gatt_exchange_params *params)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_exchange_mtu_id;
  bt_evt.data.exchange_mtu.conn = conn;
  bt_evt.data.exchange_mtu.err = err;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

int bt_evt_exchange_mtu(struct bt_conn *conn,
                        struct bt_gatt_exchange_params *params)
{
  // Start mtu exchange
  params->func = mtu_exchange_callback;

  return bt_gatt_exchange_mtu(conn, params);
}

static uint8_t discover_callback(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
  struct bt_evt bt_evt;
  uint8_t uuid_size;

  if (!attr) {
    LOG_INF("Discover complete");
    (void)memset(params, 0, sizeof(*params));
    return BT_GATT_ITER_STOP;
  }

  LOG_INF("attr uuid:   %s", uuid_to_str(attr->uuid));
  LOG_INF("params uuid: %s", uuid_to_str(params->uuid));

  bt_evt.id = bt_evt_gatt_discover_attrs_id;
  bt_evt.data.gatt_discover_attrs.conn = conn;
  if (params->uuid->type == BT_UUID_TYPE_16) {
    uuid_size = sizeof(struct bt_uuid_16);
  } else if (params->uuid->type == BT_UUID_TYPE_32) {
    uuid_size = sizeof(struct bt_uuid_32);
  } else {
    uuid_size = sizeof(struct bt_uuid_128);
  }
  memcpy(&(bt_evt.data.gatt_discover_attrs.uuid),
         params->uuid,
         uuid_size);
  bt_evt.data.gatt_discover_attrs.handle = attr->handle;
  bt_evt.data.gatt_discover_attrs.perm = attr->perm;
  bt_evt.data.gatt_discover_attrs.value_handle =
    bt_gatt_attr_value_handle(attr);

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);

  return BT_GATT_ITER_STOP;
}

void le_scan_cb(const bt_addr_le_t *addr, int8_t rssi,
                uint8_t adv_type, struct net_buf_simple *buf)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_le_scan_report_id;
  memcpy(&(bt_evt.data.le_scan_report.addr),
         addr,
         sizeof(bt_addr_le_t));
  bt_evt.data.le_scan_report.rssi = rssi;
  bt_evt.data.le_scan_report.type = adv_type;
  uint16_t len = ADV_DATA_MAX_LENGTH;
  if (len > buf->len) {
    len = buf->len;
  }
  memcpy(bt_evt.data.le_scan_report.buf.data, buf->data, len);

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

int bt_evt_le_scan_start(const struct bt_le_scan_param *param)
{
  return bt_le_scan_start(param, le_scan_cb);
}

int bt_evt_discover_attr(struct bt_conn *conn,
                         uint8_t type,
                         const struct bt_uuid *uuid,
                         uint16_t start_handle,
                         uint16_t end_handle)
{
  discover_params.type = type;
  discover_params.uuid = uuid;
  discover_params.func = discover_callback;
  discover_params.start_handle = start_handle;
  discover_params.end_handle = end_handle;
  LOG_INF("Start discover");
  return bt_gatt_discover(conn, &discover_params);
}

static uint8_t notification_callback(struct bt_conn *conn,
                                     struct bt_gatt_subscribe_params *params,
                                     const void *data, uint16_t length)
{
  struct bt_evt bt_evt;

  if (!data) {
    LOG_INF("[UNSUBSCRIBED]\n");
    params->value_handle = 0U;
    return BT_GATT_ITER_STOP;
  }

  bt_evt.id = bt_evt_gatt_characteristic_notification_id;
  bt_evt.data.gatt_characteristic_notification.conn = conn;
  bt_evt.data.gatt_characteristic_notification.value_handle =
    params->value_handle;
  if (length > ATTR_DATA_MAX_LENGTH) {
    length = ATTR_DATA_MAX_LENGTH;
  }
  memcpy(bt_evt.data.gatt_characteristic_notification.value.data,
         data,
         length);
  bt_evt.data.gatt_characteristic_notification.value.len = length;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);

  return BT_GATT_ITER_CONTINUE;
}

static void subscribe_status(struct bt_conn *conn, uint8_t err,
                             struct bt_gatt_subscribe_params *params)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_gatt_characteristic_subscribe_status_id;
  bt_evt.data.gatt_characteristic_subscribe_status.conn = conn;
  bt_evt.data.gatt_characteristic_subscribe_status.err = err;
  bt_evt.data.gatt_characteristic_subscribe_status.params = params;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
  LOG_INF("[SUBSCRIBE] status: %d, value_handle = %d", err,
          params->value_handle);
}

#if defined(CONFIG_BT_GATT_AUTO_DISCOVER_CCC)
int bt_evt_gatt_subscribe_for_notification(struct bt_conn *conn,
                                           struct bt_gatt_subscribe_params *params,
                                           struct bt_gatt_discover_params *disc_params)
{
  params->ccc_handle = 0;
  params->end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
  params->value = BT_GATT_CCC_NOTIFY;
  params->notify = notification_callback;
  params->subscribe = subscribe_status;
  params->disc_params = disc_params;
  return bt_gatt_subscribe(conn, params);
}

#else
int bt_evt_gatt_subscribe_for_notification(struct bt_conn *conn,
                                           struct bt_gatt_subscribe_params *params)
{
  params->ccc_handle = 0;
  params->end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
  params->value = BT_GATT_CCC_NOTIFY;
  params->notify = notification_callback;
  params->subscribe = subscribe_status;
  return bt_gatt_subscribe(conn, params);
}

#endif

static uint8_t indication_callback(struct bt_conn *conn,
                                   struct bt_gatt_subscribe_params *params,
                                   const void *data, uint16_t length)
{
  struct bt_evt bt_evt;

  if (!data) {
    LOG_INF("[UNSUBSCRIBED]\n");
    params->value_handle = 0U;
    return BT_GATT_ITER_STOP;
  }

  bt_evt.id = bt_evt_gatt_characteristic_indication_id;
  bt_evt.data.gatt_characteristic_indication.conn = conn;
  bt_evt.data.gatt_characteristic_indication.value_handle =
    params->value_handle;
  if (length > ATTR_DATA_MAX_LENGTH) {
    length = ATTR_DATA_MAX_LENGTH;
  }
  memcpy(bt_evt.data.gatt_characteristic_indication.value.data,
         data,
         length);
  bt_evt.data.gatt_characteristic_indication.value.len = length;

  k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);

  return BT_GATT_ITER_CONTINUE;
}

#if defined(CONFIG_BT_GATT_AUTO_DISCOVER_CCC)
int bt_evt_gatt_subscribe_for_indication(struct bt_conn *conn,
                                         struct bt_gatt_subscribe_params *params,
                                         struct bt_gatt_discover_params *disc_params)
{
  params->ccc_handle = 0;
  params->end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
  params->value = BT_GATT_CCC_INDICATE;
  params->notify = indication_callback;
  params->subscribe = subscribe_status;
  params->disc_params = disc_params;
  return bt_gatt_subscribe(conn, params);
}

#else
int bt_evt_gatt_subscribe_for_indication(struct bt_conn *conn,
                                         struct bt_gatt_subscribe_params *params)
{
  params->ccc_handle = 0;
  params->end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
  params->value = BT_GATT_CCC_INDICATE;
  params->notify = indication_callback;
  params->subscribe = subscribe_status;
  return bt_gatt_subscribe(conn, params);
}

#endif

int bt_evt_wait_event(struct bt_evt *evt, k_timeout_t timeout)
{
  return k_msgq_get(&bt_evt_msgq, evt, timeout);
}

int bt_evt_external_signal(uint32_t signals)
{
  struct bt_evt bt_evt;

  bt_evt.id = bt_evt_system_external_signal_id;
  bt_evt.data.system_external_signal.extsignals = signals;
  return k_msgq_put(&bt_evt_msgq, &bt_evt, K_FOREVER);
}

void bt_evt_init(void)
{
  if (!conn_callbacks_registered) {
    bt_conn_cb_register(&conn_callbacks);
    conn_callbacks_registered = true;
  }
}
