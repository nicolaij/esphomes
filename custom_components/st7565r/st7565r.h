#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"

namespace esphome
{
  namespace st7565r
  {

    class ST7565r;

    using st7565r_writer_t = std::function<void(ST7565r &)>;

    class ST7565r : public display::DisplayBuffer,
                   public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                               spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ>
    {
    public:
      void set_dc_pin(GPIOPin *dc_pin) { this->dc_pin_ = dc_pin; }
      void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
      void set_backlight_pin(GPIOPin *backlight_pin) { this->backlight_pin_ = backlight_pin; }

      void set_height(uint16_t height) { this->height_ = height; }
      void set_width(uint16_t width) { this->width_ = width; }

      // ========== INTERNAL METHODS ==========
      // (In most use cases you won't need these)
      void setup() override;
      void dump_config() override;
      float get_setup_priority() const override;
      void update() override;
      void fill(Color color) override;
      void write_display_data();
      void set_contrast(uint8_t val); // 0..31
      display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_BINARY; }

    protected:
      void init_reset_();
      void backlight_(bool onoff);

      int get_height_internal() override;
      int get_width_internal() override;
      size_t get_buffer_length_();
      void display_init_();
      void command_(uint8_t value);

      void draw_absolute_pixel_internal(int x, int y, Color color) override;

      int16_t width_ = 128, height_ = 64;
      GPIOPin *dc_pin_;
      GPIOPin *reset_pin_;
      GPIOPin *backlight_pin_;

    };

  } // namespace st7565r
} // namespace esphome
