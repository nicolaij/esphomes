#pragma once

#ifdef USE_ZEPHYR

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"

namespace esphome::ble_automation_io {

/// Bluetooth Automation IO Service (0x1815) for nRF52/Zephyr builds.
///
/// Digital Input  (0x2A56): bit 0 = LED, bit 1 = power output
/// Digital Output (0x2A57): bit 0 = LED, bit 1 = power output
/// Analog Input   (0x2A59): float32 battery voltage (V)
/// Analog Input   (0x2A59): float32 CPU temperature (C)
/// Analog Input   (0x2A59): float32 connection RSSI (dBm)
class BleAutomationIO final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_BLUETOOTH;
  }

  void set_led_switch(switch_::Switch *sw) { this->led_ = sw; }
  void set_power_switch(switch_::Switch *sw) { this->power_ = sw; }
  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_ = sensor; }
  void set_cpu_temperature_sensor(sensor::Sensor *sensor) {
    this->cpu_temperature_ = sensor;
  }

  /// Apply digital outputs on the ESPHome main loop.
  void apply_outputs(bool led, bool power);

  switch_::Switch *led_{nullptr};
  switch_::Switch *power_{nullptr};
  sensor::Sensor *voltage_{nullptr};
  sensor::Sensor *cpu_temperature_{nullptr};
  uint32_t rssi_last_{0};
};

}  // namespace esphome::ble_automation_io

#endif  // USE_ZEPHYR
