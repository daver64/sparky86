#pragma once
#include <x86emu.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include "system_bus.h"

class X86Cpu {
public:
    X86Cpu(SystemBus& bus, int core_id) 
        : m_bus(bus), m_core_id(core_id), m_ram(64 * 1024, 0), m_halted(false) 
    {
        m_emu = x86emu_new(X86EMU_PERM_RWX, 0);
        if (!m_emu) throw std::runtime_error("Failed to initialize libx86emu context");
        m_emu->_private = this;
        x86emu_set_memio_handler(m_emu, &X86Cpu::memio_handler);
        x86emu_set_code_handler(m_emu, &X86Cpu::code_handler);
    }

    ~X86Cpu() { if (m_emu) x86emu_done(m_emu); }

    X86Cpu(const X86Cpu&) = delete;
    X86Cpu& operator=(const X86Cpu&) = delete;

    void load_program(const std::vector<uint8_t>& code, uint32_t offset = 0) {
        if (offset + code.size() > m_ram.size()) throw std::out_of_range("Program exceeds 64KB");
        std::copy(code.begin(), code.end(), m_ram.begin() + offset);
    }

    void set_up_real_mode(uint16_t cs_val, uint16_t ip_val, uint16_t ss_val, uint16_t sp_val) {
        m_emu->x86.R_EIP = ip_val;
        x86emu_set_seg_register(m_emu, m_emu->x86.R_CS_SEL, cs_val);
        m_emu->x86.R_ESP = sp_val;
        x86emu_set_seg_register(m_emu, m_emu->x86.R_SS_SEL, ss_val);
    }

    void run() {
        x86emu_run(m_emu, X86EMU_RUN_LOOP);
        m_halted = true;
    }

    uint32_t get_eax() const { return m_emu->x86.R_EAX; }
    uint32_t get_eip() const { return m_emu->x86.R_EIP; }
    bool is_halted() const   { return m_halted; }
    void force_stop()        { m_stop_requested = true; x86emu_stop(m_emu); }

private:
    SystemBus& m_bus;
    int m_core_id;
    std::vector<uint8_t> m_ram;
    std::atomic<bool> m_halted;
    std::atomic<bool> m_stop_requested{false};
    x86emu_t* m_emu = nullptr;

    // Runs on the CPU thread before every instruction, so raising interrupts here is race free.
    // libx86emu ignores IF, so it is honoured here; only one interrupt can be pending at a time.
    static int code_handler(x86emu_t* emu) {
        return static_cast<X86Cpu*>(emu->_private)->before_instruction();
    }

    // Returns non-zero to stop the emulator
    int before_instruction() {
        if (m_stop_requested) return 1;
        deliver_interrupts();
        // A guest "hlt" with interrupts enabled idles until an interrupt or quit request, instead of
        // stopping the emulator. With IF clear (e.g. in the fault handler) it still stops, as the CPU
        // could never wake up.
        uint32_t opcode_addr = m_emu->x86.R_CS_BASE + m_emu->x86.R_EIP;
        if ((m_emu->x86.R_EFLG & F_IF) && read_byte(opcode_addr) == 0xF4) {
            while (m_bus.pending_irq() < 0 && !m_bus.quit_requested() && !m_stop_requested)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (m_stop_requested) return 1;
            m_emu->x86.R_EIP++; // resume after the hlt
            deliver_interrupts();
        }
        return 0;
    }

    void deliver_interrupts() {
        if (!(m_emu->x86.R_EFLG & F_IF) || m_emu->x86.intr_type) return;
        int vector = m_bus.pending_irq();
        if (vector >= 0) x86emu_intr_raise(m_emu, static_cast<u8>(vector), INTR_TYPE_SOFT, 0);
    }

    static unsigned memio_handler(x86emu_t* emu, u32 addr, u32* val, unsigned type) {
        return static_cast<X86Cpu*>(emu->_private)->handle_memio(addr, val, type);
    }

    // Helper functions to safely route read/write requests depending on address map
    uint8_t read_byte(uint32_t addr) {
        if (addr >= 0xA0000 && addr <= 0xBFFFF) {
            return m_bus.read_shared_ram(addr - 0xA0000);
        }
        return m_ram[addr & 0xFFFF]; // Default local wrap
    }

    void write_byte(uint32_t addr, uint8_t val) {
        if (addr >= 0xA0000 && addr <= 0xBFFFF) {
            m_bus.write_shared_ram(addr - 0xA0000, val);
        } else {
            m_ram[addr & 0xFFFF] = val;
        }
    }

    unsigned handle_memio(u32 addr, u32* val, unsigned type) {
        unsigned access_type = type & 0xFF00; 
        unsigned data_size   = type & 0x00FF; 

        if (access_type == X86EMU_MEMIO_I)  { *val = m_bus.read_io(addr & 0xFFFF); return 0; }
        if (access_type == X86EMU_MEMIO_O) { m_bus.write_io(addr & 0xFFFF, *val); return 0; }

        if (access_type == X86EMU_MEMIO_R || access_type == X86EMU_MEMIO_X || access_type == X86EMU_MEMIO_I) {
            if (data_size == X86EMU_MEMIO_8)  *val = read_byte(addr);
            if (data_size == X86EMU_MEMIO_16) *val = read_byte(addr) | (read_byte(addr + 1) << 8);
            if (data_size == X86EMU_MEMIO_32) {
                *val = read_byte(addr) | (read_byte(addr + 1) << 8) | 
                       (read_byte(addr + 2) << 16) | (read_byte(addr + 3) << 24);
            }
            return 0;
        } 
        if (access_type == X86EMU_MEMIO_W) {
            if (data_size == X86EMU_MEMIO_8)  write_byte(addr, *val & 0xFF);
            if (data_size == X86EMU_MEMIO_16) {
                write_byte(addr, *val & 0xFF); write_byte(addr + 1, (*val >> 8) & 0xFF);
            }
            if (data_size == X86EMU_MEMIO_32) {
                write_byte(addr, *val & 0xFF);         write_byte(addr + 1, (*val >> 8) & 0xFF);
                write_byte(addr + 2, (*val >> 16) & 0xFF); write_byte(addr + 3, (*val >> 24) & 0xFF);
            }
            return 0;
        }
        return 1; 
    }
};


int run_cpu();