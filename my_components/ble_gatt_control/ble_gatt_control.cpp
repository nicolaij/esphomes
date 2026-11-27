#include "ble_gatt_control.h"

#ifdef USE_ZEPHYR

#include "esphome/core/log.h"

#include <cerrno>
#include <cmath>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/bas.h>

namespace esphome::ble_gatt_control {

static const char *const TAG = "ble_gatt_control";

// Vendor-specific 16-bit UUID range (0xFF00..0xFFFF)
#define BLE_GATT_CTRL_SVC_UUID 0xFF00
#define BLE_GATT_CTRL_LED_UUID 0xFF01
#define BLE_GATT_CTRL_POWER_UUID 0xFF02
#define BLE_GATT_CTRL_VOLTAGE_UUID 0xFF03

BleGattControl *BleGattControl::instance_ = nullptr;  // NOLINT

// clang-format off
BT_GATT_SERVICE_DEFINE(  // NOLINT
    ble_gatt_control_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(BLE_GATT_CTRL_SVC_UUID)),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(BLE_GATT_CTRL_LED_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        BleGattControl::read_led, BleGattControl::write_led, nullptr),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(BLE_GATT_CTRL_POWER_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        BleGattControl::read_power, BleGattControl::write_power, nullptr),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(BLE_GATT_CTRL_VOLTAGE_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        BleGattControl::read_voltage, nullptr, nullptr),
    BT_GATT_CCC(BleGattControl::voltage_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);
// clang-format on

void BleGattControl::setup() {
  instance_ = this;

  if (this->voltage_ != nullptr) {
    this->voltage_->add_on_state_callback(
        [this](float state) { this->publish_voltage_(state); });
    if (!std::isnan(this->voltage_->state)) {
      this->publish_voltage_(this->voltage_->state);
    }
  }
  if (this->level_ != nullptr) {
    this->level_->add_on_state_callback(
        [this](float state) { this->publish_level_(state); });
    if (!std::isnan(this->level_->state)) {
      this->publish_level_(this->level_->state);
    }
  }
}

void BleGattControl::dump_config() {
  ESP_LOGCONFIG(TAG,
                "BLE GATT control:\n"
                "  Service: 0x%04X (LED 0x%04X, power 0x%04X, voltage 0x%04X)\n"
                "  Battery service: 0x180F / 0x2A19 (percent)\n"
                "  LED switch: %s\n"
                "  Power switch: %s",
                BLE_GATT_CTRL_SVC_UUID, BLE_GATT_CTRL_LED_UUID,
                BLE_GATT_CTRL_POWER_UUID, BLE_GATT_CTRL_VOLTAGE_UUID,
                this->led_ != nullptr ? "yes" : "no",
                this->power_ != nullptr ? "yes" : "no");
}

void BleGattControl::publish_voltage_(float volts) {
  if (std::isnan(volts) || volts < 0.0f) {
    return;
  }
  const auto mv = static_cast<uint16_t>(lroundf(volts * 1000.0f));
  if (mv == this->voltage_mv_) {
    return;
  }
  this->voltage_mv_ = mv;

  int err = bt_gatt_notify_uuid(nullptr,
                                BT_UUID_DECLARE_16(BLE_GATT_CTRL_VOLTAGE_UUID),
                                &ble_gatt_control_svc.attrs[0], &this->voltage_mv_,
                                sizeof(this->voltage_mv_));
  if (err != 0 && err != -ENOTCONN) {
    ESP_LOGV(TAG, "Voltage notify failed (%d)", err);
  }
}

void BleGattControl::publish_level_(float percent) {
  if (std::isnan(percent)) {
    return;
  }
  if (percent < 0.0f) {
    percent = 0.0f;
  } else if (percent > 100.0f) {
    percent = 100.0f;
  }
  int err = bt_bas_set_battery_level(static_cast<uint8_t>(lroundf(percent)));
  if (err != 0 && err != -ENOTCONN) {
    ESP_LOGV(TAG, "BAS notify failed (%d)", err);
  }
}

ssize_t BleGattControl::read_led(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr, void *buf,
                                 uint16_t len, uint16_t offset) {
  uint8_t value =
      (instance_ != nullptr && instance_->led_ != nullptr &&
       instance_->led_->state)
          ? 1
          : 0;
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

ssize_t BleGattControl::write_led(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
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
  auto *self = instance_;
  if (self == nullptr || self->led_ == nullptr) {
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  }
  const bool turn_on = static_cast<const uint8_t *>(buf)[0] != 0;
  self->defer([self, turn_on]() {
    if (turn_on) {
      self->led_->turn_on();
    } else {
      self->led_->turn_off();
    }
  });
  return len;
}

ssize_t BleGattControl::read_power(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr, void *buf,
                                   uint16_t len, uint16_t offset) {
  uint8_t value =
      (instance_ != nullptr && instance_->power_ != nullptr &&
       instance_->power_->state)
          ? 1
          : 0;
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

ssize_t BleGattControl::write_power(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
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
  auto *self = instance_;
  if (self == nullptr || self->power_ == nullptr) {
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  }
  const bool turn_on = static_cast<const uint8_t *>(buf)[0] != 0;
  self->defer([self, turn_on]() {
    if (turn_on) {
      self->power_->turn_on();
    } else {
      self->power_->turn_off();
    }
  });
  return len;
}

ssize_t BleGattControl::read_voltage(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr, void *buf,
                                     uint16_t len, uint16_t offset) {
  const uint16_t mv = instance_ != nullptr ? instance_->voltage_mv_ : 0;
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &mv, sizeof(mv));
}

void BleGattControl::voltage_ccc_changed(const struct bt_gatt_attr *attr,
                                         uint16_t value) {
  (void) attr;
  if (value != BT_GATT_CCC_NOTIFY || instance_ == nullptr) {
    return;
  }
  bt_gatt_notify_uuid(nullptr, BT_UUID_DECLARE_16(BLE_GATT_CTRL_VOLTAGE_UUID),
                      &ble_gatt_control_svc.attrs[0], &instance_->voltage_mv_,
                      sizeof(instance_->voltage_mv_));
}

}  // namespace esphome::ble_gatt_control

#endif  // USE_ZEPHYR
