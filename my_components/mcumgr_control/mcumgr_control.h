#pragma once

#ifdef USE_ZEPHYR

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"

namespace esphome::mcumgr_control {

/// Custom MCUmgr/SMP group for nRF52 control and telemetry.
///
/// Group 64 (MGMT_GROUP_ID_PERUSER):
///   command 0 (read)  -> led, pwr, batt_mv, batt_pct, cpu_c
///   command 1 (write) <- led, pwr
class McumgrControl final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_BLUETOOTH;
  }

  void set_led_switch(switch_::Switch *sw) { this->led_ = sw; }
  void set_power_switch(switch_::Switch *sw) { this->power_ = sw; }
  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_ = sensor; }
  void set_level_sensor(sensor::Sensor *sensor) { this->level_ = sensor; }
  void set_cpu_temperature_sensor(sensor::Sensor *sensor) {
    this->cpu_temperature_ = sensor;
  }

  /// Schedule port changes on the ESPHome main loop.
  void apply_ports(bool has_led, bool led, bool has_pwr, bool pwr);

  switch_::Switch *led_{nullptr};
  switch_::Switch *power_{nullptr};
  sensor::Sensor *voltage_{nullptr};
  sensor::Sensor *level_{nullptr};
  sensor::Sensor *cpu_temperature_{nullptr};
};

}  // namespace esphome::mcumgr_control

#endif  // USE_ZEPHYR
