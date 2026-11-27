#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/log.h"
#include <cmath>

namespace lsm303_custom {

class LSM303DLHCCustomSensor : public esphome::PollingComponent, public esphome::i2c::I2CDevice {
 public:
  esphome::sensor::Sensor *heading_sensor_{nullptr};
  esphome::sensor::Sensor *pitch_sensor_{nullptr};
  esphome::sensor::Sensor *roll_sensor_{nullptr};
  
  // Новые сенсоры осей
  esphome::sensor::Sensor *accel_x_sensor_{nullptr};
  esphome::sensor::Sensor *accel_y_sensor_{nullptr};
  esphome::sensor::Sensor *accel_z_sensor_{nullptr};
  esphome::sensor::Sensor *mag_x_sensor_{nullptr};
  esphome::sensor::Sensor *mag_y_sensor_{nullptr};
  esphome::sensor::Sensor *mag_z_sensor_{nullptr};

  void set_heading_sensor(esphome::sensor::Sensor *s) { this->heading_sensor_ = s; }
  void set_pitch_sensor(esphome::sensor::Sensor *s) { this->pitch_sensor_ = s; }
  void set_roll_sensor(esphome::sensor::Sensor *s) { this->roll_sensor_ = s; }
  
  void set_accel_x_sensor(esphome::sensor::Sensor *s) { this->accel_x_sensor_ = s; }
  void set_accel_y_sensor(esphome::sensor::Sensor *s) { this->accel_y_sensor_ = s; }
  void set_accel_z_sensor(esphome::sensor::Sensor *s) { this->accel_z_sensor_ = s; }
  void set_mag_x_sensor(esphome::sensor::Sensor *s) { this->mag_x_sensor_ = s; }
  void set_mag_y_sensor(esphome::sensor::Sensor *s) { this->mag_y_sensor_ = s; }
  void set_mag_z_sensor(esphome::sensor::Sensor *s) { this->mag_z_sensor_ = s; }

  LSM303DLHCCustomSensor() : PollingComponent(1000) {}

  void setup() override {
    // Инициализация Акселерометра (0x19), 100Hz, все оси активны
    if (!this->write_byte(0x20, 0x57)) {
      ESP_LOGE("lsm303_custom", "Акселерометр (0x19) не отвечает!");
    }

    // Инициализация Магнитометра (0x1E), непрерывный режим
    this->set_i2c_address(0x1E);
    if (!this->write_byte(0x02, 0x00)) {
      ESP_LOGE("lsm303_custom", "Магнитометр (0x1E) не отвечает!");
    }
    this->set_i2c_address(0x19);
  }

  void update() override {
    uint8_t accel_buffer[6] = {0};
    uint8_t mag_buffer[6] = {0};

    // Чтение акселерометра (0x19)
    this->set_i2c_address(0x19);
    if (!this->read_bytes(0xA8, accel_buffer, 6)) {
      ESP_LOGW("lsm303_custom", "Ошибка чтения данных акселерометра");
      return;
    }

    // Чтение магнитометра (0x1E)
    this->set_i2c_address(0x1E);
    if (!this->read_bytes(0x03, mag_buffer, 6)) {
      ESP_LOGW("lsm303_custom", "Ошибка чтения данных магнитометра");
      this->set_i2c_address(0x19);
      return;
    }
    this->set_i2c_address(0x19);

    // Сборка сырых 16-битных значений осей
    int16_t ax_raw = (int16_t)((accel_buffer[1] << 8) | accel_buffer[0]) >> 4;
    int16_t ay_raw = (int16_t)((accel_buffer[3] << 8) | accel_buffer[2]) >> 4;
    int16_t az_raw = (int16_t)((accel_buffer[5] << 8) | accel_buffer[4]) >> 4;

    int16_t mx_raw = (int16_t)((mag_buffer[0] << 8) | mag_buffer[1]);
    int16_t mz_raw = (int16_t)((mag_buffer[2] << 8) | mag_buffer[3]);
    int16_t my_raw = (int16_t)((mag_buffer[4] << 8) | mag_buffer[5]);

    // Перевод в физические единицы: ускорение в g, магнитное поле в микротеслах (µT)
    float ax_g = (float)ax_raw / 4096.0f;
    float ay_g = (float)ay_raw / 4096.0f;
    float az_g = (float)az_raw / 4096.0f;

    float mx_ut = ((float)mx_raw / 1100.0f) * 100.0f;
    float my_ut = ((float)my_raw / 1100.0f) * 100.0f;
    float mz_ut = ((float)mz_raw / 980.0f) * 100.0f;

    // Публикация осей ускорения
    if (this->accel_x_sensor_ != nullptr) this->accel_x_sensor_->publish_state(ax_g);
    if (this->accel_y_sensor_ != nullptr) this->accel_y_sensor_->publish_state(ay_g);
    if (this->accel_z_sensor_ != nullptr) this->accel_z_sensor_->publish_state(az_g);

    // Публикация осей магнитометра
    if (this->mag_x_sensor_ != nullptr) this->mag_x_sensor_->publish_state(mx_ut);
    if (this->mag_y_sensor_ != nullptr) this->mag_y_sensor_->publish_state(my_ut);
    if (this->mag_z_sensor_ != nullptr) this->mag_z_sensor_->publish_state(mz_ut);

    if (ax_raw == 0 && ay_raw == 0 && az_raw == 0) return;

    // Расчет углов Pitch, Roll и Heading с компенсацией наклона
    float pitch = std::atan2((float)ax_raw, std::sqrt((float)ay_raw * ay_raw + (float)az_raw * az_raw));
    float roll = std::atan2((float)ay_raw, std::sqrt((float)ax_raw * ax_raw + (float)az_raw * az_raw));

    if (this->pitch_sensor_ != nullptr) this->pitch_sensor_->publish_state(pitch * 180.0f / M_PI);
    if (this->roll_sensor_ != nullptr) this->roll_sensor_->publish_state(roll * 180.0f / M_PI);

    float cos_pitch = std::cos(pitch);
    float sin_pitch = std::sin(pitch);
    float cos_roll = std::cos(roll);
    float sin_roll = std::sin(roll);

    float mx_comp = mx_ut * cos_pitch + mz_ut * sin_pitch;
    float my_comp = mx_ut * sin_roll * sin_pitch + my_ut * cos_roll - mz_ut * sin_roll * cos_pitch;

    float heading = std::atan2(my_comp, mx_comp) * 180.0f / M_PI;
    if (heading < 0) heading += 360.0f;

    if (this->heading_sensor_ != nullptr) this->heading_sensor_->publish_state(heading);
  }
};

}  // namespace lsm303_custom
