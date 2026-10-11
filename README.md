# sparky86

A fantasy console built around a virtual 386-class CPU. It uses [libx86emu](https://github.com/wfeldt/libx86emu) for CPU emulation and simlib for the host window, graphics and audio.

Programs are flat binaries, so you can write them in C, assembler, or anything else that can produce one (gcc + ld, fasm, nasm, ...).

> **Status: proof of concept.** Expect missing features and breaking changes.

## Layout

| Path | Contents |
|------|----------|
| `src/boot/` | Client code that runs inside the virtual machine: real-mode bootstrap (fasm) plus a freestanding C payload, linked into `pmode_core.bin`. |
| `src/sim/`  | Host-side simulator: CPU cores, system bus, and the simlib window. |

## Machine overview (current)

- Multiple 386-class cores, each running on its own host thread with its own private RAM.
- Cores start at address `0x0000` in real mode; the bootstrap is expected to switch to 32-bit protected mode.
- 128 KB shared RAM mapped at `0xA0000`–`0xBFFFF`, synchronised via the system bus.
- I/O ports:

| Port | Dir | Description |
|------|-----|-------------|
| `0x10` | out | Video mode |
| `0x20` | out | Sound frequency (Hz) |
| `0x30` | in  | Player 1 controller (`0xFF` = idle) |

## Writing programs

Programs are loaded at `0x0000` as a flat binary. The first code in the image must be the entry point. For C, build freestanding, with no FPU/MMX/SSE:

```
gcc -m32 -march=i386 -ffreestanding -O2 -mno-80387 -mno-mmx -mno-sse -c payload.c
```

and link with a script that emits `OUTPUT_FORMAT("binary")` at origin 0 (see [src/boot/linker.ld](src/boot/linker.ld)). See [src/boot/payload.c](src/boot/payload.c) for a minimal example that plays a tone and writes to shared RAM.

## Building

Requirements: `fasm`, `gcc` (with 32-bit support), `binutils`, `g++`, `libx86emu`, `cmake`, and simlib installed under `~/opt` (override with `-DSIMLIB_PREFIX=...`) along with the SDL2, OpenGL, Vulkan, PNG, zlib and SQLite development packages.

```
cmake -S . -B build
cmake --build build                  # both
cmake --build build --target boot    # boot image only
cmake --build build --target x86cpu  # simulator only
```

Or disable a part at configure time: `-DBUILD_BOOT=OFF` / `-DBUILD_SIM=OFF`.

Outputs `pmode_core.bin` and `x86cpu` are placed in the project root.
