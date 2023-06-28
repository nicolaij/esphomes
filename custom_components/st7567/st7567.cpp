#include "st7567.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/display/display_buffer.h"

namespace esphome
{
  namespace st7567
  {

    static const char *const TAG = "st7567";

    // ST7567 COMMANDS
    static const uint8_t LCD_DATA = 0xFA;
    static const uint8_t LCD_COMMAND = 0xF8;
    static const uint8_t LCD_CLS = 0x01;
    static const uint8_t LCD_HOME = 0x02;
    static const uint8_t LCD_ADDRINC = 0x06;
    static const uint8_t LCD_DISPLAYON = 0x0C;
    static const uint8_t LCD_DISPLAYOFF = 0x08;
    static const uint8_t LCD_CURSORON = 0x0E;
    static const uint8_t LCD_CURSORBLINK = 0x0F;
    static const uint8_t LCD_BASIC = 0x30;
    static const uint8_t LCD_GFXMODE = 0x36;
    static const uint8_t LCD_EXTEND = 0x34;
    static const uint8_t LCD_TXTMODE = 0x34;
    static const uint8_t LCD_STANDBY = 0x01;
    static const uint8_t LCD_SCROLL = 0x03;
    static const uint8_t LCD_SCROLLADDR = 0x40;
    static const uint8_t LCD_ADDR = 0x80;
    static const uint8_t LCD_LINE0 = 0x80;
    static const uint8_t LCD_LINE1 = 0x90;
    static const uint8_t LCD_LINE2 = 0x88;
    static const uint8_t LCD_LINE3 = 0x98;

    void ST7567::init_reset_()
    {
      if (this->reset_pin_ != nullptr)
      {
        this->reset_pin_->setup();
        this->reset_pin_->digital_write(true);
        delay(1);
        // Trigger Reset
        this->reset_pin_->digital_write(false);
        delay(10);
        // Wake up
        this->reset_pin_->digital_write(true);
      }
    }

    void ST7567::backlight_(bool onoff)
    {
      if (this->backlight_pin_ != nullptr)
      {
        this->backlight_pin_->setup();
        this->backlight_pin_->digital_write(onoff);
      }
    }

    void ST7567::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up ST7567...");
      this->dump_config();
      this->init_reset_();
      this->dc_pin_->setup();
      this->spi_setup();
      this->init_internal_(this->get_buffer_length_());
      display_init_();
    }

    void ST7567::command_(uint8_t value)
    {
      this->dc_pin_->digital_write(false);
      this->enable();
      this->write_byte(value);
      this->disable();
    }

    void HOT ST7567::write_display_data()
    {
      
      this->command_(0x40); // Set start line address=0x00
      //for (uint8_t y = 0; y < (uint8_t)this->get_height_internal() / 8; y++)
      for (uint8_t y = 0; y < 32 / 8; y++)
      {
        this->command_(0xB0 + y); // Set Page
        this->command_(0x10);     // Set MSB Column address
        this->command_(0x00);     // Set LSB Column address
        this->dc_pin_->digital_write(true);
        this->enable();

        for (uint8_t x = 0; x < (uint8_t)this->get_width_internal(); x++)
        {
          this->write_byte(this->buffer_[x + y * this->get_width_internal()]);
        }
        this->disable();        
        App.feed_wdt();
      }
      //this->command_(0xA4); //Cancel All Pixel ON
      this->command_(0xAF); // Display ON(0xAF)
    }

    void ST7567::fill(Color color) { memset(this->buffer_, color.is_on() ? 0xFF : 0x00, this->get_buffer_length_()); }

    void ST7567::dump_config()
    {
      LOG_DISPLAY("", "ST7567", this);
      ESP_LOGCONFIG(TAG, "  Height: %d", this->height_);
      ESP_LOGCONFIG(TAG, "  Width: %d", this->width_);
      LOG_PIN("  CS Pin: ", this->cs_);
      LOG_PIN("  DC Pin: ", this->dc_pin_);
      LOG_PIN("  Reset Pin: ", this->reset_pin_);
      LOG_PIN("  B/L Pin: ", this->backlight_pin_);
      LOG_UPDATE_INTERVAL(this);
    }

    float ST7567::get_setup_priority() const { return setup_priority::PROCESSOR; }

    void ST7567::update()
    {
      this->do_update_();
      this->write_display_data();
    }

    void HOT ST7567::draw_absolute_pixel_internal(int x, int y, Color color)
    {
      if (x >= this->get_width_internal() || x < 0 || y >= this->get_height_internal() || y < 0)
        return;

      uint16_t pos = x + (y / 8) * this->get_width_internal();
      uint8_t subpos = y & 0x07;
      if (color.is_on())
      {
        this->buffer_[pos] |= (1 << subpos);
      }
      else
      {
        this->buffer_[pos] &= ~(1 << subpos);
      }
    }

    int ST7567::get_width_internal() { return this->width_; }

    int ST7567::get_height_internal() { return this->height_; }

    size_t ST7567::get_buffer_length_()
    {
      return size_t(this->get_width_internal()) * size_t(this->get_height_internal()) / 8u;
    }

    void ST7567::display_init_()
    {
      ESP_LOGD(TAG, "Initializing display...");
      // this->command_(0xae); /* display off */

      //this->command_(0xE2);
      this->command_(0xAE); /* display off */
      //this->command_(0x40); /* set display start line to 0 */
      //this->command_(0xA1); /* ADC set to reverse */
      //this->command_(0xC0); /* common output mode */
      //this->command_(0xA6); /* display normal, bit val 0: LCD pixel off. */ 

      //this->command_(0xA2); // Select 1/9 Bias
      //this->command_(0xA3); // Select 1/7 Bias
      //this->command_(0xA0); // Select SEG Normal Direction
      //this->command_(0xC0); // Select COM Normal Direction
      //this->command_(0x24); // Select Regulation Ratio=5.0
      //this->command_(0x81); // Set EV Command
      //this->command_(0x20); // Set EV=32

      this->command_(0x28|4); // Booster ON
      delay(50);
      this->command_(0x28|6); // Regulator ON
      delay(50);
      this->command_(0x28|7); // Follower ON
      delay(50);

      //this->command_(0x23); /* v0 voltage resistor ratio */

      //this->command_(0x81);	/* set contrast, contrast value*/
      //this->command_(200>>2);	/* set contrast, contrast value*/

      //this->command_(0xae); /* display off */
      //this->command_(0xA5); /* enter powersafe: all pixel on, issue 142 */

      this->write_display_data();
    }

  } // namespace st7567
} // namespace esphome
