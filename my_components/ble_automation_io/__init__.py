"""Bluetooth Automation IO Service (0x1815) for nRF52/Zephyr builds."""

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
CONF_CPU_TEMPERATURE = "cpu_temperature"

ble_automation_io_ns = cg.esphome_ns.namespace("ble_automation_io")
BleAutomationIO = ble_automation_io_ns.class_(
    "BleAutomationIO", cg.Component
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BleAutomationIO),
            cv.Required(CONF_LED_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_POWER_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_BATTERY_VOLTAGE): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CPU_TEMPERATURE): cv.use_id(sensor.Sensor),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ZEPHYR),
)


async def to_code(config: ConfigType) -> None:
    # Needed for the HCI Read RSSI command (otherwise the SDC HCI driver
    # returns UNKNOWN_CMD / -EIO).
    zephyr_add_prj_conf("BT_CTLR_CONN_RSSI", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_led_switch(await cg.get_variable(config[CONF_LED_SWITCH])))
    cg.add(
        var.set_power_switch(await cg.get_variable(config[CONF_POWER_SWITCH]))
    )
    cg.add(
        var.set_voltage_sensor(
            await cg.get_variable(config[CONF_BATTERY_VOLTAGE])
        )
    )
    cg.add(
        var.set_cpu_temperature_sensor(
            await cg.get_variable(config[CONF_CPU_TEMPERATURE])
        )
    )
