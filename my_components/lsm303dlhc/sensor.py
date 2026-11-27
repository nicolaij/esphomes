import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, i2c

lsm303_ns = cg.esphome_ns.namespace("lsm303_custom")
LSM303DLHCCustomSensor = lsm303_ns.class_(
    "LSM303DLHCCustomSensor", cg.PollingComponent, i2c.I2CDevice
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LSM303DLHCCustomSensor),
            cv.Required("heading"): sensor.sensor_schema(
                icon="mdi:compass", unit_of_measurement="°", accuracy_decimals=1
            ),
            cv.Required("pitch"): sensor.sensor_schema(
                icon="mdi:axis-x-rotate-clockwise", unit_of_measurement="°", accuracy_decimals=1
            ),
            cv.Required("roll"): sensor.sensor_schema(
                icon="mdi:axis-y-rotate-clockwise", unit_of_measurement="°", accuracy_decimals=1
            ),
            # НОВЫЕ СЕНСОРЫ: Ускорение (g)
            cv.Optional("accel_x"): sensor.sensor_schema(unit_of_measurement="g", accuracy_decimals=2),
            cv.Optional("accel_y"): sensor.sensor_schema(unit_of_measurement="g", accuracy_decimals=2),
            cv.Optional("accel_z"): sensor.sensor_schema(unit_of_measurement="g", accuracy_decimals=2),
            # НОВЫЕ СЕНСОРЫ: Магнитное поле (µT)
            cv.Optional("mag_x"): sensor.sensor_schema(unit_of_measurement="µT", accuracy_decimals=1),
            cv.Optional("mag_y"): sensor.sensor_schema(unit_of_measurement="µT", accuracy_decimals=1),
            cv.Optional("mag_z"): sensor.sensor_schema(unit_of_measurement="µT", accuracy_decimals=1),
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(i2c.i2c_device_schema(0x19)),
)

async def to_code(config):
    var = cg.new_Pvariable(config[cv.GenerateID()])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    # Привязка базовых датчиков
    for key in ["heading", "pitch", "roll"]:
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(var, f"set_{key}_sensor")(sens))

    # Привязка новых сенсоров (если они объявлены в YAML)
    for key in ["accel_x", "accel_y", "accel_z", "mag_x", "mag_y", "mag_z"]:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_sensor")(sens))
