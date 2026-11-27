"""Custom MCUmgr/SMP group for port control and telemetry."""

import esphome.codegen as cg
from esphome.components import sensor, switch
from esphome.components.zephyr import zephyr_add_prj_conf
from esphome.const import CONF_ID, Framework
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@local"]
DEPENDENCIES = ["ota"]

CONF_LED_SWITCH = "led_switch"
CONF_POWER_SWITCH = "power_switch"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_LEVEL = "battery_level"
CONF_CPU_TEMPERATURE = "cpu_temperature"

mcumgr_control_ns = cg.esphome_ns.namespace("mcumgr_control")
McumgrControl = mcumgr_control_ns.class_("McumgrControl", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(McumgrControl),
            cv.Required(CONF_LED_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_POWER_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_BATTERY_VOLTAGE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_BATTERY_LEVEL): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_CPU_TEMPERATURE): cv.use_id(sensor.Sensor),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ZEPHYR),
)


async def to_code(config: ConfigType) -> None:
    # OTA over BLE writes internal flash; a page erase can stall the CPU for
    # tens of milliseconds. The Zephyr default supervision timeout is only
    # 420 ms, which makes the link drop under load. Use 4 s and request faster
    # connection parameters only while SMP traffic is active.
    zephyr_add_prj_conf("BT_PERIPHERAL_PREF_TIMEOUT", 400)
    zephyr_add_prj_conf("MCUMGR_TRANSPORT_BT_CONN_PARAM_CONTROL", True)
    zephyr_add_prj_conf("MCUMGR_TRANSPORT_BT_CONN_PARAM_CONTROL_MIN_INT", 12)
    zephyr_add_prj_conf("MCUMGR_TRANSPORT_BT_CONN_PARAM_CONTROL_MAX_INT", 24)
    zephyr_add_prj_conf("MCUMGR_TRANSPORT_BT_CONN_PARAM_CONTROL_LATENCY", 0)
    zephyr_add_prj_conf("MCUMGR_TRANSPORT_BT_CONN_PARAM_CONTROL_TIMEOUT", 400)
    zephyr_add_prj_conf(
        "MCUMGR_TRANSPORT_BT_CONN_PARAM_CONTROL_RESTORE_TIME", 5000
    )
    zephyr_add_prj_conf("MCUMGR_TRANSPORT_BT_CONN_PARAM_CONTROL_RETRY_TIME", 1000)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_led_switch(await cg.get_variable(config[CONF_LED_SWITCH])))
    cg.add(
        var.set_power_switch(await cg.get_variable(config[CONF_POWER_SWITCH]))
    )

    if CONF_BATTERY_VOLTAGE in config:
        cg.add(
            var.set_voltage_sensor(
                await cg.get_variable(config[CONF_BATTERY_VOLTAGE])
            )
        )
    if CONF_BATTERY_LEVEL in config:
        cg.add(
            var.set_level_sensor(
                await cg.get_variable(config[CONF_BATTERY_LEVEL])
            )
        )
    if CONF_CPU_TEMPERATURE in config:
        cg.add(
            var.set_cpu_temperature_sensor(
                await cg.get_variable(config[CONF_CPU_TEMPERATURE])
            )
        )
