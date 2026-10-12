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
| 2 | Draw line | `x1, y1, x2, y2` | Implemented |
| 3 | Draw rect | `x, y, w, h` | Implemented |
| 4 | Present | none | Ends a frame; the host shows one frame per present |
| 5 | Set draw colour | `arg1` = colour `0xRRGGBBAA` | Implemented |
| 6 | Text begin | `x, y, length` (max 128) | Implemented |
| 7 | Text data | 16 chars packed little-endian across `arg1..arg4` | Implemented |

### Text

Guest code calls `textprintf(x, y, fmt, ...)` (see `src/boot/gpu.cpp`). The guest formats the string itself with a built-in `vsnprintf` supporting `%d %i %u %x %X %c %s %%` with width, `0` padding and `-` left-align, truncated to 128 characters. It is sent as one text-begin packet followed by text-data packets, and the host draws it with the default simlib monospace font in the current draw colour.

### I/O ports

| Port | Dir | Description |
|------|-----|-------------|
| `0x10` | out | GPU doorbell: tells the host new packets are queued |
| `0x40` | in  | System status: bit 0 set means the host wants the guest to quit |
| `0x50` | in  | Keyboard: each read pops one event; `0` means the queue is empty |

Unmapped ports read as `0xFFFFFFFF`.

### Interrupts and keyboard

The guest runs with interrupts enabled. `boot.asm` builds an IDT covering vectors `0x00`-`0x21`; unhandled vectors and CPU faults land in a handler that executes `hlt`, which stops the guest. Keyboard input is delivered on vector `0x21`:

1. The host maps each key press/release to a key code and queues an event (up to 64; extras are dropped).
2. While the queue is non-empty and the guest has `IF` set, the CPU raises vector `0x21` between instructions. It is level triggered: it keeps firing until the handler has drained port `0x50`.
3. The handler (`keyboard_isr` in `src/boot/input.cpp`) reads port `0x50` until it returns `0`, updates a key-state table and pushes new press/release events onto a 32-entry queue.

Event word: bits 0-7 key code (USB HID usage IDs, the `KEY_*` constants in `sparky86.h`), bit 8 pressed, bit 9 auto-repeat. Auto-repeat events update the state table but are not queued. Guest API:

```cpp
bool key_down(uint8_t code);     // is the key currently held?
bool key_pop(uint32_t *event);   // next press/release event, false if none
```

### Halting

`hlt` with interrupts enabled idles the guest until an interrupt arrives (or the host requests quit), so a guest can sleep between frames. `hlt` with interrupts disabled stops the emulator, because nothing could ever wake it. That is what happens when `_start` returns, and in the default handler for CPU faults and unhandled vectors.

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

## Running and debugging

Run from the project root; the program defaults to `pmode_core.bin`:

```
./sparky86 [--step] [program.bin]
```

`--step` loads the program paused before its first instruction. Any flat binary can be given as `program.bin`.

| Key | Action |
|-----|--------|
| `F5` | Toggle run / pause |
| `F10` | Pause (if running) and execute one instruction; hold to repeat |
| `F11` | Pause (if running) and execute 100 instructions |

These keys are handled by the host and are not sent to the guest. While paused, an overlay at the bottom of the window shows the instruction count, the general registers, `EIP`, `EFLAGS` and the raw bytes at `EIP`. The display keeps showing the last completed frame, and key presses are still queued for the guest and delivered as you step. Quitting works while paused.

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
