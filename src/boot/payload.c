typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

static inline void outw(uint16_t port, uint16_t data) {
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

void _start() {
    outw(0x0020, 1000); 

    volatile uint32_t* shared_ram = (volatile uint32_t*)0x000A0000;
    *shared_ram = 0xABCDEF12; // Dereference pointer to write to memory

    while(1) {
        __asm__ volatile("hlt");
    }
}
