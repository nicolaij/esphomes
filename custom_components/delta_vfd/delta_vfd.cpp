#include "delta_vfd.h"
#include "esphome/core/log.h"

namespace esphome
{
  namespace delta_vfd
  {
    static const char *TAG = "delta_vfd";
    static unsigned long sendtime;

    void VFDComponent::update()
    {
       if (this->state_ != STATE_REPLYWAIT)
      {
        this->state_ = STATE_REPLYWAIT;
        send_cmd_(0x03, 0x0000, 5); //read reg
      }
    };

    void VFDComponent::start(uint16_t freq)
    {
      if (this->state_ != STATE_REPLYWAIT)
      {
        this->state_ = STATE_REPLYWAIT;
        send_cmd_(0x10, 0x2000, 0b10, freq * 5);
      }
      else
      {
        this->later_func_ = 1;
        this->later_freq_ = freq;
      }
    };

    void VFDComponent::stop()
    {
      if (this->state_ != STATE_REPLYWAIT)
      {
        this->state_ = STATE_REPLYWAIT;
        send_cmd_(0x06, 0x2000, 0);
      }
      else
      {
        this->later_func_ = -1;
      }
    };

    void VFDComponent::loop()
    {
      if (this->state_ == STATE_REPLYWAIT)
        if ((millis() - sendtime) > this->timeout_)
        {
          this->state_ = STATE_NOREPLY;
          if (this->later_func_ == -1)
            this->stop();
          else if (this->later_func_ == 1)
            this->start(this->later_freq_);

          this->later_func_ = 0;
        }

      // Read message
      while (this->available())
      {
        uint8_t byte;
        this->read_byte(&byte);

        if (this->read_pos_ == READ_BUFFER_LENGTH)
          this->read_pos_ = 0;

        ESP_LOGVV(TAG, "Buffer pos: %u %d", this->read_pos_, byte); // NOLINT

        if (byte == ASCII_CR)
          continue;

        if (byte >= 0x7F)
          byte = '?'; // need to be valid utf8 string for log functions.

        this->read_buffer_[this->read_pos_] = byte;

        if (this->read_buffer_[this->read_pos_] == ASCII_LF)
        {
          this->read_buffer_[this->read_pos_] = 0;
          this->read_pos_ = 0;

          if (this->state_ == STATE_REPLYWAIT)
          {
            ESP_LOGD(TAG, "Receive: %s - %d", this->read_buffer_, this->state_);
          }
          else
          {
            ESP_LOGD(TAG, "UNEXPECTED: %s - %d", this->read_buffer_, this->state_);
          };

          this->parse_cmd_(this->read_buffer_);

          this->state_ = STATE_IDLE;
        }
        else
        {
          this->read_pos_++;
        };
      }
    };

    char DecToChar(uint8_t c)
    {
      if (c <= 9)
        return c + '0';
      if (c >= 0xA && c <= 0xF)
        return c - 0x0A + 'A';
      return 0;
    };

    void VFDComponent::send_cmd_(uint8_t cmd, uint16_t start_address, uint16_t data)
    {
      uint8_t frame[10];
      frame[0] = mbaddress_;
      frame[1] = cmd;
      frame[2] = start_address >> 8;
      frame[3] = start_address & 0xff;
      frame[4] = data >> 8;
      frame[5] = data & 0xff;
      frame[6] = 0xff - uint8_t(frame[0] + frame[1] + frame[2] + frame[4] + frame[5]) + 1;

      static char send_frame[32];
      char *p = send_frame;
      char c;
      *p = ':';
      p++;
      for (int i = 0; i < 7; i++)
      {
        *p = DecToChar(frame[i] >> 4);
        p++;
        *p = DecToChar(frame[i] & 0x0f);
        p++;
      }
      *p = ASCII_CR;
      p++;
      *p = ASCII_LF;
      p++;
      *p = 0;

      digitalWrite(4, HIGH); 
      digitalWrite(15, HIGH);

      sendtime = millis();

      this->write_str(send_frame);

      int tdelay = ((p - send_frame) * 10 * 1000 ) / 19200 + 1;
      delay(tdelay);

      digitalWrite(4, LOW); 
      digitalWrite(15, LOW);

      ESP_LOGD(TAG, "Send: %s - %d", send_frame, this->state_);
    };

    void VFDComponent::send_cmd_(uint8_t cmd, uint16_t start_address, uint16_t data1, uint16_t data2)
    {
      uint8_t frame[12];
      frame[0] = mbaddress_;
      frame[1] = cmd;
      frame[2] = start_address >> 8;
      frame[3] = start_address & 0xff;
      frame[4] = 0;
      frame[5] = 2;
      frame[6] = 4;
      frame[7] = data1 >> 8;
      frame[8] = data1 & 0xff;
      frame[9] = data2 >> 8;
      frame[10] = data2 & 0xff;
      frame[11] = 0xff - uint8_t(frame[0] + frame[1] + frame[2] + frame[4] + frame[5] + frame[6] + frame[7] + frame[8] + frame[9] + frame[10]) + 1;

      static char send_frame[32];
      char *p = send_frame;
      *p = ':';
      p++;
      for (int i = 0; i < 12; i++)
      {
        *p = DecToChar(frame[i] >> 4);
        p++;
        *p = DecToChar(frame[i] & 0x0f);
        p++;
      }
      *p = ASCII_CR;
      p++;
      *p = ASCII_LF;
      p++;
      *p = 0;

      digitalWrite(4, HIGH); 
      digitalWrite(15, HIGH);

      sendtime = millis();

      this->write_str(send_frame);

      int tdelay = ((p - send_frame) * 10 * 1000 ) / 19200 + 1;
      delay(tdelay);

      digitalWrite(4, LOW); 
      digitalWrite(15, LOW);

      ESP_LOGD(TAG, "Send: %s - %d", send_frame, this->state_);
    };

    uint8_t CharToDec(char c)
    {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      return 0;
    };

    void VFDComponent::parse_cmd_(char *message)
    {
      uint8_t frame[128];
      int i = 0;
      int cnt = 0;
      uint8_t c;

      if (message[i] != ':')
        return;
      i++;

      while (message[i] != 0)
      {
        frame[cnt] = 0;
        if (message[i] == ASCII_CR)
          break;
        if (message[i] == ASCII_LF)
          break;

        c = CharToDec(message[i]);
        frame[cnt] |= c << 4;
        i++;
        c = CharToDec(message[i]);
        frame[cnt] |= c;
        i++;
        cnt++;
        if (cnt == sizeof(frame))
          return;
      }

      cnt--; //без CRC

      if (cnt >= 6)
      {
        uint8_t crc = 0;
        int i = 0;
        while (i < cnt)
        {
          crc += frame[i];
          i++;
        }
        if ((0xff - crc + 1) != frame[i])
        {
          ESP_LOGD(TAG, "CRC ERROR: %s - %d", this->read_buffer_, this->state_);
        }
        else
        {
          ESP_LOGV(TAG, "DATA OK: %s - %d", this->read_buffer_, this->state_);
        }

        if (frame[1] == 0x03)
        {
          uint8_t len = frame[2];
          if (cnt > 12 && len == 10) //запрошено 5 слов
          {
            uint16_t error_code = (uint16_t(frame[3]<<8)) | frame[4];
            uint16_t status_code = (uint16_t(frame[5]<<8)) | frame[6];
            uint16_t set_freq = (uint16_t(frame[7]<<8)) | frame[8];
            uint16_t out_freq = (uint16_t(frame[9]<<8)) | frame[10];
            uint16_t out_current = (uint16_t(frame[11]<<8)) | frame[12];
            this->error_code_sensor->publish_state(error_code);
            this->status_code_sensor->publish_state(status_code);
            this->set_freq_sensor->publish_state(set_freq);
            this->out_freq_sensor->publish_state(out_freq);
            this->out_current_sensor->publish_state(out_current);
          }
        }
      }
    };

    void VFDComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "DELTA VFD:");
      ESP_LOGCONFIG(TAG, "  ADDRESS: %d", this->mbaddress_);
      ESP_LOGCONFIG(TAG, "  Timeout: %ld", this->timeout_);
    };

    float VFDComponent::get_setup_priority() const { return setup_priority::DATA; }

    void VFDComponent::setup()
    {
      pinMode(4, OUTPUT);
      pinMode(15, OUTPUT);
      digitalWrite(4, LOW);
      digitalWrite(15, LOW);
    };

  } // namespace delta_vfd
} // namespace esphome
