"""Build backend for RTEMS: generate a ninja file and drive the cross toolchain.

RTEMS has no PlatformIO platform, and it does not need one.  An installed BSP
ships ``<arch>-rtems7-<board>.pc``:

    Cflags:  -march=rv32imc -mabi=ilp32 -isystem<bsp>/lib/include
    Ldflags: -march=rv32imc -mabi=ilp32 -B<bsp>/lib -qrtems -Wl,--gc-sections

That is the whole interface between ESPHome and RTEMS.  Everything else -- the
cross compiler, the arch, the board -- is discoverable from the same file, so
this backend needs no knowledge of RTEMS' own build system: no waf, no cmake,
no RTEMS makefiles.

Ninja rather than a Makefile because ESPHome already depends on it for the
ESP-IDF backend, so it is not a new tool for anyone building this.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

from esphome.const import __version__
from esphome.core import CORE, EsphomeError
from esphome.helpers import mkdir_p, write_file_if_changed

NINJA_FILE_NAME = "build.ninja"

# Sources ESPHome generates or copies into the project.  Everything under src/
# is ours; there is no vendored SDK to exclude, because the SDK is the
# installed BSP and is reached through -isystem.
SOURCE_SUFFIXES = (".cpp", ".c", ".cc", ".S")


def _pkg_config_name() -> str:
    from esphome.components.rtems.const import KEY_ARCH, KEY_BOARD, KEY_RTEMS

    data = CORE.data[KEY_RTEMS]
    return f"{data[KEY_ARCH]}-rtems7-{data[KEY_BOARD]}"


def _tools_prefix() -> Path:
    """Where the RTEMS tools and installed BSPs live."""
    from esphome.components.rtems.const import CONF_TOOLS_PREFIX

    configured = CORE.config.get("rtems", {}).get(CONF_TOOLS_PREFIX)
    if configured:
        return Path(configured).expanduser()
    if env := os.environ.get("RTEMS_TOOLS_PREFIX"):
        return Path(env).expanduser()
    # The RSB's conventional prefix.
    return Path.home() / "rtems" / "7"


def _pkg_config(prefix: Path, *args: str) -> str:
    name = _pkg_config_name()
    pkgdir = prefix / "lib" / "pkgconfig"
    env = dict(os.environ)
    env["PKG_CONFIG_PATH"] = os.pathsep.join(
        [str(pkgdir), env.get("PKG_CONFIG_PATH", "")]
    ).rstrip(os.pathsep)
    exe = shutil.which("pkg-config")
    if exe is None:
        raise EsphomeError(
            "pkg-config is required to build for RTEMS: it is how the BSP's "
            "compiler and linker flags are discovered."
        )
    proc = subprocess.run(
        [exe, *args, name], capture_output=True, text=True, env=env, check=False
    )
    if proc.returncode != 0:
        raise EsphomeError(
            f"No pkg-config file for RTEMS BSP '{name}' under {pkgdir}.\n"
            f"The BSP is probably built but not installed -- run './waf install' "
            f"in the RTEMS tree, which is what creates the .pc file.\n"
            f"pkg-config said: {proc.stderr.strip()}"
        )
    return proc.stdout.strip()


def _bsp_libdir(prefix: Path) -> Path:
    """Where the BSP's libraries are, which pkg-config states as -B."""
    from esphome.components.rtems.const import KEY_ARCH, KEY_BOARD, KEY_RTEMS

    data = CORE.data[KEY_RTEMS]
    return prefix / f"{data[KEY_ARCH]}-rtems7" / data[KEY_BOARD] / "lib"


def _find_sources(src_dir: Path) -> list[Path]:
    return sorted(
        p for p in src_dir.rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES
    )


def _ninja_escape(path: str) -> str:
    """Ninja escapes only these three in paths, and only with '$'."""
    return path.replace("$", "$$").replace(" ", "$ ").replace(":", "$:")


def get_ninja_content() -> str:
    prefix = _tools_prefix()
    from esphome.components.rtems.const import KEY_ARCH, KEY_RTEMS

    arch = CORE.data[KEY_RTEMS][KEY_ARCH]

    cflags = _pkg_config(prefix, "--cflags")
    ldflags = _pkg_config(prefix, "--libs")

    # rtems-lwip installs liblwip.a into the BSP's own lib directory -- the one
    # the .pc file already points -B at -- but ships no pkg-config file of its
    # own, so nothing in those flags mentions it.
    #
    # Link it when it is there.  Conditioning on the file rather than on which
    # components the configuration uses keeps this out of the business of
    # guessing: a BSP without networking has no liblwip.a and would fail to
    # link against one, and a configuration that uses no socket drops it at
    # --gc-sections anyway.
    lwip = _bsp_libdir(prefix) / "liblwip.a"
    if lwip.is_file():
        ldflags = f"{ldflags} -llwip"

    cxx = prefix / "bin" / f"{arch}-rtems7-g++"
    objcopy = prefix / "bin" / f"{arch}-rtems7-objcopy"
    for tool in (cxx, objcopy):
        if not tool.is_file():
            raise EsphomeError(
                f"Missing RTEMS cross tool: {tool}\n"
                f"Set 'tools_prefix' in the rtems: block, or RTEMS_TOOLS_PREFIX, "
                f"if the toolchain is installed somewhere other than {prefix}."
            )

    src_dir = CORE.relative_build_path("src")
    sources = _find_sources(src_dir)
    if not sources:
        raise EsphomeError(f"No sources to build under {src_dir}")

    build_flags = sorted(CORE.build_flags)
    cxx_flags = sorted(CORE.cxx_build_flags)
    std = CORE.cpp_standard or "gnu++20"

    lines: list[str] = [
        f"# Auto generated by ESPHome {__version__}",
        "ninja_required_version = 1.10",
        "",
        f"cxx = {_ninja_escape(str(cxx))}",
        f"objcopy = {_ninja_escape(str(objcopy))}",
        f"bsp_cflags = {cflags}",
        f"bsp_ldflags = {ldflags}",
        f"build_flags = {' '.join(build_flags)}",
        f"cxx_flags = {' '.join(cxx_flags)} -std={std}",
        f"includes = -I{_ninja_escape(str(src_dir))}",
        "",
        # deps = gcc plus -MMD gives ninja real header dependencies, so an edit
        # to a generated header rebuilds what included it rather than nothing.
        "rule cxx_compile",
        "  command = $cxx -MMD -MF $out.d $bsp_cflags $includes $build_flags "
        "$cxx_flags -c $in -o $out",
        "  description = CXX $out",
        "  depfile = $out.d",
        "  deps = gcc",
        "",
        "rule link",
        "  command = $cxx $bsp_cflags $build_flags $cxx_flags @$out.rsp "
        "$bsp_ldflags -o $out",
        "  description = LINK $out",
        "  rspfile = $out.rsp",
        "  rspfile_content = $in",
        "",
        # The ESP32-C3 boots from flash in the ROM's direct boot mode: the ELF's
        # load addresses are already flash offsets, so objcopy places the image
        # with no further work.  A BSP that boots some other way wants a
        # different final step, which is why this is a named rule rather than
        # part of the link.
        "rule objcopy_bin",
        "  command = $objcopy -O binary $in $out",
        "  description = BIN $out",
        "",
    ]

    objects: list[str] = []
    for source in sources:
        rel = source.relative_to(src_dir)
        obj = f"obj/{rel.with_suffix(rel.suffix + '.o')}"
        objects.append(obj)
        lines.append(
            f"build {_ninja_escape(obj)}: cxx_compile {_ninja_escape(str(source))}"
        )

    elf = f"{CORE.name}.elf"
    binary = f"{CORE.name}.bin"
    lines += [
        "",
        f"build {_ninja_escape(elf)}: link " + " ".join(_ninja_escape(o) for o in objects),
        f"build {_ninja_escape(binary)}: objcopy_bin {_ninja_escape(elf)}",
        "",
        f"default {_ninja_escape(binary)}",
        "",
    ]
    return "\n".join(lines)


def write_project() -> None:
    mkdir_p(CORE.build_path)
    write_file_if_changed(
        CORE.relative_build_path(NINJA_FILE_NAME), get_ninja_content()
    )


def run_compile(verbose: bool = False) -> int:
    ninja = shutil.which("ninja")
    if ninja is None:
        raise EsphomeError("ninja is required to build for RTEMS but was not found")
    cmd = [ninja, "-f", NINJA_FILE_NAME]
    if verbose:
        cmd.append("-v")
    return subprocess.run(cmd, cwd=CORE.build_path, check=False).returncode
