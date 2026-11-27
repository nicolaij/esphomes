#include "mcumgr_control.h"

#ifdef USE_ZEPHYR

#include "esphome/core/log.h"

#include <zephyr/kernel.h>

#if defined(CONFIG_MCUMGR)

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <cmath>
#include <cstdint>

#endif  // CONFIG_MCUMGR

namespace esphome::mcumgr_control {

static const char *const TAG = "mcumgr_control";  // NOLINT

#define MCUMGR_CTRL_GROUP_ID MGMT_GROUP_ID_PERUSER
#define MCUMGR_CTRL_CMD_STATUS 0
#define MCUMGR_CTRL_CMD_WRITE 1

#if defined(CONFIG_MCUMGR)

static McumgrControl *global_mcumgr_control = nullptr;  // NOLINT

static bool put_float_or_nil(zcbor_state_t *zse, float value) {
  if (std::isfinite(value)) {
    return zcbor_float32_put(zse, value);
  }
  return zcbor_nil_put(zse, nullptr);
}

static int mcumgr_status_read(struct smp_streamer *ctxt) {
  zcbor_state_t *zse = ctxt->writer->zs;
  auto *self = global_mcumgr_control;
  if (self == nullptr) {
    return MGMT_ERR_EUNKNOWN;
  }

  const bool led = self->led_ != nullptr && self->led_->state;
  const bool pwr = self->power_ != nullptr && self->power_->state;
  const float volts = self->voltage_ != nullptr ? self->voltage_->state : NAN;
  const float percent = self->level_ != nullptr ? self->level_->state : NAN;
  const float cpu = self->cpu_temperature_ != nullptr
                        ? self->cpu_temperature_->state
                        : NAN;

  uint32_t millivolts = 0;
  if (std::isfinite(volts)) {
    millivolts = static_cast<uint32_t>(lroundf(volts * 1000.0f));
  }

  bool ok =
      zcbor_tstr_put_lit(zse, "led") && zcbor_bool_put(zse, led) &&
      zcbor_tstr_put_lit(zse, "pwr") && zcbor_bool_put(zse, pwr) &&
      zcbor_tstr_put_lit(zse, "batt_mv") &&
      zcbor_uint32_put(zse, millivolts) &&
      zcbor_tstr_put_lit(zse, "batt_pct") && put_float_or_nil(zse, percent) &&
      zcbor_tstr_put_lit(zse, "cpu_c") && put_float_or_nil(zse, cpu);

  return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int mcumgr_ports_write(struct smp_streamer *ctxt) {
  zcbor_state_t *zse = ctxt->writer->zs;
  zcbor_state_t *zsd = ctxt->reader->zs;
  auto *self = global_mcumgr_control;
  if (self == nullptr) {
    return MGMT_ERR_EUNKNOWN;
  }

  bool led = false;
  bool pwr = false;
  struct zcbor_map_decode_key_val decode[] = {
      {
          .key = {.value = reinterpret_cast<const uint8_t *>("led"),
                  .len = sizeof("led") - 1},
          .decoder = reinterpret_cast<zcbor_decoder_t *>(zcbor_bool_decode),
          .value_ptr = &led,
          .found = false,
      },
      {
          .key = {.value = reinterpret_cast<const uint8_t *>("pwr"),
                  .len = sizeof("pwr") - 1},
          .decoder = reinterpret_cast<zcbor_decoder_t *>(zcbor_bool_decode),
          .value_ptr = &pwr,
          .found = false,
      },
  };

  size_t decoded = 0;
  if (zcbor_map_decode_bulk(zsd, decode, ARRAY_SIZE(decode), &decoded) != 0) {
    return MGMT_ERR_EINVAL;
  }

  const bool has_led =
      zcbor_map_decode_bulk_key_found(decode, ARRAY_SIZE(decode), "led");
  const bool has_pwr =
      zcbor_map_decode_bulk_key_found(decode, ARRAY_SIZE(decode), "pwr");
  if (!has_led && !has_pwr) {
    return MGMT_ERR_EINVAL;
  }
  if ((has_led && self->led_ == nullptr) ||
      (has_pwr && self->power_ == nullptr)) {
    return MGMT_ERR_ENOENT;
  }

  self->apply_ports(has_led, led, has_pwr, pwr);

  const bool led_state =
      has_led ? led : (self->led_ != nullptr && self->led_->state);
  const bool pwr_state =
      has_pwr ? pwr : (self->power_ != nullptr && self->power_->state);

  bool ok = zcbor_tstr_put_lit(zse, "ok") && zcbor_bool_put(zse, true) &&
            zcbor_tstr_put_lit(zse, "led") &&
            zcbor_bool_put(zse, led_state) &&
            zcbor_tstr_put_lit(zse, "pwr") &&
            zcbor_bool_put(zse, pwr_state);
  return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static const struct mgmt_handler MCUMGR_CONTROL_HANDLERS[] = {
    [MCUMGR_CTRL_CMD_STATUS] =
        {
            .mh_read = mcumgr_status_read,
            .mh_write = nullptr,
        },
    [MCUMGR_CTRL_CMD_WRITE] =
        {
            .mh_read = nullptr,
            .mh_write = mcumgr_ports_write,
        },
};

static struct mgmt_group MCUMGR_CONTROL_GROUP = {
    .mg_handlers = MCUMGR_CONTROL_HANDLERS,
    .mg_handlers_count = ARRAY_SIZE(MCUMGR_CONTROL_HANDLERS),
    .mg_group_id = MCUMGR_CTRL_GROUP_ID,
};

#endif  // CONFIG_MCUMGR

void McumgrControl::apply_ports(bool has_led, bool led, bool has_pwr,
                               bool pwr) {
  this->defer([this, has_led, led, has_pwr, pwr]() {
    if (has_led && this->led_ != nullptr) {
      if (led) {
        this->led_->turn_on();
      } else {
        this->led_->turn_off();
      }
    }
    if (has_pwr && this->power_ != nullptr) {
      if (pwr) {
        this->power_->turn_on();
      } else {
        this->power_->turn_off();
      }
    }
  });
}

void McumgrControl::setup() {
#if defined(CONFIG_MCUMGR)
  global_mcumgr_control = this;
  mgmt_register_group(&MCUMGR_CONTROL_GROUP);
  ESP_LOGCONFIG(TAG, "MCUmgr group %u registered", MCUMGR_CTRL_GROUP_ID);
#else
  ESP_LOGW(TAG, "MCUmgr is not enabled; control group is unavailable");
#endif
}

void McumgrControl::dump_config() {
  ESP_LOGCONFIG(TAG,
                "MCUmgr control:\n"
                "  LED switch: %s\n"
                "  Power switch: %s\n"
                "  Battery voltage: %s\n"
                "  Battery level: %s\n"
                "  CPU temperature: %s",
                this->led_ != nullptr ? "yes" : "no",
                this->power_ != nullptr ? "yes" : "no",
                this->voltage_ != nullptr ? "yes" : "no",
                this->level_ != nullptr ? "yes" : "no",
                this->cpu_temperature_ != nullptr ? "yes" : "no");
}

}  // namespace esphome::mcumgr_control

#endif  // USE_ZEPHYR
