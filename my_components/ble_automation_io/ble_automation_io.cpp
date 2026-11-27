#include "ble_automation_io.h"

#ifdef USE_ZEPHYR

#include "esphome/core/log.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>

namespace esphome::ble_automation_io {

static const char *const TAG = "ble_automation_io";  // NOLINT

static BleAutomationIO *global_aios = nullptr;  // NOLINT

#define AIOS_SERVICE_UUID 0x1815
#define AIOS_DIGITAL_INPUT_UUID 0x2A56
#define AIOS_DIGITAL_OUTPUT_UUID 0x2A57
#define AIOS_ANALOG_INPUT_UUID 0x2A59
#define AIOS_NUMBER_OF_DIGITALS_UUID 0x2909

// Attribute indices in aios_svc.attrs[].
enum AiosAttr : uint8_t {
  ATTR_SERVICE = 0,
  ATTR_DIN_CHRC = 1,
  ATTR_DIN_VALUE = 2,
  ATTR_DIN_CCC = 3,
  ATTR_DIN_NUM = 4,
  ATTR_DOUT_CHRC = 5,
  ATTR_DOUT_VALUE = 6,
  ATTR_DOUT_CCC = 7,
  ATTR_DOUT_NUM = 8,
  ATTR_AIN_BATT_CHRC = 9,
  ATTR_AIN_BATT_VALUE = 10,
  ATTR_AIN_BATT_CCC = 11,
  ATTR_AIN_BATT_CPF = 12,
  ATTR_AIN_BATT_CUD = 13,
  ATTR_AIN_CPU_CHRC = 14,
  ATTR_AIN_CPU_VALUE = 15,
  ATTR_AIN_CPU_CCC = 16,
  ATTR_AIN_CPU_CPF = 17,
  ATTR_AIN_CPU_CUD = 18,
  ATTR_AIN_RSSI_CHRC = 19,
  ATTR_AIN_RSSI_VALUE = 20,
  ATTR_AIN_RSSI_CCC = 21,
  ATTR_AIN_RSSI_CPF = 22,
  ATTR_AIN_RSSI_CUD = 23,
};

static const uint8_t NUM_DIGITALS = 2;

// Characteristic Presentation Format: float32, exponent 0, Bluetooth SIG
// namespace, description 0. Unit 0x2728 = volt, 0x272F = degree Celsius.
static const uint8_t CPF_BATTERY[7] = {0x14, 0x00, 0x28, 0x27, 0x01, 0x00, 0x00};
static const uint8_t CPF_CPU[7] = {0x14, 0x00, 0x2F, 0x27, 0x01, 0x00, 0x00};
// float32, exponent 0, Bluetooth SIG namespace; unit left unitless (0x2700),
// the CUD below names it as dBm.
static const uint8_t CPF_RSSI[7] = {0x14, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00};

static const char CUD_BATTERY[] = "Battery voltage";
static const char CUD_CPU[] = "CPU temperature";
static const char CUD_RSSI[] = "RSSI, dBm";

static uint8_t digital_value() {
  auto *self = global_aios;
  uint8_t value = 0;
  if (self != nullptr) {
    if (self->led_ != nullptr && self->led_->state) {
      value |= 0x01;
    }
    if (self->power_ != nullptr && self->power_->state) {
      value |= 0x02;
    }
  }
  return value;
}

static float battery_voltage() {
  auto *self = global_aios;
  if (self == nullptr || self->voltage_ == nullptr) {
    return NAN;
  }
  return self->voltage_->state;
}

static float cpu_temperature() {
  auto *self = global_aios;
  if (self == nullptr || self->cpu_temperature_ == nullptr) {
    return NAN;
  }
  return self->cpu_temperature_->state;
}

struct RssiHandleLookup {
  uint16_t handle;
  bool found;
};

static void rssi_lookup_cb(struct bt_conn *conn, void *data) {
  auto *ctx = static_cast<RssiHandleLookup *>(data);
  if (ctx->found) {
    return;
  }
  struct bt_conn_info info;
  if (bt_conn_get_info(conn, &info) != 0 ||
      info.state != BT_CONN_STATE_CONNECTED) {
    return;
  }
  uint16_t handle = 0;
  if (bt_hci_get_conn_handle(conn, &handle) == 0) {
    ctx->handle = handle;
    ctx->found = true;
  }
}

static bool get_conn_handle(uint16_t *handle) {
  RssiHandleLookup ctx{0, false};
  bt_conn_foreach(BT_CONN_TYPE_LE, rssi_lookup_cb, &ctx);
  if (!ctx.found) {
    return false;
  }
  *handle = ctx.handle;
  return true;
}

static void rssi_log_failure(const char *what, int value) {
  static uint32_t last_log = 0;
  const uint32_t now = k_uptime_get_32();
  if (now - last_log < 10000) {
    return;
  }
  last_log = now;
  ESP_LOGW(TAG, "RSSI: %s (%d)", what, value);
}

// Connection RSSI through the HCI Read RSSI command. Web Bluetooth does not
// expose the RSSI of a connected device, so the device publishes it itself.
static bool read_conn_rssi(int8_t *rssi) {
  uint16_t handle = 0;
  if (!get_conn_handle(&handle)) {
    rssi_log_failure("no connection handle", 0);
    return false;
  }

  struct net_buf *buf = bt_hci_cmd_create(
      BT_HCI_OP_READ_RSSI, sizeof(struct bt_hci_cp_read_rssi));
  if (buf == nullptr) {
    rssi_log_failure("no HCI buffer", 0);
    return false;
  }
  auto *cp = static_cast<struct bt_hci_cp_read_rssi *>(
      net_buf_add(buf, sizeof(struct bt_hci_cp_read_rssi)));
  cp->handle = sys_cpu_to_le16(handle);

  struct net_buf *rsp = nullptr;
  const int err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
  if (err != 0) {
    if (rsp != nullptr) {
      net_buf_unref(rsp);
    }
    rssi_log_failure("HCI send failed", err);
    return false;
  }

  auto *rp = reinterpret_cast<struct bt_hci_rp_read_rssi *>(rsp->data);
  const uint8_t status = rp->status;
  const int8_t value = rp->rssi;
  net_buf_unref(rsp);

  if (status != 0) {
    rssi_log_failure("HCI status", status);
    return false;
  }
  if (value >= 127) {
    rssi_log_failure("RSSI unavailable", value);
    return false;
  }
  *rssi = value;

  static uint32_t last_ok_log = 0;
  const uint32_t now = k_uptime_get_32();
  if (now - last_ok_log >= 10000) {
    last_ok_log = now;
    ESP_LOGI(TAG, "RSSI: %d dBm", value);
  }
  return true;
}

// Updated from the main loop; GATT/CCC callbacks run in the BT RX thread and
// must not issue a blocking HCI command (that would deadlock the RX thread).
static float cached_rssi = NAN;

static float rssi_value() { return cached_rssi; }

static ssize_t read_digital_input(bt_conn *conn, const bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset) {
  const uint8_t value = digital_value();
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value,
                           sizeof(value));
}

static ssize_t read_digital_output(bt_conn *conn, const bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset) {
  const uint8_t value = digital_value();
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value,
                           sizeof(value));
}

static ssize_t read_analog_battery(bt_conn *conn, const bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset) {
  const float value = battery_voltage();
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value,
                           sizeof(value));
}

static ssize_t read_analog_cpu(bt_conn *conn, const bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset) {
  const float value = cpu_temperature();
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value,
                           sizeof(value));
}

static ssize_t read_analog_rssi(bt_conn *conn, const bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset) {
  const float value = rssi_value();
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value,
                           sizeof(value));
}

static ssize_t read_cpf(bt_conn *conn, const bt_gatt_attr *attr, void *buf,
                        uint16_t len, uint16_t offset) {
  return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, 7);
}

static ssize_t read_cud(bt_conn *conn, const bt_gatt_attr *attr, void *buf,
                        uint16_t len, uint16_t offset) {
  const auto *str = static_cast<const char *>(attr->user_data);
  return bt_gatt_attr_read(conn, attr, buf, len, offset, str, strlen(str));
}

static ssize_t read_num_digitals(bt_conn *conn, const bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset) {
  return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, 1);
}

static ssize_t write_digital_output(bt_conn *conn, const bt_gatt_attr *attr,
                                    const void *buf, uint16_t len,
                                    uint16_t offset, uint8_t flags) {
  (void) conn;
  (void) attr;
  (void) flags;
  if (offset != 0) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }
  if (len != 1) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  auto *self = global_aios;
  if (self == nullptr) {
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  }
  const uint8_t value = static_cast<const uint8_t *>(buf)[0];
  self->apply_outputs((value & 0x01) != 0, (value & 0x02) != 0);
  return len;
}

extern const struct bt_gatt_service_static aios_svc;

static void publish_digital() {
  const uint8_t value = digital_value();
  bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_DIN_VALUE], &value,
                 sizeof(value));
  bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_DOUT_VALUE], &value,
                 sizeof(value));
}

static void publish_battery() {
  const float value = battery_voltage();
  bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_AIN_BATT_VALUE], &value,
                 sizeof(value));
}

static void publish_cpu() {
  const float value = cpu_temperature();
  bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_AIN_CPU_VALUE], &value,
                 sizeof(value));
}

static void publish_rssi() {
  const float value = rssi_value();
  bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_AIN_RSSI_VALUE], &value,
                 sizeof(value));
}

static void digital_ccc_changed(const bt_gatt_attr *attr, uint16_t value) {
  if (value == BT_GATT_CCC_NOTIFY) {
    const uint8_t current = digital_value();
    bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_DIN_VALUE], &current,
                   sizeof(current));
    bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_DOUT_VALUE], &current,
                   sizeof(current));
  }
}

static void battery_ccc_changed(const bt_gatt_attr *attr, uint16_t value) {
  if (value == BT_GATT_CCC_NOTIFY) {
    const float current = battery_voltage();
    bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_AIN_BATT_VALUE], &current,
                   sizeof(current));
  }
}

static void cpu_ccc_changed(const bt_gatt_attr *attr, uint16_t value) {
  if (value == BT_GATT_CCC_NOTIFY) {
    const float current = cpu_temperature();
    bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_AIN_CPU_VALUE], &current,
                   sizeof(current));
  }
}

static void rssi_ccc_changed(const bt_gatt_attr *attr, uint16_t value) {
  if (value == BT_GATT_CCC_NOTIFY) {
    const float current = rssi_value();
    bt_gatt_notify(nullptr, &aios_svc.attrs[ATTR_AIN_RSSI_VALUE], &current,
                   sizeof(current));
  }
}

// clang-format off
BT_GATT_SERVICE_DEFINE(  // NOLINT
    aios_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(AIOS_SERVICE_UUID)),

    // Digital Input
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(AIOS_DIGITAL_INPUT_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        read_digital_input, nullptr, nullptr),
    BT_GATT_CCC(digital_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(
        BT_UUID_DECLARE_16(AIOS_NUMBER_OF_DIGITALS_UUID), BT_GATT_PERM_READ,
        read_num_digitals, nullptr, (void *) &NUM_DIGITALS),

    // Digital Output
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(AIOS_DIGITAL_OUTPUT_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
            BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        read_digital_output, write_digital_output, nullptr),
    BT_GATT_CCC(digital_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(
        BT_UUID_DECLARE_16(AIOS_NUMBER_OF_DIGITALS_UUID), BT_GATT_PERM_READ,
        read_num_digitals, nullptr, (void *) &NUM_DIGITALS),

    // Analog Input: battery voltage
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(AIOS_ANALOG_INPUT_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        read_analog_battery, nullptr, nullptr),
    BT_GATT_CCC(battery_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CPF, BT_GATT_PERM_READ, read_cpf, nullptr,
                       (void *) CPF_BATTERY),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CUD, BT_GATT_PERM_READ, read_cud, nullptr,
                       (void *) CUD_BATTERY),

    // Analog Input: CPU die temperature
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(AIOS_ANALOG_INPUT_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        read_analog_cpu, nullptr, nullptr),
    BT_GATT_CCC(cpu_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CPF, BT_GATT_PERM_READ, read_cpf, nullptr,
                       (void *) CPF_CPU),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CUD, BT_GATT_PERM_READ, read_cud, nullptr,
                       (void *) CUD_CPU),

    // Analog Input: connection RSSI (dBm)
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(AIOS_ANALOG_INPUT_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        read_analog_rssi, nullptr, nullptr),
    BT_GATT_CCC(rssi_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CPF, BT_GATT_PERM_READ, read_cpf, nullptr,
                       (void *) CPF_RSSI),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CUD, BT_GATT_PERM_READ, read_cud, nullptr,
                       (void *) CUD_RSSI),
);
// clang-format on

void BleAutomationIO::setup() {
  global_aios = this;

  if (this->led_ != nullptr) {
    this->led_->add_on_state_callback([](float) { publish_digital(); });
  }
  if (this->power_ != nullptr) {
    this->power_->add_on_state_callback([](float) { publish_digital(); });
  }
  if (this->voltage_ != nullptr) {
    this->voltage_->add_on_state_callback([](float) { publish_battery(); });
  }
  if (this->cpu_temperature_ != nullptr) {
    this->cpu_temperature_->add_on_state_callback([](float) { publish_cpu(); });
  }
}

void BleAutomationIO::loop() {
  const uint32_t now = k_uptime_get_32();
  if (now - this->rssi_last_ < 2000) {
    return;
  }
  this->rssi_last_ = now;

  // read_conn_rssi() returns false when there is no active connection; no HCI
  // command is issued in that case.
  int8_t rssi = 0;
  if (read_conn_rssi(&rssi)) {
    cached_rssi = static_cast<float>(rssi);
  } else {
    cached_rssi = NAN;
  }
  publish_rssi();
}

void BleAutomationIO::apply_outputs(bool led, bool power) {
  this->defer([this, led, power]() {
    if (this->led_ != nullptr) {
      if (led) {
        this->led_->turn_on();
      } else {
        this->led_->turn_off();
      }
    }
    if (this->power_ != nullptr) {
      if (power) {
        this->power_->turn_on();
      } else {
        this->power_->turn_off();
      }
    }
  });
}

void BleAutomationIO::dump_config() {
  ESP_LOGCONFIG(TAG,
                "BLE Automation IO Service (0x%04X):\n"
                "  Digital Input/Output: bit0 LED, bit1 Power\n"
                "  Analog Input: battery voltage, CPU temperature, RSSI\n"
                "  LED switch: %s\n"
                "  Power switch: %s\n"
                "  Battery voltage sensor: %s\n"
                "  CPU temperature sensor: %s",
                AIOS_SERVICE_UUID, this->led_ != nullptr ? "yes" : "no",
                this->power_ != nullptr ? "yes" : "no",
                this->voltage_ != nullptr ? "yes" : "no",
                this->cpu_temperature_ != nullptr ? "yes" : "no");
}

}  // namespace esphome::ble_automation_io

#endif  // USE_ZEPHYR
