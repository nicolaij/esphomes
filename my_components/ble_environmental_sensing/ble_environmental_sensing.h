#pragma once

#ifdef USE_ZEPHYR

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::ble_environmental_sensing {

/// Bluetooth Environmental Sensing Service (0x181A) with Temperature (0x2A6E).
///
/// Temperature is encoded as sint16 in 0.01 °C, as required by the GATT spec.
class BleEnvironmentalSensing final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_BLUETOOTH;
  }

  void set_cpu_temperature_sensor(sensor::Sensor *sensor) {
    this->cpu_temperature_ = sensor;
  }

  sensor::Sensor *cpu_temperature_{nullptr};
};

}  // namespace esphome::ble_environmental_sensing

#endif  // USE_ZEPHYR
