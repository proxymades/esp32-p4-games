#!/usr/bin/env python3
"""Prepare a verified upstream tree with small, documented port fixes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
manifest = json.loads((HERE / "upstream.json").read_text())
for entry in manifest["files"]:
    source = HERE / "upstream" / entry["path"]
    data = source.read_bytes()
    digest = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    if digest != entry["git_blob_sha1"]:
        raise ValueError("Upstream changed: " + entry["path"])
    target = args.output / entry["path"]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)

def change(relative, old, new):
    path = args.output / relative
    text = path.read_text()
    if text.count(old) != 1:
        raise ValueError("Patch context mismatch: " + relative + " / " + old[:70])
    path.write_text(text.replace(old, new))

for relative, name, synth, divisor in [
    ("src/sound/ym2612.c", "ym2612", "YM2612Update", "ym2612->divisor"),
    ("src/sound/gwenesis_sn76489.c", "sn76489", "gwenesis_SN76489_Update", "gwenesis_SN76489.divisor"),
]:
    path = args.output / relative
    text = '#include "gwenesis_profile.h"\n' + path.read_text()
    start_name = "void ym2612_run(" if name == "ym2612" else "void gwenesis_SN76489_run("
    start = text.index(start_name)
    end = text.index("\n}", start) + 2
    body = (
        start_name + "int target) {\n"
        "  extern int gwenesis_audio_overflow;\n"
        "  if (target <= " + name + "_clock) { return; }\n"
        "  int count = (target - " + name + "_clock) / " + divisor + ";\n"
        "  if (count <= 0) { return; }\n"
        "  if (count > GWENESIS_AUDIO_CAPACITY - " + name + "_index) {\n"
        "    gwenesis_audio_overflow = 1;\n"
        "    count = GWENESIS_AUDIO_CAPACITY - " + name + "_index;\n"
        "  }\n"
        "  uint32_t synth_start = gw_cycle_now();\n"
        "  " + synth + "(gwenesis_" + name + "_buffer + " + name + "_index, count);\n"
        "  gw_synth_cycles += gw_cycle_now() - synth_start;\n"
        "  " + name + "_index += count;\n"
        "  " + name + "_clock += count * " + divisor + ";\n"
        "}"
    )
    path.write_text(text[:start] + body + text[end:])

# esp_attr.h was included but no ESP-specific attributes are used.
for relative in ["src/sound/ym2612.c", "src/sound/gwenesis_sn76489.c", "src/sound/z80inst.c", "src/vdp/gwenesis_vdp_gfx.c"]:
    change(relative, "#include <esp_attr.h>", "/* Portable core: no platform-specific attributes required. */")

# The original typedef is 32 bits on ESP32 and 64 bits on macOS.
change("src/sound/gwenesis_sn76489.h", "typedef long signed int INT64;", "typedef int32_t INT64; /* Original 32-bit MCU arithmetic. */")
path = args.output / "src/sound/gwenesis_sn76489.c"
path.write_text(path.read_text().replace("LONG_MIN", "INT32_MIN"))

# Wrap 32-bit RAM accesses at the 64 KiB boundary and use word-aligned loads.
change("src/cpus/M68K/m68k.h",
       "#define FETCH32RAM(A) ( (*(unsigned int *)&M68K_RAM[(A&0XFFFF)] << 16) | (*(unsigned int *)&M68K_RAM[(A&0XFFFF)] >> 16) )",
       "#define FETCH32RAM(A) (((uint32_t)FETCH16RAM(A) << 16) | FETCH16RAM((A) + 2))")
change("src/cpus/M68K/m68k.h",
       "#define WRITE32RAM(A, V) ((*(unsigned int *)&M68K_RAM[(A)&0XFFFF] =( ((V) << 16) | ((V) >> 16) ) ))",
       "#define WRITE32RAM(A, V) do { WRITE16RAM(A, (uint32_t)(V) >> 16); WRITE16RAM((A) + 2, (uint32_t)(V)); } while (0)")
change("src/cpus/M68K/m68k.h",
       "#define FETCH32ROM(A) (((unsigned int)(A) + 3 < gwenesis_rom_size) ? ((*(unsigned int *)&ROM_DATA[(A)] << 16) | (*(unsigned int *)&ROM_DATA[(A)] >> 16)) : 0xFFFFFFFF)",
       "#define FETCH32ROM(A) (((uint32_t)FETCH16ROM(A) << 16) | FETCH16ROM((A) + 2))")
print("Prepared verified Gwenesis core with P4 port fixes")

change("src/vdp/gwenesis_vdp_gfx.c", "for (scr; scr < end; scr += 8)", "for (; scr < end; scr += 8)")


# Skip pixel work when SPI has no free framebuffer, while preserving the
# original sprite-table traversal, pixel limits, masking and overflow state.
change("src/vdp/gwenesis_vdp_gfx.c",
       "void draw_sprites_over_planes(int line)",
       "void draw_sprites_over_planes(int line, bool draw_pixels)")
change("src/vdp/gwenesis_vdp_gfx.c",
       "void draw_sprites(int line)",
       "void draw_sprites(int line, bool draw_pixels)")
gfx = args.output / "src/vdp/gwenesis_vdp_gfx.c"
text = gfx.read_text()
for call in ("draw_pattern_sprite_over_planes(scr + sx + p * 8, name, paty);",
             "draw_pattern_sprite(scr + sx + p * 8, name, paty);"):
    if text.count(call) != 2:
        raise ValueError("Sprite draw patch context mismatch")
    text = text.replace(call, "if (draw_pixels) " + call)
text = text.replace("draw_sprites(line);", "draw_sprites(line, true);")
text = text.replace("draw_sprites_over_planes(line);", "draw_sprites_over_planes(line, true);")
text += """
/* Pixel-free counterpart of render_line with the same observable VDP state. */
void gwenesis_vdp_evaluate_line(int line)
{
    mode_h40 = REG12_MODE_H40;
    if (BITS_GEN(gwenesis_vdp_regs[12], 1, 2) != 0 ||
        line >= (REG1_PAL ? 240 : 224) || REG0_DISABLE_DISPLAY)
        return;
    if (MODE_SHI)
        draw_sprites(line, false);
    else
        draw_sprites_over_planes(line, false);
}
"""
gfx.write_text(text)
change("src/vdp/gwenesis_vdp.h",
       "void gwenesis_vdp_render_line(int line);",
       "void gwenesis_vdp_render_line(int line);\nvoid gwenesis_vdp_evaluate_line(int line);")
