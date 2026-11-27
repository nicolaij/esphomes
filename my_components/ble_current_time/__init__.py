"""Bluetooth Current Time Service (0x1805) for nRF52/Zephyr builds."""

import esphome.codegen as cg
from esphome.components import time as time_
from esphome.components.zephyr import (
    zephyr_add_overlay,
    zephyr_add_prj_conf,
)

CODEOWNERS = ["@local"]
DEPENDENCIES = ["zephyr_ble_server"]

ble_current_time_ns = cg.esphome_ns.namespace("ble_current_time")
BleCurrentTime = ble_current_time_ns.class_("BleCurrentTime", time_.RealTimeClock)


def add_rtc_config() -> None:
    # RTC2 is free (RTC0 is used by the Bluetooth controller, RTC1 by the
    # Zephyr system timer) and is clocked from LFCLK/LFXO.
    zephyr_add_prj_conf("COUNTER", True)
    zephyr_add_overlay(
        """
        &rtc2 {
            status = "okay";
        };
        """
    )
