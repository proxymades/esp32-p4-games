#!/usr/bin/env python3
"""Package only the SD-based play image. Run with the ESP-IDF Python."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, default=ROOT / "build-play")
args = parser.parse_args()
build = args.build.resolve()
config = (build / "sdkconfig").read_text().splitlines()
if "CONFIG_P4GAME_PLAY=y" not in config or "CONFIG_P4GAME_ROM_TEST=y" in config:
    raise SystemExit("Package requires the normal play build, without an embedded ROM.")
symbols = subprocess.run(["riscv32-esp-elf-nm", str(build / "p4nano_games.elf")],
                         capture_output=True, text=True, check=True).stdout
if "_binary_test_rom" in symbols:
    raise SystemExit("Embedded ROM symbols found: refusing to package.")
flash = json.loads((build / "flasher_args.json").read_text())
out = ROOT / "firmware"
out.mkdir(exist_ok=True)
image = out / "p4nano-games.bin"
command = [sys.executable, "-m", "esptool", "--chip", "esp32p4", "merge_bin",
           "-o", str(image), *flash["write_flash_args"]]
parts = sorted(flash["flash_files"].items(), key=lambda p: int(p[0], 0))
for offset, name in parts:
    command.extend([offset, str(build / name)])
subprocess.run(command, check=True)
digest = hashlib.sha256(image.read_bytes()).hexdigest()
(out / "SHA256SUMS").write_text(digest + "  " + image.name + "\n")
manifest = {
    "target": "esp32p4",
    "board": "Waveshare ESP32-P4-NANO",
    "tested_chip_revision": "1.3",
    "idf_version": "5.4.4",
    "flash_size": "16MB",
    "mode": "play",
    "embedded_rom": False,
    "write_offset": "0x0",
    "sha256": digest,
    "size": image.stat().st_size,
    "sdkconfig_sha256": hashlib.sha256((build / "sdkconfig").read_bytes()).hexdigest(),
    "parts": [{"offset": offset, "name": name,
               "sha256": hashlib.sha256((build / name).read_bytes()).hexdigest()}
              for offset, name in parts],
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("Packaged:", image.name, digest)
