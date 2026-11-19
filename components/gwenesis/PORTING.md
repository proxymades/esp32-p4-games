# Gwenesis P4 port

Upstream files are pinned and hash-checked by upstream.json. Their original
copyright notices and licences are retained. The firmware uses the existing
M68000 and Z80 interpreters; the alternate gen68k adapter is not compiled.

prepare_core.py generates a build-local copy with these fixes:

- FM and PSG sample clocks advance relative to their previous clock, retaining
  fractional phase at frame boundaries. Both synths execute on the emulation task.
- Audio buffers have an explicit 2048-sample limit and report overflow.
- Frames without a free SPI buffer evaluate sprite-table limits/masking/overflow
  but skip plane and sprite pixel writes. The original sprite traversal is
  reused with pixel drawing disabled. CPU, VDP timing and audio still run every
  frame. Native full/one-in-four rendering is compared over 7200 game frames,
  including the entire resampled PCM stream.
- Unused esp_attr includes are removed for native host validation.
- The PSG intermediate arithmetic has the same 32-bit width on macOS and ESP32.
- Longword RAM reads/writes wrap at 64 KiB; ROM longword reads use word loads.

The application chooses PAL/NTSC from the console region register, independently
of the VDP 224/240-line setting. RGB conversion is performed per scanline while
that line's palette is valid. Video buffer ownership belongs to the platform.
The active RGB image is overwritten per scanline; only the outside borders are
cleared. Large internal allocations precede smaller ones. The emulation stack
is 16 KiB; its measured high-water mark is reported by the device.

No save-state UI or filesystem callbacks are implemented.
