"""Device Information Service (GATT 0x180A) for Zephyr/nRF52 builds."""

from datetime import datetime

from esphome.components.zephyr import zephyr_add_prj_conf
from esphome.const import CONF_MODEL, Framework, __version__ as esphome_version
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@local"]
DEPENDENCIES = ["zephyr_ble_server"]

CONF_MANUFACTURER = "manufacturer"
CONF_SERIAL_NUMBER = "serial_number"
CONF_FIRMWARE_REVISION = "firmware_revision"
CONF_HARDWARE_REVISION = "hardware_revision"
CONF_SOFTWARE_REVISION = "software_revision"
CONF_PNP = "pnp"
CONF_VENDOR_ID_SOURCE = "vendor_id_source"
CONF_VENDOR_ID = "vendor_id"
CONF_PRODUCT_ID = "product_id"
CONF_PRODUCT_VERSION = "product_version"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_MANUFACTURER, default="ESPHome"): cv.string_strict,
            cv.Optional(CONF_MODEL): cv.string_strict,
            cv.Optional(CONF_SERIAL_NUMBER): cv.string_strict,
            cv.Optional(
                CONF_FIRMWARE_REVISION, default=esphome_version
            ): cv.string_strict,
            cv.Optional(CONF_HARDWARE_REVISION): cv.string_strict,
            cv.Optional(CONF_SOFTWARE_REVISION): cv.string_strict,
            cv.Optional(CONF_PNP): cv.Schema(
                {
                    cv.Optional(
                        CONF_VENDOR_ID_SOURCE, default=1
                    ): cv.int_range(min=1, max=2),
                    cv.Optional(CONF_VENDOR_ID, default=0): cv.int_range(
                        min=0, max=0xFFFF
                    ),
                    cv.Optional(CONF_PRODUCT_ID, default=0): cv.int_range(
                        min=0, max=0xFFFF
                    ),
                    cv.Optional(
                        CONF_PRODUCT_VERSION, default=0x0100
                    ): cv.int_range(min=0, max=0xFFFF),
                }
            ),
        }
    ),
    cv.only_with_framework(Framework.ZEPHYR),
)


async def to_code(config: ConfigType) -> None:
    zephyr_add_prj_conf("BT_DIS", True)
    zephyr_add_prj_conf("BT_DIS_MANUF", config[CONF_MANUFACTURER])

    if CONF_MODEL in config:
        zephyr_add_prj_conf("BT_DIS_MODEL", config[CONF_MODEL])

    zephyr_add_prj_conf("BT_DIS_FW_REV", True)
    zephyr_add_prj_conf("BT_DIS_FW_REV_STR", config[CONF_FIRMWARE_REVISION])

    if CONF_HARDWARE_REVISION in config:
        zephyr_add_prj_conf("BT_DIS_HW_REV", True)
        zephyr_add_prj_conf(
            "BT_DIS_HW_REV_STR", config[CONF_HARDWARE_REVISION]
        )

    software_revision = config.get(CONF_SOFTWARE_REVISION)
    if software_revision is None:
        software_revision = (
            f"ESPHome {esphome_version} build "
            f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
        )
    zephyr_add_prj_conf("BT_DIS_SW_REV", True)
    zephyr_add_prj_conf("BT_DIS_SW_REV_STR", software_revision)

    if CONF_SERIAL_NUMBER in config:
        zephyr_add_prj_conf("BT_DIS_SERIAL_NUMBER", True)
        zephyr_add_prj_conf(
            "BT_DIS_SERIAL_NUMBER_STR", config[CONF_SERIAL_NUMBER]
        )

    pnp = config.get(CONF_PNP)
    if pnp is None:
        zephyr_add_prj_conf("BT_DIS_PNP", False)
        return

    zephyr_add_prj_conf("BT_DIS_PNP", True)
    zephyr_add_prj_conf("BT_DIS_PNP_VID_SRC", pnp[CONF_VENDOR_ID_SOURCE])
    zephyr_add_prj_conf("BT_DIS_PNP_VID", pnp[CONF_VENDOR_ID])
    zephyr_add_prj_conf("BT_DIS_PNP_PID", pnp[CONF_PRODUCT_ID])
    zephyr_add_prj_conf("BT_DIS_PNP_VER", pnp[CONF_PRODUCT_VERSION])
