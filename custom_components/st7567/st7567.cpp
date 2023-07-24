#include "st7567.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/display/display_buffer.h"

// ST7567 Commands
#define ST7567_POWER_ON 0x2F       // internal power supply on
#define ST7567_POWER_CTL 0x28      // internal power supply off
#define ST7567_CONTRAST 0x80       // 0x80 + (0..31)
#define ST7567_SEG_NORMAL 0xA0     // SEG remap normal
#define ST7567_SEG_REMAP 0xA1      // SEG remap reverse (flip horizontal)
#define ST7567_DISPLAY_NORMAL 0xA4 // display ram content
#define ST7567_DISPLAY_TEST 0xA5   // all pixels on
#define ST7567_INVERT_OFF 0xA6     // not inverted
#define ST7567_INVERT_ON 0xA7      // inverted
#define ST7567_DISPLAY_ON 0XAF     // display on
#define ST7567_DISPLAY_OFF 0XAE    // display off
#define ST7567_STATIC_OFF 0xAC
#define ST7567_STATIC_ON 0xAD
#define ST7567_SCAN_START_LINE 0x40 // scrolling 0x40 + (0..63)
#define ST7567_COM_NORMAL 0xC0      // COM remap normal
#define ST7567_COM_REMAP 0xC8       // COM remap reverse (flip vertical)
#define ST7567_SW_RESET 0xE2        // connect RST pin to GND and rely on software reset
#define ST7567_NOP 0xE3             // no operation
#define ST7567_TEST 0xF0

#define ST7567_COL_ADDR_H 0x10 // x pos (0..95) 4 MSB
#define ST7567_COL_ADDR_L 0x00 // x pos (0..95) 4 LSB
#define ST7567_PAGE_ADDR 0xB0  // y pos, 8.5 rows (0..8)
#define ST7567_RMW 0xE0
#define ST7567_RMW_CLEAR 0xEE

#define ST7567_BIAS_9 0xA2
#define ST7567_BIAS_7 0xA3

#define ST7567_VOLUME_FIRST 0x81
#define ST7567_VOLUME_SECOND 0x00

#define ST7567_RESISTOR_RATIO 0x20
#define ST7567_STATIC_REG 0x0
#define ST7567_BOOSTER_FIRST 0xF8
#define ST7567_BOOSTER_234 0
#define ST7567_BOOSTER_5 1
#define ST7567_BOOSTER_6 3

namespace esphome
{
  namespace st7567
  {

    static const char *const TAG = "st7567";

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
      // this->dc_pin_->digital_write(false);
      this->enable();
      this->write_byte(value);
      this->disable();
    }

    void HOT ST7567::write_display_data()
    {
      // this->command_(0x40); // Set start line address=0x00
      for (uint8_t y = 0; y < (uint8_t)this->get_height_internal() / 8; y++)
      {
        this->dc_pin_->digital_write(false);
        this->command_(ST7567_PAGE_ADDR + y); // Set Page
        this->command_(ST7567_COL_ADDR_H);    // Set MSB Column address
        this->command_(ST7567_COL_ADDR_L);    // Set LSB Column address
        // this->command_(ST7567_RMW);
        this->dc_pin_->digital_write(true);

        this->enable();
        this->write_array(&this->buffer_[y * this->get_width_internal()], this->get_width_internal());
        this->disable();

        App.feed_wdt();
      }
      // this->command_(0xA4); //Cancel All Pixel ON
      // this->command_(0xAF); // Display ON(0xAF)
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
      this->dc_pin_->digital_write(false);

      // this->command_(0xae); /* display off */

      // this->command_(0xE2);
      // this->command_(0xAE); /* display off */
      // this->command_(0x40); /* set display start line to 0 */
      // this->command_(0xA1); /* ADC set to reverse */
      // this->command_(0xC0); /* common output mode */
      // this->command_(0xA6); /* display normal, bit val 0: LCD pixel off. */

      // this->command_(0xA2); // Select 1/9 Bias
      // this->command_(0xA3); // Select 1/7 Bias
      // this->command_(0xA0); // Select SEG Normal Direction
      // this->command_(0xC0); // Select COM Normal Direction
      // this->command_(0x24); // Select Regulation Ratio=5.0
      // this->command_(0x81); // Set EV Command
      // this->command_(0x20); // Set EV=32

      // this->command_(0x28 | 4); // Booster ON
      // delay(50);
      // this->command_(0x28 | 6); // Regulator ON
      // delay(50);
      // this->command_(0x28 | 7); // Follower ON
      // delay(50);

      // this->command_(0x23); /* v0 voltage resistor ratio */

      // this->command_(0x81);	/* set contrast, contrast value*/
      // this->command_(200>>2);	/* set contrast, contrast value*/

      // this->command_(0xae); /* display off */
      // this->command_(0xA5); /* enter powersafe: all pixel on, issue 142 */

      this->command_(ST7567_BIAS_9);
      this->command_(ST7567_SEG_NORMAL);
      this->command_(ST7567_COM_REMAP);
      this->command_(ST7567_POWER_CTL | 0x4);
      this->command_(ST7567_POWER_CTL | 0x6);
      this->command_(ST7567_POWER_CTL | 0x7);
      // this->command_(ST7567_RESISTOR_RATIO | 0x6);
      this->command_(ST7567_SCAN_START_LINE);
      this->command_(ST7567_DISPLAY_ON);
      this->command_(ST7567_DISPLAY_NORMAL);

      //set_contrast(31);
      this->command_(200>>2);	/* set contrast, contrast value*/


      //this->write_display_data();
    }

    void ST7567::set_contrast(uint8_t val) // 0..31
    {
      // now write the new contrast level to the display (0x81)
      this->dc_pin_->digital_write(false);
      this->command_(ST7567_VOLUME_FIRST);
      this->command_(ST7567_VOLUME_SECOND | (val & 0x3f));
    }
  } // namespace st7567
} // namespace esphome
