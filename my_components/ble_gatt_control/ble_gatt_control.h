#pragma once

#ifdef USE_ZEPHYR

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"

#include <cstdint>
#include <zephyr/bluetooth/gatt.h>

namespace esphome::ble_gatt_control {

/// Custom GATT control service for nRF52 (Zephyr) builds.
///
/// Service 0xFF00 (vendor-specific):
///   0xFF01  LED          read/write  1 byte (0/1)
///   0xFF02  Power (P0.13) read/write 1 byte (0/1)
///   0xFF03  Battery voltage read/notify uint16 LE, millivolts
///
/// Plus the standard Battery Service 0x180F / 0x2A19 (percent, read/notify)
/// provided by Zephyr's CONFIG_BT_BAS.
class BleGattControl final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_BLUETOOTH;
  }

  void set_led_switch(switch_::Switch *sw) { this->led_ = sw; }
  void set_power_switch(switch_::Switch *sw) { this->power_ = sw; }
  void set_voltage_sensor(sensor::Sensor *s) { this->voltage_ = s; }
  void set_level_sensor(sensor::Sensor *s) { this->level_ = s; }

  // GATT callbacks (called from the Bluetooth RX thread)
  static ssize_t read_led(struct bt_conn *conn,
                          const struct bt_gatt_attr *attr, void *buf,
                          uint16_t len, uint16_t offset);
  static ssize_t write_led(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr, const void *buf,
                           uint16_t len, uint16_t offset, uint8_t flags);
  static ssize_t read_power(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, void *buf,
                            uint16_t len, uint16_t offset);
  static ssize_t write_power(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset,
                             uint8_t flags);
  static ssize_t read_voltage(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset);
  static void voltage_ccc_changed(const struct bt_gatt_attr *attr,
                                  uint16_t value);

 protected:
  void publish_voltage_(float volts);
  void publish_level_(float percent);

  static BleGattControl *instance_;

  switch_::Switch *led_{nullptr};
  switch_::Switch *power_{nullptr};
  sensor::Sensor *voltage_{nullptr};
  sensor::Sensor *level_{nullptr};
  uint16_t voltage_mv_{0};
};

}  // namespace esphome::ble_gatt_control

#endif  // USE_ZEPHYR
