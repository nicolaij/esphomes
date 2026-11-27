#include "ble_environmental_sensing.h"

#ifdef USE_ZEPHYR

#include "esphome/core/log.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

namespace esphome::ble_environmental_sensing {

static const char *const TAG = "ble_environmental_sensing";  // NOLINT

static BleEnvironmentalSensing *global_ess = nullptr;  // NOLINT

#define ESS_SERVICE_UUID 0x181A
#define ESS_TEMPERATURE_UUID 0x2A6E

enum EssAttr : uint8_t {
  ATTR_SERVICE = 0,
  ATTR_TEMP_CHRC = 1,
  ATTR_TEMP_VALUE = 2,
  ATTR_TEMP_CCC = 3,
  ATTR_TEMP_CPF = 4,
  ATTR_TEMP_CUD = 5,
};

// CPF: sint16, exponent -2, Celsius (0x272F), Bluetooth SIG namespace.
static const uint8_t CPF_TEMPERATURE[7] = {0x0E, 0xFE, 0x2F, 0x27,
                                           0x01, 0x00, 0x00};
static const char CUD_TEMPERATURE[] = "CPU temperature";

static int16_t temperature_value() {
  auto *self = global_ess;
  if (self == nullptr || self->cpu_temperature_ == nullptr) {
    return 0;
  }
  const float celsius = self->cpu_temperature_->state;
  if (!std::isfinite(celsius)) {
    return 0;
  }
  float scaled = celsius * 100.0f;
  if (scaled > 32767.0f) {
    scaled = 32767.0f;
  } else if (scaled < -32768.0f) {
    scaled = -32768.0f;
  }
  return static_cast<int16_t>(lroundf(scaled));
}

static ssize_t read_temperature(bt_conn *conn, const bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset) {
  const int16_t value = temperature_value();
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

extern const struct bt_gatt_service_static ess_svc;

static void publish_temperature() {
  const int16_t value = temperature_value();
  bt_gatt_notify(nullptr, &ess_svc.attrs[ATTR_TEMP_VALUE], &value,
                 sizeof(value));
}

static void temperature_ccc_changed(const bt_gatt_attr *attr, uint16_t value) {
  if (value == BT_GATT_CCC_NOTIFY) {
    const int16_t current = temperature_value();
    bt_gatt_notify(nullptr, &ess_svc.attrs[ATTR_TEMP_VALUE], &current,
                   sizeof(current));
  }
}

// clang-format off
BT_GATT_SERVICE_DEFINE(  // NOLINT
    ess_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(ESS_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(ESS_TEMPERATURE_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        read_temperature, nullptr, nullptr),
    BT_GATT_CCC(temperature_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CPF, BT_GATT_PERM_READ, read_cpf, nullptr,
                       (void *) CPF_TEMPERATURE),
    BT_GATT_DESCRIPTOR(BT_UUID_GATT_CUD, BT_GATT_PERM_READ, read_cud, nullptr,
                       (void *) CUD_TEMPERATURE),
);
// clang-format on

void BleEnvironmentalSensing::setup() {
  global_ess = this;
  if (this->cpu_temperature_ != nullptr) {
    this->cpu_temperature_->add_on_state_callback(
        [](float) { publish_temperature(); });
  }
}

void BleEnvironmentalSensing::dump_config() {
  ESP_LOGCONFIG(TAG,
                "BLE Environmental Sensing (0x%04X):\n"
                "  Temperature: 0x%04X, sint16, 0.01 C\n"
                "  CPU temperature sensor: %s",
                ESS_SERVICE_UUID, ESS_TEMPERATURE_UUID,
                this->cpu_temperature_ != nullptr ? "yes" : "no");
}

}  // namespace esphome::ble_environmental_sensing

#endif  // USE_ZEPHYR
