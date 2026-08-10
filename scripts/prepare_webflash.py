"""
PlatformIO extra script: merge firmware for ESP Web Tools browser flash.

  pio run -e m5sticks3 -t webflash
"""

Import("env")
from pathlib import Path

project_dir = Path(env["PROJECT_DIR"])
build_dir = Path(env.subst("$BUILD_DIR"))
webflash_dir = project_dir / "webflash"
out_bin = webflash_dir / "firmware.bin"
packages_dir = Path(env["PROJECT_PACKAGES_DIR"])


def _find_esptool():
    direct = packages_dir / "tool-esptoolpy" / "esptool.py"
    if direct.exists():
        return direct
    found = list(packages_dir.rglob("esptool.py"))
    return found[0] if found else None


def _find_boot_app0():
    p = packages_dir / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
    if p.exists():
        return p
    found = list(packages_dir.rglob("boot_app0.bin"))
    return found[0] if found else None


def prepare_webflash(source=None, target=None, env=env):
    webflash_dir.mkdir(parents=True, exist_ok=True)

    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    firmware = build_dir / "firmware.bin"
    boot_app0 = _find_boot_app0()
    esptool = _find_esptool()

    for label, p in (
        ("bootloader", bootloader),
        ("partitions", partitions),
        ("firmware", firmware),
        ("boot_app0", boot_app0),
        ("esptool", esptool),
    ):
        if p is None or not Path(p).exists():
            print(f"[webflash] ERROR: missing {label}: {p}")
            return

    # Web Tools cannot patch flash params; merge as dio/8MB/80m
    cmd = (
        f'"{env.subst("$PYTHONEXE")}" "{esptool}" --chip esp32s3 merge_bin '
        f'-o "{out_bin}" --flash_mode dio --flash_freq 80m --flash_size 8MB '
        f'0x0 "{bootloader}" 0x8000 "{partitions}" 0xe000 "{boot_app0}" 0x10000 "{firmware}"'
    )
    print("[webflash] " + cmd)
    env.Execute(cmd)

    if out_bin.exists():
        print(f"[webflash] OK -> {out_bin} ({out_bin.stat().st_size} bytes)")
        print("[webflash] Serve:  npx --yes serve webflash")
        print("[webflash] Then open the printed URL and click Install")
    else:
        print("[webflash] ERROR: merge failed")


env.AddCustomTarget(
    "webflash",
    "$BUILD_DIR/firmware.bin",
    prepare_webflash,
    title="Prepare Web Flash",
    description="Merge firmware for browser flashing (webflash/)",
)
