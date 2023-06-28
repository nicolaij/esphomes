import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_ADDRESS, CONF_RECEIVE_TIMEOUT, CONF_SENSORS, CONF_NAME
from esphome.components import uart, sensor

DEPENDENCIES = ['uart']
MULTI_CONF = True
CONF_ERRORCODE = 'error_code'
CONF_STATUSCODE = 'status_code'

deltavfd_ns = cg.esphome_ns.namespace('delta_vfd')
VFDComponent = deltavfd_ns.class_('VFDComponent', cg.Component)

CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(VFDComponent),
    cv.Optional(CONF_ADDRESS, default=1): cv.positive_int,
    cv.Optional(CONF_RECEIVE_TIMEOUT, default='1s'): cv.positive_time_period_milliseconds,
}).extend(cv.polling_component_schema('10s')).extend(uart.UART_DEVICE_SCHEMA))


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_timeout(config[CONF_RECEIVE_TIMEOUT]))

    yield cg.register_component(var, config)
    yield uart.register_uart_device(var, config)
