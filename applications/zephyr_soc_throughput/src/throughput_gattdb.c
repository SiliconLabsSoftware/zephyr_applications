/***************************************************************************//**
 * @file throughput_gattdb.c
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
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "throughput_gattdb.h"

/* Throughput Test Service */
struct bt_uuid_128 throughput_test_service_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0xbbb99e70, 0xfff7, 0x46cf, 0xabc7, 0x2d32c71820f2));

// /* Throughput Information Service */
struct bt_uuid_128 throughput_information_service_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0xba1e0e9f, 0x4d81, 0xbae3, 0xf748, 0x3ad55da38b46));

/* Indications characteristic */
const struct bt_uuid_128 indication_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0x6109b631, 0xa643, 0x4a51, 0x83d2, 0x2059700ad49f));

/* Notifications characteristic */
const struct bt_uuid_128 notification_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0x47b73dd6, 0xdee3, 0x4da1, 0x9be0, 0xf5c539a9a4be));

/* Transmission ON characteristic */
const struct bt_uuid_128 transmission_on_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0xbe6b6be1, 0xcd8a, 0x4106, 0x9181, 0x5ffe2bc67718));

/* Throughput result characteristic */
const struct bt_uuid_128 throughput_result_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0xadf32227, 0xb00f, 0x400c, 0x9eeb, 0xb903a6cc291b));

/* Connection PHY characteristic */
const struct bt_uuid_128 connection_phy_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0x00a82b93, 0x0feb, 0x2739, 0x72be, 0xabda1f5993d0));

/* Connection interval characteristic */
const struct bt_uuid_128 connection_interval_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0x0a32f5a6, 0x0a6c, 0x4954, 0xf413, 0xa698faf2c664));

/* Responder latency characteristic */
const struct bt_uuid_128 responder_latency_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0xff629b92, 0x332b, 0xe7f7, 0x975f, 0x0e535872ddae));

/* Supervision timeout characteristic */
const struct bt_uuid_128 supervision_timeout_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0x67e2c4f2, 0x2f50, 0x914c, 0xa611, 0xadb3727b056d));

/* PDU size characteristic */
const struct bt_uuid_128 pdu_size_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0x30cc364a, 0x0739, 0x268c, 0x4926, 0x36f112631e0c));

/* MTU size characteristic */
const struct bt_uuid_128 mtu_size_char_uuid = BT_UUID_INIT_128(
  BT_UUID_128_ENCODE(0x3816df2f, 0xd974, 0xd915, 0xd26e, 0x78300f25e86e));

static void (*ccc_changed_cb)(enum throughput_gattdb_char c,
                              const struct bt_gatt_attr *attr,
                              uint16_t value) = NULL;

static ssize_t (*on_char_read_cb)(enum throughput_gattdb_char c,
                                  struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len,
                                  uint16_t offset) = NULL;

static ssize_t (*on_char_write_cb)(enum throughput_gattdb_char c,
                                   struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags) = NULL;

static void indication_ccc_changed(const struct bt_gatt_attr *attr,
                                   uint16_t value);
static void notification_ccc_changed(const struct bt_gatt_attr *attr,
                                     uint16_t value);
static void transmission_on_ccc_changed(const struct bt_gatt_attr *attr,
                                        uint16_t value);
static void throughput_result_ccc_changed(const struct bt_gatt_attr *attr,
                                          uint16_t value);

static void connection_phy_ccc_changed(const struct bt_gatt_attr *attr,
                                       uint16_t value);

static void connection_interval_ccc_changed(const struct bt_gatt_attr *attr,
                                            uint16_t value);

static void responder_latency_ccc_changed(const struct bt_gatt_attr *attr,
                                          uint16_t value);

static void supervision_timeout_ccc_changed(const struct bt_gatt_attr *attr,
                                            uint16_t value);

static void pdu_size_ccc_changed(const struct bt_gatt_attr *attr,
                                 uint16_t value);

static void mtu_size_ccc_changed(const struct bt_gatt_attr *attr,
                                 uint16_t value);

static ssize_t transmission_on_char_read(struct bt_conn *conn,
                                         const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len,
                                         uint16_t offset);
static ssize_t transmission_on_char_write(struct bt_conn *conn,
                                          const struct bt_gatt_attr *attr,
                                          const void *buf, uint16_t len,
                                          uint16_t offset, uint8_t flags);

static ssize_t connection_phy_char_read(struct bt_conn *conn,
                                        const struct bt_gatt_attr *attr,
                                        void *buf, uint16_t len,
                                        uint16_t offset);

static ssize_t connection_interval_char_read(struct bt_conn *conn,
                                             const struct bt_gatt_attr *attr,
                                             void *buf, uint16_t len,
                                             uint16_t offset);
static ssize_t connection_responder_latency_char_read(struct bt_conn *conn,
                                                      const struct bt_gatt_attr *attr,
                                                      void *buf,
                                                      uint16_t len,
                                                      uint16_t offset);
static ssize_t connection_timeout_char_read(struct bt_conn *conn,
                                            const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len,
                                            uint16_t offset);
static ssize_t pdu_size_char_read(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len,
                                  uint16_t offset);
static ssize_t mtu_size_char_read(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len,
                                  uint16_t offset);

BT_GATT_SERVICE_DEFINE(throughput_test_svc,
                       BT_GATT_PRIMARY_SERVICE(&throughput_test_service_uuid),

                       BT_GATT_CHARACTERISTIC(&indication_char_uuid.uuid,
                                              BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_NONE,
                                              NULL,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(indication_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Indication data array", BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&notification_char_uuid.uuid,
                                              BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE,
                                              NULL,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(notification_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Notification data array",
                                   BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&transmission_on_char_uuid.uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_WRITE
                                              | BT_GATT_CHRC_WRITE_WITHOUT_RESP
                                              | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_WRITE
                                              | BT_GATT_PERM_READ,
                                              transmission_on_char_read,
                                              transmission_on_char_write,
                                              NULL),
                       BT_GATT_CCC(transmission_on_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Transmission ON", BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&throughput_result_char_uuid.uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_READ,
                                              NULL,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(throughput_result_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Throughput result", BT_GATT_PERM_READ),
                       );

BT_GATT_SERVICE_DEFINE(throughput_information_svc,
                       BT_GATT_PRIMARY_SERVICE(&
                                               throughput_information_service_uuid),

                       BT_GATT_CHARACTERISTIC(&connection_phy_char_uuid.uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              connection_phy_char_read,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(connection_phy_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Connection PHY status", BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&connection_interval_char_uuid.
                                              uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              connection_interval_char_read,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(connection_interval_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Connection interval (in 1.25 ms steps)",
                                   BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&responder_latency_char_uuid.uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              connection_responder_latency_char_read,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(responder_latency_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Responder latency (in connection intervals)",
                                   BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&supervision_timeout_char_uuid.
                                              uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              connection_timeout_char_read,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(supervision_timeout_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("Supervision timeout (in 10 ms steps)",
                                   BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&pdu_size_char_uuid.uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              pdu_size_char_read,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(pdu_size_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("PDU size in bytes", BT_GATT_PERM_READ),

                       BT_GATT_CHARACTERISTIC(&mtu_size_char_uuid.uuid,
                                              BT_GATT_CHRC_READ
                                              | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              mtu_size_char_read,
                                              NULL,
                                              NULL),
                       BT_GATT_CCC(mtu_size_ccc_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CUD("MTU size in bytes", BT_GATT_PERM_READ),
                       );

static void indication_ccc_changed(const struct bt_gatt_attr *attr,
                                   uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_indication,
                   attr,
                   value);
  }
}

static void notification_ccc_changed(const struct bt_gatt_attr *attr,
                                     uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_notification,
                   attr,
                   value);
  }
}

static void transmission_on_ccc_changed(const struct bt_gatt_attr *attr,
                                        uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_transmission_on,
                   attr,
                   value);
  }
}

static void throughput_result_ccc_changed(const struct bt_gatt_attr *attr,
                                          uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_throughput_result,
                   attr,
                   value);
  }
}

static void connection_phy_ccc_changed(const struct bt_gatt_attr *attr,
                                       uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_connection_phy,
                   attr,
                   value);
  }
}

static void connection_interval_ccc_changed(const struct bt_gatt_attr *attr,
                                            uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_connection_interval,
                   attr,
                   value);
  }
}

static void responder_latency_ccc_changed(const struct bt_gatt_attr *attr,
                                          uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_responder_latency,
                   attr,
                   value);
  }
}

static void supervision_timeout_ccc_changed(const struct bt_gatt_attr *attr,
                                            uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_supervision_timeout,
                   attr,
                   value);
  }
}

static void pdu_size_ccc_changed(const struct bt_gatt_attr *attr,
                                 uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_pdu_size,
                   attr,
                   value);
  }
}

static void mtu_size_ccc_changed(const struct bt_gatt_attr *attr,
                                 uint16_t value)
{
  if (ccc_changed_cb) {
    ccc_changed_cb(throughput_gattdb_char_mtu_size,
                   attr,
                   value);
  }
}

static ssize_t transmission_on_char_read(struct bt_conn *conn,
                                         const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len,
                                         uint16_t offset)
{
  if (on_char_read_cb) {
    return on_char_read_cb(throughput_gattdb_char_transmission_on,
                           conn,
                           attr,
                           buf,
                           len,
                           offset);
  }
  return 0;
}

static ssize_t transmission_on_char_write(struct bt_conn *conn,
                                          const struct bt_gatt_attr *attr,
                                          const void *buf, uint16_t len,
                                          uint16_t offset, uint8_t flags)
{
  if (on_char_write_cb) {
    return on_char_write_cb(throughput_gattdb_char_transmission_on,
                            conn,
                            attr,
                            buf,
                            len,
                            offset,
                            flags);
  }
  return 0;
}

static ssize_t connection_phy_char_read(struct bt_conn *conn,
                                        const struct bt_gatt_attr *attr,
                                        void *buf, uint16_t len,
                                        uint16_t offset)
{
  if (on_char_read_cb) {
    return on_char_read_cb(throughput_gattdb_char_connection_phy,
                           conn,
                           attr,
                           buf,
                           len,
                           offset);
  }
  return 0;
}

static ssize_t connection_interval_char_read(struct bt_conn *conn,
                                             const struct bt_gatt_attr *attr,
                                             void *buf, uint16_t len,
                                             uint16_t offset)
{
  if (on_char_read_cb) {
    return on_char_read_cb(throughput_gattdb_char_connection_interval,
                           conn,
                           attr,
                           buf,
                           len,
                           offset);
  }
  return 0;
}

static ssize_t connection_responder_latency_char_read(struct bt_conn *conn,
                                                      const struct bt_gatt_attr *attr,
                                                      void *buf,
                                                      uint16_t len,
                                                      uint16_t offset)
{
  if (on_char_read_cb) {
    return on_char_read_cb(throughput_gattdb_char_responder_latency,
                           conn,
                           attr,
                           buf,
                           len,
                           offset);
  }
  return 0;
}

static ssize_t connection_timeout_char_read(struct bt_conn *conn,
                                            const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len,
                                            uint16_t offset)
{
  if (on_char_read_cb) {
    return on_char_read_cb(throughput_gattdb_char_supervision_timeout,
                           conn,
                           attr,
                           buf,
                           len,
                           offset);
  }
  return 0;
}

static ssize_t pdu_size_char_read(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len,
                                  uint16_t offset)
{
  if (on_char_read_cb) {
    return on_char_read_cb(throughput_gattdb_char_pdu_size,
                           conn,
                           attr,
                           buf,
                           len,
                           offset);
  }
  return 0;
}

static ssize_t mtu_size_char_read(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len,
                                  uint16_t offset)
{
  if (on_char_read_cb) {
    return on_char_read_cb(throughput_gattdb_char_mtu_size,
                           conn,
                           attr,
                           buf,
                           len,
                           offset);
  }
  return 0;
}

void throughput_gattdb_register_char_read_cb(ssize_t (*cb)(
                                               enum throughput_gattdb_char c,
                                               struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr,
                                               void *buf,
                                               uint16_t len,
                                               uint16_t offset))
{
  on_char_read_cb = cb;
}

void throughput_gattdb_register_char_write_cb(ssize_t (*cb)(
                                                enum throughput_gattdb_char c,
                                                struct bt_conn *conn,
                                                const struct bt_gatt_attr *attr,
                                                const void *buf,
                                                uint16_t len,
                                                uint16_t offset,
                                                uint8_t flags))
{
  on_char_write_cb = cb;
}

void throughput_gattdb_register_ccc_changed_cb(void (*cb)(
                                                 enum throughput_gattdb_char c,
                                                 const struct bt_gatt_attr *attr,
                                                 uint16_t value))
{
  ccc_changed_cb = cb;
}
