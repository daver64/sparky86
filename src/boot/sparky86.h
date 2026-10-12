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
    GPU_CMD_PRESENT = 4,
    GPU_CMD_DRAW_COLOUR = 5,
    GPU_CMD_TEXT_BEGIN = 6, // arg1=x, arg2=y, arg3=length (max GPU_TEXT_MAX)
    GPU_CMD_TEXT_DATA = 7   // arg1..arg4 = 16 chars, little-endian packed
};

constexpr uint32_t GPU_TEXT_MAX = 128;

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

// ---- Keyboard (src/boot/input.cpp) ----
constexpr uint16_t PORT_KEYBOARD_DATA = 0x0050;

// Key codes are USB HID usage IDs
enum KeyCode : uint8_t
{
    KEY_A = 4, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_1 = 30, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_ENTER = 40, KEY_ESCAPE, KEY_BACKSPACE, KEY_TAB, KEY_SPACE,
    KEY_F1 = 58, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_RIGHT = 79, KEY_LEFT, KEY_DOWN, KEY_UP,
    KEY_LCTRL = 224, KEY_LSHIFT, KEY_LALT, KEY_RCTRL = 228, KEY_RSHIFT, KEY_RALT
};

// Event word returned by key_pop(): bits 0-7 key code, bit 8 pressed, bit 9 auto-repeat
#define KEY_EVENT_CODE(e) ((uint8_t)((e) & 0xFF))
#define KEY_EVENT_PRESSED(e) (((e) >> 8) & 1)
#define KEY_EVENT_REPEAT(e) (((e) >> 9) & 1)

bool key_down(uint8_t code);            // current state of a key, maintained by the keyboard interrupt
bool key_pop(uint32_t *event);          // next queued key event; false if none

void submit_gpu_command(uint32_t opcode, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
void clear_screen(uint32_t colour);
void draw_colour(uint32_t colour);
void line(int x1,int y1, int x2, int y2);
void rect(int x, int y, int width, int height);
void present();
int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);
void textprintf(int x, int y, const char *fmt, ...);
