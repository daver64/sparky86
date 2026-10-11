# sparky86

A fantasy console built around a virtual 386-class CPU. It uses [libx86emu](https://github.com/wfeldt/libx86emu) for CPU emulation and simlib for the host window, graphics and audio.

Programs are flat binaries, so you can write them in C, assembler, or anything else that can produce one (gcc + ld, fasm, nasm, ...).

> **Status: proof of concept.** Expect missing features and breaking changes.

## Layout

| Path | Contents |
|------|----------|
| `src/boot/` | Guest code: a real-mode bootstrap (`boot.asm`, fasm) that switches to 32-bit protected mode, plus a freestanding C++ payload (`payload.cpp`), linked into `pmode_core.bin`. |
| `src/sim/`  | Host simulator: the CPU wrapper (`x86cpu.h/.cpp`), system bus and virtual GPU (`system_bus.h`), and the simlib window (`main.cpp`). |

## Machine overview (current)

- One 386-class core running on its own host thread with 64 KB private RAM.
- `pmode_core.bin` is loaded at `0x0000` and started in real mode; the bootstrap loads a GDT, enters 32-bit protected mode and calls `_start`.
- 128 KB shared RAM mapped at `0xA0000`-`0xBFFFF`, synchronised via the system bus.
- Output goes to the simlib window; host diagnostics go to stderr.

### Virtual GPU

The guest publishes commands to a ring buffer in shared RAM and rings a doorbell; the host drains it once per frame.

| Address (shared RAM) | Meaning |
|------|---------|
| `0xA0000` | Read pointer (written by host) |
| `0xA0004` | Write pointer (written by guest) |
| `0xA0010` | Ring of 256 packets, each 5 x `uint32` (`opcode, arg1..arg4`) |

| Opcode | Command | Arguments | Status |
|--------|---------|-----------|--------|
| 1 | Clear screen | `arg1` = colour `0xRRGGBBAA` | Implemented |
| 2 | Draw line | `x1, y1, x2, y2` | Defined, not drawn yet |
| 3 | Draw rect | `x, y, w, h` | Defined, not drawn yet |

### I/O ports

| Port | Dir | Description |
|------|-----|-------------|
| `0x10` | out | GPU doorbell: tells the host new packets are queued |
| `0x40` | in  | System status: bit 0 set means the host wants the guest to quit |

Unmapped ports read as `0xFFFFFFFF`.

### Quitting

When the window is closed the host sets the quit flag on port `0x40` and waits up to 2 seconds for the guest to halt. A guest that runs forever should poll it:

```cpp
while (!(inl(0x40) & 1)) {
    // frame work
}
```

If the guest does not halt in time, the host force-stops the emulator.

## Writing programs

Programs are flat binaries loaded at `0x0000`; the first code in the image is the entry point. Anything that can produce a flat binary works. For C/C++, compile freestanding with no FPU/MMX/SSE:

```
gcc -m32 -march=i386 -ffreestanding -O2 -mno-80387 -mno-mmx -mno-sse -c payload.cpp
```

and link with a script that emits `OUTPUT_FORMAT("binary")` at origin 0 (see [src/boot/linker.ld](src/boot/linker.ld)). [src/boot/payload.cpp](src/boot/payload.cpp) is a working example that submits GPU commands in a loop until the host asks it to quit.

## Building

Requirements: `fasm`, `gcc` (with 32-bit support), `binutils`, `g++`, `libx86emu`, `cmake`, and simlib installed under `~/opt` (override with `-DSIMLIB_PREFIX=...`) along with the SDL2, OpenGL, Vulkan, PNG, zlib and SQLite development packages.

```
cmake -S . -B build
cmake --build build                  # both
cmake --build build --target boot    # boot image only
cmake --build build --target sparky86  # simulator only
```

Or disable a part at configure time: `-DBUILD_BOOT=OFF` / `-DBUILD_SIM=OFF`.

Outputs `pmode_core.bin` and `sparky86` are placed in the project root.

## Running

Run from the project root so the simulator can find `pmode_core.bin`:

```
./sparky86
```
