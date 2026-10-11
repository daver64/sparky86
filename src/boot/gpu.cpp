#include "sparky86.h"

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

void clear_screen(uint32_t colour)
{
    submit_gpu_command(GPU_CMD_CLEAR_SCREEN, colour, 0, 0, 0);
}

void draw_colour(uint32_t colour)
{
    submit_gpu_command(GPU_CMD_DRAW_COLOUR, colour, 0, 0, 0);
}

void line(int x1,int y1, int x2, int y2)
{
    submit_gpu_command(GPU_CMD_DRAW_LINE,x1,y1,x2,y2);
}

void rect(int x, int y, int width, int height)
{
    submit_gpu_command(GPU_CMD_DRAW_RECT,x,y,width,height);
}

// Ends the frame and blocks until the host has consumed it, locking the guest to the display rate
void present()
{
    submit_gpu_command(GPU_CMD_PRESENT, 0, 0, 0, 0);
    while (GPU_READ_PTR != GPU_WRITE_PTR && !quit_requested())
    {
        __asm__ volatile("pause");
    }
}