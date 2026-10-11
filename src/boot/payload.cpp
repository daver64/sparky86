
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
    GPU_CMD_DRAW_RECT = 3
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

// Thread-safe-ready Guest Command Publisher
void submit_gpu_command(uint32_t opcode, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t current_w = GPU_WRITE_PTR;
    uint32_t next_w = (current_w + 1) % GPU_RING_MAX_SLOTS;

    // Spinlock/Wait if the ring buffer is completely full (write caught up to read)
    while (next_w == GPU_READ_PTR)
    {
        if (quit_requested())
            return;
        __asm__ volatile("pause"); // Avoid wasting CPU pipeline energy while waiting
    }

    // Populate the packet values at our current write pointer slot
    GPU_RING_SLOTS[current_w].opcode = opcode;
    GPU_RING_SLOTS[current_w].arg1 = a1;
    GPU_RING_SLOTS[current_w].arg2 = a2;
    GPU_RING_SLOTS[current_w].arg3 = a3;
    GPU_RING_SLOTS[current_w].arg4 = a4;

    // Advance the write pointer to expose it to the host compiler
    GPU_WRITE_PTR = next_w;

    // Ring the doorbell register to alert the host to process the queue
    outw(0x0010, 1);
}

extern "C" void _start()
{

    while(!quit_requested())
    {
        // Clear screen with a background color hex code
        submit_gpu_command(GPU_CMD_CLEAR_SCREEN, 0xFF0000FF, 0, 0, 0);

        // Draw a bounding frame line from (10, 20) to (300, 20)
        submit_gpu_command(GPU_CMD_DRAW_LINE, 10, 20, 300, 20);

        // Draw a square primitive block
        submit_gpu_command(GPU_CMD_DRAW_RECT, 50, 50, 100, 100);
    }
    while (1)
    {
        __asm__ volatile("hlt");
    }
}