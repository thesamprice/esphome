from typing import Any

from esphome import pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INPUT,
    CONF_INVERTED,
    CONF_MODE,
    CONF_NUMBER,
    CONF_OPEN_DRAIN,
    CONF_OUTPUT,
    CONF_PULLDOWN,
    CONF_PULLUP,
    PLATFORM_RTEMS,
)
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType

from .const import rtems_ns

RTEMSGPIOPin = rtems_ns.class_("RTEMSGPIOPin", cg.InternalGPIOPin)


def _translate_pin(value: Any) -> int:
    """A pin on this platform is a number, and the BSP decides what it means.

    There is no board pin-name table here on purpose: the same RTEMS build
    backend serves any BSP, and inventing names for one board's silkscreen
    would be wrong on the next.  ``GPIOn`` is accepted because every other
    ESPHome platform accepts it and a configuration should not have to change
    spelling to move.
    """
    if isinstance(value, dict) or value is None:
        raise cv.Invalid(
            "This variable only supports pin numbers, not full pin schemas "
            "(with inverted and mode)."
        )
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    if not isinstance(value, str):
        raise cv.Invalid(f"Invalid pin number: {value}")
    try:
        return int(value)
    except ValueError:
        pass
    if value.startswith("GPIO"):
        return cv.int_(value[len("GPIO") :].strip())
    raise cv.Invalid(
        f"Invalid pin '{value}'. On RTEMS a pin is a number, or GPIO<number>; "
        f"which pad that is, is the BSP's business."
    )


def validate_gpio_pin(value: Any) -> int:
    return _translate_pin(value)


RTEMS_PIN_SCHEMA = pins.gpio_base_schema(
    RTEMSGPIOPin,
    validate_gpio_pin,
    # OPEN_DRAIN is accepted, but it is emulated rather than configured:
    # <bsp/gpio.h> still has no open-drain mode, so digital_write() drives low
    # or releases the pad to the pull-up, which is what open drain is.  It
    # costs about six times a push-pull write; see docs/esp32c3-bsp.md.
    modes=[CONF_INPUT, CONF_OUTPUT, CONF_OPEN_DRAIN, CONF_PULLUP, CONF_PULLDOWN],
)


@pins.PIN_SCHEMA_REGISTRY.register(PLATFORM_RTEMS, RTEMS_PIN_SCHEMA)
async def rtems_pin_to_code(config: ConfigType) -> MockObj:
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_pin(config[CONF_NUMBER]))
    if config[CONF_INVERTED]:
        cg.add(var.set_inverted(True))
    cg.add(var.set_flags(pins.gpio_flags_expr(config[CONF_MODE])))
    return var
