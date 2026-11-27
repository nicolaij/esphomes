"""Standard GATT Battery Service (0x180F/0x2A19) for nRF52/Zephyr builds."""

import esphome.codegen as cg
from esphome.components import sensor
from esphome.components.zephyr import zephyr_add_prj_conf
from esphome.const import CONF_ID, Framework
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@local"]
DEPENDENCIES = ["zephyr_ble_server"]

CONF_BATTERY_LEVEL = "battery_level"

ble_battery_ns = cg.esphome_ns.namespace("ble_battery")
BleBattery = ble_battery_ns.class_("BleBattery", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BleBattery),
            cv.Required(CONF_BATTERY_LEVEL): cv.use_id(sensor.Sensor),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ZEPHYR),
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_level_sensor(
            await cg.get_variable(config[CONF_BATTERY_LEVEL])
        )
    )
    zephyr_add_prj_conf("BT_BAS", True)
