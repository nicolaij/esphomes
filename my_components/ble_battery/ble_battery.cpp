#include "ble_battery.h"

#ifdef USE_ZEPHYR

#include "esphome/core/log.h"

#include <cerrno>
#include <cmath>
#include <zephyr/bluetooth/services/bas.h>

namespace esphome::ble_battery {

static const char *const TAG = "ble_battery";  // NOLINT

void BleBattery::setup() {
  if (this->level_ != nullptr) {
    this->level_->add_on_state_callback(
        [this](float state) { this->publish_level_(state); });
    if (!std::isnan(this->level_->state)) {
      this->publish_level_(this->level_->state);
    }
  }
}

void BleBattery::publish_level_(float percent) {
  if (std::isnan(percent)) {
    return;
  }
  if (percent < 0.0f) {
    percent = 0.0f;
  } else if (percent > 100.0f) {
    percent = 100.0f;
  }
  const int err = bt_bas_set_battery_level(
      static_cast<uint8_t>(lroundf(percent)));
  if (err != 0 && err != -ENOTCONN) {
    ESP_LOGV(TAG, "BAS notify failed (%d)", err);
  }
}

void BleBattery::dump_config() {
  ESP_LOGCONFIG(TAG,
                "BLE Battery Service:\n"
                "  Level sensor: %s",
                this->level_ != nullptr ? "yes" : "no");
}

}  // namespace esphome::ble_battery

#endif  // USE_ZEPHYR
