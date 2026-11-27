"""Bluetooth Environmental Sensing Service (0x181A) for nRF52/Zephyr builds."""

import esphome.codegen as cg
from esphome.components import sensor
from esphome.const import CONF_ID, Framework
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@local"]
DEPENDENCIES = ["zephyr_ble_server"]

CONF_CPU_TEMPERATURE = "cpu_temperature"

ble_environmental_sensing_ns = cg.esphome_ns.namespace(
    "ble_environmental_sensing"
)
BleEnvironmentalSensing = ble_environmental_sensing_ns.class_(
    "BleEnvironmentalSensing", cg.Component
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BleEnvironmentalSensing),
            cv.Required(CONF_CPU_TEMPERATURE): cv.use_id(sensor.Sensor),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ZEPHYR),
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_cpu_temperature_sensor(
            await cg.get_variable(config[CONF_CPU_TEMPERATURE])
        )
    )
