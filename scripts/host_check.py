#!/usr/bin/env python3
"""Build and run the same core under AddressSanitizer, then render captures."""
import argparse
import concurrent.futures
from pathlib import Path
import struct
import subprocess
import sys
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("rom", type=Path)
parser.add_argument("--frames", type=int, default=3600)
parser.add_argument("--render-every", type=int, default=1)
parser.add_argument("--output", type=Path, default=ROOT / ".private" / "host")
args = parser.parse_args()
if args.render_every < 1:
    parser.error("--render-every must be positive")
rom = args.rom.resolve()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
component = ROOT / "components" / "gwenesis"
core = out / "core"
subprocess.run([sys.executable, str(component / "prepare_core.py"), "--output", str(core)], check=True)
dirs = ["bus", "cpus/M68K", "cpus/Z80", "io", "sound", "vdp", "savestate"]
includes = ["-I" + str(core / "src" / p) for p in dirs]
includes += ["-I" + str(component / "include"), "-I" + str(ROOT / "main")]
sources = [
    core / "src/bus/gwenesis_bus.c", core / "src/cpus/M68K/m68kcpu.c",
    core / "src/cpus/Z80/Z80.c", core / "src/io/gwenesis_io.c",
    core / "src/sound/z80inst.c", core / "src/sound/ym2612.c",
    core / "src/sound/gwenesis_sn76489.c",
    core / "src/vdp/gwenesis_vdp_mem.c", core / "src/vdp/gwenesis_vdp_gfx.c",
    component / "gwenesis_port.c", ROOT / "main/audio_resample.c",
    ROOT / "main/checksum.c", ROOT / "tests/core_check.c",
]
flags = ["-std=gnu11", "-O2", "-g", "-fsanitize=address", "-fno-omit-frame-pointer",
         "-fno-strict-aliasing", "-include", "stdint.h",
         "-DGWENESIS_AUDIO_ACCURATE=1", "-DGWENESIS_AUDIO_CAPACITY=2048"]
objects = [out / ("object-" + str(i) + ".o") for i in range(len(sources))]

def compile_one(pair):
    source, obj = pair
    strict = source in sources[9:]
    warnings = (["-Wall", "-Wextra", "-Werror"] if strict else
                ["-w", "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion"])
    subprocess.run(["cc", *flags, *warnings, *includes, "-c", str(source), "-o", str(obj)], check=True)

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(compile_one, zip(sources, objects)))
binary = out / "core_check"
subprocess.run(["cc", "-fsanitize=address", *map(str, objects), "-lm", "-o", str(binary)], check=True)
with (out / "run.log").open("w") as log:
    subprocess.run([str(binary), str(rom), str(args.frames), str(out), str(args.render_every)],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
print((out / "run.log").read_text())

def png_chunk(name, payload):
    return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload))

for image in out.glob("frame-*.rgb565"):
    raw = image.read_bytes()
    rgb = bytearray()
    for y in range(240):
        rgb.append(0)
        for x in range(320):
            p = struct.unpack_from("<H", raw, (y * 320 + x) * 2)[0]
            r, g, b = p >> 11, (p >> 5) & 63, p & 31
            rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    data = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 2, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress(rgb)) + png_chunk(b"IEND", b""))
    image.with_suffix(".png").write_bytes(data)
with wave.open(str(out / "audio.wav"), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(48000)
    wav.writeframes((out / "audio.s16le").read_bytes())
print("Captures:", out)
