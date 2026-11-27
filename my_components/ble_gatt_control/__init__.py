import esphome.codegen as cg
from esphome.components import sensor, switch
from esphome.components.zephyr import zephyr_add_prj_conf
from esphome.const import CONF_ID, Framework
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@local"]

DEPENDENCIES = ["zephyr_ble_server"]

CONF_LED_SWITCH = "led_switch"
CONF_POWER_SWITCH = "power_switch"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_LEVEL = "battery_level"

ble_gatt_control_ns = cg.esphome_ns.namespace("ble_gatt_control")
BleGattControl = ble_gatt_control_ns.class_("BleGattControl", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BleGattControl),
            cv.Required(CONF_LED_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_POWER_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_BATTERY_VOLTAGE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_BATTERY_LEVEL): cv.use_id(sensor.Sensor),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ZEPHYR),
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_led_switch(await cg.get_variable(config[CONF_LED_SWITCH])))
    cg.add(var.set_power_switch(await cg.get_variable(config[CONF_POWER_SWITCH])))

    if CONF_BATTERY_VOLTAGE in config:
        cg.add(
            var.set_voltage_sensor(
                await cg.get_variable(config[CONF_BATTERY_VOLTAGE])
            )
        )
    if CONF_BATTERY_LEVEL in config:
        cg.add(
            var.set_level_sensor(await cg.get_variable(config[CONF_BATTERY_LEVEL]))
        )

    # Standard Battery Service (0x180F / 0x2A19), read + notify
    zephyr_add_prj_conf("BT_BAS", True)
