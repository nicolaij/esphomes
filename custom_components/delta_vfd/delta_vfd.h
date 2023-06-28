#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome
{
  namespace delta_vfd
  {

    const uint8_t READ_BUFFER_LENGTH = 255;
    const char ASCII_CR = 0x0D;
    const char ASCII_LF = 0x0A;

    enum State
    {
      STATE_IDLE = 0,
      STATE_INIT,
      STATE_REPLYWAIT,
      STATE_NOREPLY,
      STATE_ERROR
    };

    class VFDComponent : public uart::UARTDevice, public PollingComponent
    {
    public:
      /// Retrieve the latest sensor values. This operation takes approximately 16ms.
      void update() override;
      void setup() override;
      void loop() override;
      void dump_config() override;
      float get_setup_priority() const override;
      void start(uint16_t freq);
      void stop();

      void set_address(uint8_t mbaddress) { this->mbaddress_ = mbaddress; };
      void set_timeout(unsigned long timeout) { this->timeout_ = timeout; };
      /*void set_error_code_sensor(sensor::Sensor *error_code_sensor) { error_code_sensor_ = error_code_sensor; }*/
      /*void set_status_code_sensor(sensor::Sensor *status_code_sensor) { status_code_sensor_ = status_code_sensor; }*/
      sensor::Sensor *error_code_sensor = new sensor::Sensor();
      sensor::Sensor *status_code_sensor = new sensor::Sensor();
      sensor::Sensor *set_freq_sensor = new sensor::Sensor();
      sensor::Sensor *out_freq_sensor = new sensor::Sensor();
      sensor::Sensor *out_current_sensor = new sensor::Sensor();

    protected:
      void send_cmd_(uint8_t cmd, uint16_t start_address, uint16_t data);
      void send_cmd_(uint8_t cmd, uint16_t start_address, uint16_t data1, uint16_t data2);
      void parse_cmd_(char *message);

      char read_buffer_[READ_BUFFER_LENGTH];
      size_t read_pos_{};
      delta_vfd::State state_{STATE_IDLE};
      uint8_t mbaddress_;
      unsigned long timeout_;

      int later_func_{};
      int later_freq_{};
    };
  } // namespace delta_vfd
} // namespace esphome
