#pragma once

#ifdef USE_ZEPHYR

#include "esphome/components/time/real_time_clock.h"

namespace esphome::ble_current_time {

/// Bluetooth Current Time Service (0x1805) and ESPHome time source.
///
/// Current Time (0x2A2B): 10 bytes, read/write/notify.
/// The PWA writes the browser's local time; the device keeps it on RTC2
/// (LFCLK/LFXO) and exposes the same epoch to ESPHome's time component.
class BleCurrentTime final : public time::RealTimeClock {
 public:
  void setup() override;
  void loop() override;
  void update() override {}
  void dump_config() override;
  time_t timestamp_now() override;
  void notify_time_sync();

  float get_setup_priority() const override {
    return setup_priority::AFTER_BLUETOOTH;
  }

 protected:
  uint32_t last_notify_ms_{0};
};

}  // namespace esphome::ble_current_time

#endif  // USE_ZEPHYR
