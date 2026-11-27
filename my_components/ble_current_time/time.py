"""ESPHome time source backed by the BLE Current Time Service / RTC2."""

import esphome.codegen as cg
from esphome.components import time as time_
from esphome.const import CONF_ID, Framework
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import BleCurrentTime, add_rtc_config

DEPENDENCIES = ["zephyr_ble_server"]

CONFIG_SCHEMA = cv.All(
    time_.TIME_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BleCurrentTime),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ZEPHYR),
)


async def to_code(config: ConfigType) -> None:
    add_rtc_config()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await time_.register_time(var, config)
