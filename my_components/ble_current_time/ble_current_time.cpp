#include "ble_current_time.h"

#ifdef USE_ZEPHYR

#include "esphome/core/log.h"

#include <cstdint>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

namespace esphome::ble_current_time {

static const char *const TAG = "ble_current_time";  // NOLINT

#define CTS_SERVICE_UUID 0x1805
#define CTS_CURRENT_TIME_UUID 0x2A2B

// RTC2 is free: RTC0 belongs to the Bluetooth controller and RTC1 to the
// Zephyr system timer. It is clocked from LFCLK (LFXO, 32.768 kHz on this
// board). The CTS write sets the epoch; the 1 Hz alarm advances it.
static const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc2));
static uint32_t rtc_freq = 32768;
static atomic_t rtc_epoch;     // seconds since 1970 (naive local time)
static atomic_t rtc_time_set;  // 1 once the PWA has written CTS
static BleCurrentTime *global_current_time = nullptr;

static void rtc_alarm_cb(const struct device *dev, uint8_t chan,
                         uint32_t ticks, void *user_data) {
  (void) ticks;
  (void) user_data;
  atomic_inc(&rtc_epoch);

  const struct counter_alarm_cfg cfg = {
      .callback = rtc_alarm_cb,
      .ticks = rtc_freq,
      .user_data = nullptr,
      .flags = 0,
  };
  (void) counter_set_channel_alarm(dev, chan, &cfg);
}

// Howard Hinnant's civil-from-days / days-from-civil algorithms.
static void epoch_to_civil(int64_t days, int &year, unsigned &month,
                           unsigned &day) {
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = (mp < 10) ? (mp + 3) : (mp - 9);
  year += (month <= 2);
}

static int64_t civil_to_epoch(int year, unsigned month, unsigned day,
                              unsigned hours, unsigned minutes,
                              unsigned seconds) {
  year -= (month <= 2);
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const int mp = static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned doy = (153 * mp + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + static_cast<int64_t>(doe) - 719468;
  return days * 86400 + static_cast<int64_t>(hours) * 3600 +
         static_cast<int64_t>(minutes) * 60 + seconds;
}

static void build_current_time(uint8_t out[10]) {
  int64_t epoch = 0;
  if (atomic_get(&rtc_time_set) != 0) {
    epoch = static_cast<int64_t>(atomic_get(&rtc_epoch));
  }

  if (epoch <= 0) {
    // 2000-01-01 00:00:00, Saturday. The PWA treats years < 2020 as unset
    // and writes the browser time.
    out[0] = 0xD0;
    out[1] = 0x07;
    out[2] = 1;
    out[3] = 1;
    out[4] = 0;
    out[5] = 0;
    out[6] = 0;
    out[7] = 6;
    out[8] = 0;
    out[9] = 0;
    return;
  }

  int64_t days = epoch / 86400;
  int64_t sod = epoch % 86400;
  if (sod < 0) {
    sod += 86400;
    days -= 1;
  }

  int year = 0;
  unsigned month = 1;
  unsigned day = 1;
  epoch_to_civil(days, year, month, day);

  int dow = static_cast<int>((days + 3) % 7);  // 1970-01-01 = Thursday
  if (dow < 0) {
    dow += 7;
  }

  out[0] = static_cast<uint8_t>(year & 0xff);
  out[1] = static_cast<uint8_t>((year >> 8) & 0xff);
  out[2] = static_cast<uint8_t>(month);
  out[3] = static_cast<uint8_t>(day);
  out[4] = static_cast<uint8_t>(sod / 3600);
  out[5] = static_cast<uint8_t>((sod / 60) % 60);
  out[6] = static_cast<uint8_t>(sod % 60);
  out[7] = static_cast<uint8_t>(dow + 1);
  out[8] = 0;  // fractions256
  out[9] = 0;  // adjust reason
}

extern const struct bt_gatt_service_static cts_svc;

static ssize_t read_current_time(bt_conn *conn, const bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset) {
  uint8_t value[10];
  build_current_time(value);
  return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
                           sizeof(value));
}

static ssize_t write_current_time(bt_conn *conn, const bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags) {
  (void) conn;
  (void) attr;
  (void) flags;
  if (offset != 0) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }
  if (len < 9) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }

  const auto *p = static_cast<const uint8_t *>(buf);
  const uint16_t year = static_cast<uint16_t>(p[0] | (p[1] << 8));
  const uint8_t month = p[2];
  const uint8_t day = p[3];
  const uint8_t hours = p[4];
  const uint8_t minutes = p[5];
  const uint8_t seconds = p[6];

  if (year < 1970 || year > 2105 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hours > 23 || minutes > 59 || seconds > 59) {
    return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
  }

  const int64_t epoch =
      civil_to_epoch(year, month, day, hours, minutes, seconds);
  atomic_set(&rtc_epoch, static_cast<uint32_t>(epoch));
  atomic_set(&rtc_time_set, 1);

  uint8_t value[10];
  build_current_time(value);
  bt_gatt_notify(nullptr, &cts_svc.attrs[2], value, sizeof(value));

  if (global_current_time != nullptr) {
    global_current_time->notify_time_sync();
  }

  ESP_LOGI(TAG, "Time set: %04u-%02u-%02u %02u:%02u:%02u (RTC)",
           static_cast<unsigned>(year), static_cast<unsigned>(month),
           static_cast<unsigned>(day), static_cast<unsigned>(hours),
           static_cast<unsigned>(minutes), static_cast<unsigned>(seconds));
  return len;
}

static void current_time_ccc_changed(const bt_gatt_attr *attr,
                                     uint16_t value) {
  (void) attr;
  if (value == BT_GATT_CCC_NOTIFY) {
    uint8_t current[10];
    build_current_time(current);
    bt_gatt_notify(nullptr, &cts_svc.attrs[2], current, sizeof(current));
  }
}

// clang-format off
BT_GATT_SERVICE_DEFINE(  // NOLINT
    cts_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(CTS_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_16(CTS_CURRENT_TIME_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
            BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        read_current_time, write_current_time, nullptr),
    BT_GATT_CCC(current_time_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);
// clang-format on

void BleCurrentTime::setup() {
  global_current_time = this;

  if (!device_is_ready(rtc_dev)) {
    ESP_LOGE(TAG, "RTC2 is not ready");
    return;
  }

  rtc_freq = counter_get_frequency(rtc_dev);
  atomic_clear(&rtc_epoch);
  atomic_clear(&rtc_time_set);

  int err = counter_start(rtc_dev);
  if (err != 0) {
    ESP_LOGE(TAG, "RTC2 start failed: %d", err);
    return;
  }

  const struct counter_alarm_cfg cfg = {
      .callback = rtc_alarm_cb,
      .ticks = rtc_freq,
      .user_data = nullptr,
      .flags = 0,
  };
  err = counter_set_channel_alarm(rtc_dev, 0, &cfg);
  if (err != 0) {
    ESP_LOGE(TAG, "RTC2 alarm failed: %d", err);
    return;
  }

  ESP_LOGI(TAG, "RTC2 on LFCLK started (%u Hz)", rtc_freq);
}

time_t BleCurrentTime::timestamp_now() {
  if (atomic_get(&rtc_time_set) == 0) {
    return 0;
  }
  return static_cast<time_t>(atomic_get(&rtc_epoch));
}

void BleCurrentTime::notify_time_sync() {
  this->time_sync_callback_.call();
}

void BleCurrentTime::loop() {
  const uint32_t now = k_uptime_get_32();
  if (now - this->last_notify_ms_ < 60000) {
    return;
  }
  this->last_notify_ms_ = now;

  if (atomic_get(&rtc_time_set) == 0) {
    return;
  }
  uint8_t value[10];
  build_current_time(value);
  bt_gatt_notify(nullptr, &cts_svc.attrs[2], value, sizeof(value));
}

void BleCurrentTime::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Current Time Service (0x%04X):\n"
                "  Current Time (0x%04X): read/write/notify\n"
                "  Time base: RTC2 / LFCLK (LFXO, %u Hz)",
                CTS_SERVICE_UUID, CTS_CURRENT_TIME_UUID, rtc_freq);
  time::RealTimeClock::dump_config();
}

}  // namespace esphome::ble_current_time

#endif  // USE_ZEPHYR
