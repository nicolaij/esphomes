#pragma once

#ifdef USE_ZEPHYR

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::ble_battery {

/// Publishes a sensor value to the standard GATT Battery Service (0x180F/0x2A19).
class BleBattery final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_BLUETOOTH;
  }

  void set_level_sensor(sensor::Sensor *sensor) { this->level_ = sensor; }

 protected:
  void publish_level_(float percent);

  sensor::Sensor *level_{nullptr};
};

}  // namespace esphome::ble_battery

#endif  // USE_ZEPHYR
