#pragma once

// Explicit primitive bindings mapping directly to 32-bit x86 targets
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

// Standard helper type for sizes matching standard compiler outputs
typedef __SIZE_TYPE__ size_t;

// Fixed offsets relative to the start of Shared RAM (0xA0000)
constexpr uint32_t GPU_REG_READ_PTR = 0x0000;  // Host updates this
constexpr uint32_t GPU_REG_WRITE_PTR = 0x0004; // Guest updates this
constexpr uint32_t GPU_RING_BASE = 0x0010;     // Packets start at 0xA0010
constexpr uint32_t GPU_RING_MAX_SLOTS = 256;   // Max packets in flight

// GPU Opcode Constants
enum GpuOpcode : uint32_t
{
    GPU_CMD_CLEAR_SCREEN = 1,
    GPU_CMD_DRAW_LINE = 2,
    GPU_CMD_DRAW_RECT = 3,
    GPU_CMD_PRESENT = 4
};

// Layout must match 32-bit alignment for both Host and Guest
struct GpuPacket
{
    uint32_t opcode;
    uint32_t arg1; // Color / X1
    uint32_t arg2; // Y1
    uint32_t arg3; // X2 / Width
    uint32_t arg4; // Y2 / Height
};

// Packs a colour as 0xRRGGBBAA, the format the host's GPU expects
#define rgba(r, g, b, a) \
    ((((uint32_t)(r) & 0xFF) << 24) | (((uint32_t)(g) & 0xFF) << 16) | (((uint32_t)(b) & 0xFF) << 8) | ((uint32_t)(a) & 0xFF))

// Extract a single channel from a 0xRRGGBBAA colour
#define getr(c) ((uint32_t)(((uint32_t)(c) >> 24) & 0xFF))
#define getg(c) ((uint32_t)(((uint32_t)(c) >> 16) & 0xFF))
#define getb(c) ((uint32_t)(((uint32_t)(c) >> 8) & 0xFF))
#define geta(c) ((uint32_t)((uint32_t)(c) & 0xFF))

#define SHARED_RAM_BASE 0x000A0000

// Helper references pointing to the control structure registers mapped in 32-bit pmode
#define GPU_READ_PTR (*(volatile uint32_t *)(SHARED_RAM_BASE + GPU_REG_READ_PTR))
#define GPU_WRITE_PTR (*(volatile uint32_t *)(SHARED_RAM_BASE + GPU_REG_WRITE_PTR))
#define GPU_RING_SLOTS ((volatile GpuPacket *)(SHARED_RAM_BASE + GPU_RING_BASE))

static inline void outw(uint16_t port, uint16_t data)
{
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t data;
    __asm__ volatile("inl %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

// Host sets bit 0 of the status port when the app should exit
static inline bool quit_requested()
{
    return inl(0x0040) & 1;
}