import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_GATEWAY,
    CONF_ID,
    CONF_SUBNET,
    KEY_CORE,
    KEY_FRAMEWORK_VERSION,
    KEY_TARGET_FRAMEWORK,
    KEY_TARGET_PLATFORM,
    PLATFORM_RTEMS,
    Framework,
    ThreadModel,
    Toolchain,
)
from esphome.core import CORE, EsphomeError
from esphome.types import ConfigType

from .const import (
    CONF_BSP,
    CONF_TOOLS_PREFIX,
    KEY_ARCH,
    KEY_BOARD,
    KEY_BSP,
    KEY_RTEMS,
    rtems_ns,
)

CONF_NETWORK = "network"
CONF_MAC_ADDRESS = "mac_address"

RTEMSNetwork = rtems_ns.class_("RTEMSNetwork", cg.Component)


def _ip_to_u32(value) -> int:
    """An IPv4 address as a host-order 32-bit value, which is what lwip wants."""
    parts = [int(p) for p in str(value).split(".")]
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]


NETWORK_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RTEMSNetwork),
        # Static addressing when an address is given, DHCP otherwise.  There is
        # no `dhcp: true` key because that would allow saying both.
        cv.Optional(CONF_ADDRESS): cv.ipv4address,
        cv.Optional(CONF_SUBNET, default="255.255.255.0"): cv.ipv4address,
        cv.Optional(CONF_GATEWAY): cv.ipv4address,
        # Locally administered by default, so a board with no assigned address
        # cannot collide with a real card.
        cv.Optional(CONF_MAC_ADDRESS, default="02:52:54:00:12:34"): cv.mac_address,
    }
)


def _validate_network(config: ConfigType) -> ConfigType:
    net = config.get(CONF_NETWORK)
    if net is None:
        return config
    if CONF_ADDRESS in net and CONF_GATEWAY not in net:
        raise cv.Invalid(
            "'gateway' is required when 'address' is given: a static address "
            "with no route reaches only its own subnet.",
            [CONF_NETWORK, CONF_GATEWAY],
        )
    return config

CODEOWNERS = ["@thesamprice"]
AUTO_LOAD = ["preferences"]
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
            # Not `ethernet:`.  That component drives a MAC and an external PHY
            # over SMI; which controller this board has and how it is wired is
            # decided when the BSP is built, so all a configuration can say is
            # the address.
            cv.Optional(CONF_NETWORK): NETWORK_SCHEMA,
        }
    ),
    _validate_network,
    cv.resolve_toolchain("rtems", (Toolchain.RTEMS,), Toolchain.RTEMS),
    set_core_data,
)


async def to_code(config: ConfigType) -> None:
    arch, board = config[CONF_BSP].split("/")

    cg.add_build_flag("-DUSE_RTEMS")
    cg.add_define("ESPHOME_BOARD", board)
    cg.add_define("ESPHOME_VARIANT", f"RTEMS {arch}/{board}")

    # MULTI_ATOMICS, with a caveat worth knowing.
    #
    # On the hardware facts alone MULTI_NO_ATOMICS looks right: riscv/esp32c3db
    # builds -march=rv32imc, which has no A extension, so there is no LR/SC and
    # no AMO, and RTEMS links a software __atomic_test_and_set for the BSP.
    #
    # But ESPHome currently conflates "no hardware atomics" with "therefore
    # FreeRTOS".  core/freertos_queue.h is guarded on
    # ESPHOME_THREAD_MULTI_NO_ATOMICS and includes <FreeRTOS.h>
    # unconditionally, and the generated esphome.h includes it, so selecting
    # that model on a non-FreeRTOS RTOS does not compile.  The only platform
    # using it today is LibreTiny's BK72xx, which is FreeRTOS-based, so the
    # coupling has never been exercised.
    #
    # MULTI_ATOMICS works because the toolchain ships libatomic for the
    # rv32im/ilp32 multilib: GCC emits calls for the read-modify-write
    # operations the CPU cannot do inline.  It costs a few KB of flash, which
    # is the cheap resource here -- the code region is 1 MiB and the
    # constraint on this part is 320 KiB of RAM.
    #
    # The caveat: libatomic's RMW fallback is lock based, so it is not
    # obviously ISR-safe.  Plain 8-bit loads and stores are unaffected, and
    # today the only RMW on a cross-context flag is wake_request_take(), which
    # runs on the main loop.  Confirm that before an ISR does an atomic RMW.
    cg.add_define(ThreadModel.MULTI_ATOMICS)

    # ESPHome's core uses strcasestr, which newlib guards on __GNU_VISIBLE.
    # GCC defines _GNU_SOURCE for C++ on glibc targets but not on newlib, so
    # without this the core does not compile -- ten errors from one macro.
    cg.add_build_flag("-D_GNU_SOURCE")

    # RTEMS has a 64-bit monotonic clock, rtems_clock_get_uptime_nanoseconds(),
    # so millis_64() reads it directly.  ESPHome's fallback tracks 32-bit
    # rollover with static state and a mutex taken every 49.7 days, and none of
    # that is needed when the clock is already 64-bit.
    cg.add_define("USE_NATIVE_64BIT_TIME")

    cg.add_build_flag("-std=gnu++20")

    if (net := config.get(CONF_NETWORK)) is not None:
        cg.add_define("USE_RTEMS_NETWORK")
        var = cg.new_Pvariable(net[CONF_ID])
        await cg.register_component(var, net)
        mac = net[CONF_MAC_ADDRESS]
        cg.add(var.set_mac_address(*mac.parts))
        if CONF_ADDRESS in net:
            cg.add(var.set_use_dhcp(False))
            cg.add(
                var.set_static_ip(
                    _ip_to_u32(net[CONF_ADDRESS]),
                    _ip_to_u32(net[CONF_SUBNET]),
                    _ip_to_u32(net[CONF_GATEWAY]),
                )
            )


def run_compile(args, config: ConfigType) -> bool:
    """Build the generated project with the RTEMS cross toolchain.

    ``__main__.compile_program`` looks for this on the target platform module
    and lets it take the build over, so RTEMS needs no arm in that dispatch.
    Returning False would hand back to the PlatformIO path, which cannot serve
    this platform, so failures raise instead.
    """
    from esphome.build_gen import rtems

    rc = rtems.run_compile(getattr(args, "verbose", False))
    if rc != 0:
        raise EsphomeError(f"ninja failed with exit code {rc}")
    return True


# force import gpio to register pin schema
from .gpio import rtems_pin_to_code  # noqa: F401
