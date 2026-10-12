#include "sparky86.h"

static constexpr uint32_t KEY_QUEUE_SIZE = 32; // power of two

static volatile uint8_t key_state[256];
static volatile uint32_t key_queue[KEY_QUEUE_SIZE];
static volatile uint32_t queue_head; // consumer (key_pop)
static volatile uint32_t queue_tail; // producer (interrupt handler)

// IRQ vector 0x21, called from keyboard_stub in boot.asm. Drains the host's keyboard FIFO.
extern "C" void keyboard_isr()
{
    for (;;)
    {
        uint32_t event = inl(PORT_KEYBOARD_DATA);
        if (!event)
            break;
        key_state[KEY_EVENT_CODE(event)] = KEY_EVENT_PRESSED(event);
        if (KEY_EVENT_REPEAT(event))
            continue; // repeats update nothing and are not queued
        uint32_t next = (queue_tail + 1) & (KEY_QUEUE_SIZE - 1);
        if (next != queue_head) // drop the event if the app isn't keeping up
        {
            key_queue[queue_tail] = event;
            queue_tail = next;
        }
    }
}

bool key_down(uint8_t code)
{
    return key_state[code];
}

bool key_pop(uint32_t *event)
{
    if (queue_head == queue_tail)
        return false;
    *event = key_queue[queue_head];
    queue_head = (queue_head + 1) & (KEY_QUEUE_SIZE - 1);
    return true;
}
