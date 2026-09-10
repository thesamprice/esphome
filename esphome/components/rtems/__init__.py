import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    KEY_CORE,
    KEY_FRAMEWORK_VERSION,
    KEY_TARGET_FRAMEWORK,
    KEY_TARGET_PLATFORM,
    PLATFORM_RTEMS,
    Framework,
    ThreadModel,
    Toolchain,
)
from esphome.core import CORE
from esphome.types import ConfigType

from .const import CONF_BSP, CONF_TOOLS_PREFIX, KEY_ARCH, KEY_BOARD, KEY_BSP, KEY_RTEMS

CODEOWNERS = ["@thesamprice"]
# No AUTO_LOAD yet.  "preferences" is what the other platforms pull in here,
# but it needs a backend this platform does not have; adding it now would make
# the skeleton fail for a reason that belongs to a later milestone.
IS_TARGET_PLATFORM = True


def _validate_bsp(value: str) -> str:
    """An RTEMS BSP is spelled ``arch/board``, e.g. ``riscv/esp32c3db``.

    That is the spelling RTEMS itself uses everywhere -- waf configs, the BSP
    list, the documentation -- so accepting anything else here would mean
    translating in both directions for no gain.
    """
    value = cv.string_strict(value)
    parts = value.split("/")
    if len(parts) != 2 or not all(parts):
        raise cv.Invalid(
            f"BSP must be written as 'arch/board', for example "
            f"'riscv/esp32c3db', got '{value}'"
        )
    return value


def set_core_data(config: ConfigType) -> ConfigType:
    arch, board = config[CONF_BSP].split("/")
    CORE.data[KEY_RTEMS] = {
        KEY_BSP: config[CONF_BSP],
        KEY_ARCH: arch,
        KEY_BOARD: board,
    }
    CORE.data[KEY_CORE][KEY_TARGET_PLATFORM] = PLATFORM_RTEMS
    CORE.data[KEY_CORE][KEY_TARGET_FRAMEWORK] = Framework.RTEMS
    # RTEMS 7 is the line this targets.  The real version comes from the BSP's
    # pkg-config file at build time; this is only what config validation needs
    # in order to answer version constraints before any tool has been run.
    CORE.data[KEY_CORE][KEY_FRAMEWORK_VERSION] = cv.Version(7, 0, 0)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_BSP): _validate_bsp,
            # Where the RTEMS tools and the installed BSP live.  Defaults to
            # the RSB's conventional prefix; the build backend resolves the
            # BSP's pkg-config file underneath it.
            cv.Optional(CONF_TOOLS_PREFIX): cv.string_strict,
        }
    ),
    cv.resolve_toolchain("rtems", (Toolchain.RTEMS,), Toolchain.RTEMS),
    set_core_data,
)


async def to_code(config: ConfigType) -> None:
    arch, board = config[CONF_BSP].split("/")

    cg.add_build_flag("-DUSE_RTEMS")
    cg.add_define("ESPHOME_BOARD", board)
    cg.add_define("ESPHOME_VARIANT", f"RTEMS {arch}/{board}")

    # RTEMS is multi-threaded, but that is only half the question ESPHome asks
    # here.  The other half is whether the CPU has atomic read-modify-write,
    # and on the first target it does not: riscv/esp32c3db builds
    # -march=rv32imc, which has no A extension, so there is no LR/SC and no
    # AMO.  RTEMS itself acknowledges this by linking a software
    # __atomic_test_and_set for the BSP.
    #
    # MULTI_NO_ATOMICS is the model for exactly that shape -- it exists for
    # LibreTiny's BK7231N and ARM968E-S, which have the same gap -- and it
    # keeps libatomic out of the image, which matters on a part with 320 KiB.
    #
    # A BSP whose architecture does have atomics wants MULTI_ATOMICS instead.
    # That is a per-BSP property rather than a property of RTEMS, so it will
    # have to be derived from the BSP rather than hardcoded here once a second
    # board exists.
    cg.add_define(ThreadModel.MULTI_NO_ATOMICS)

    cg.add_build_flag("-std=gnu++20")
