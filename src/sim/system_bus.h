#pragma once
#include <sl.h>
#include <mutex>
#include <vector>
#include <cstdint>
#include <iostream>

class SystemBus {
private:
    std::mutex m_bus_lock;

    // Shared Peripheral Registers
    uint16_t m_video_mode = 0;
    uint16_t m_sound_frequency = 0;
    uint8_t  m_p1_controller = 0xFF;

    // 128KB Global Shared Memory Space (Mapped to 0xA0000 - 0xBFFFF)
    std::vector<uint8_t> m_shared_ram;

public:
    SystemBus() : m_shared_ram(128 * 1024, 0) {}

    // Synchronized Shared RAM Read
    uint8_t read_shared_ram(uint32_t offset) {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        if (offset >= m_shared_ram.size()) return 0;
        return m_shared_ram[offset];
    }

    // Synchronized Shared RAM Write
    void write_shared_ram(uint32_t offset, uint8_t val) {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        if (offset < m_shared_ram.size()) {
            m_shared_ram[offset] = val;
        }
    }

    void write_io(uint16_t port, uint32_t value) {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        switch (port) {
            case 0x10: m_video_mode = value & 0xFFFF; break;
            case 0x20:
                m_sound_frequency = value & 0xFFFF;
                std::cout << "[Sound] Playing frequency: " << m_sound_frequency << "Hz\n";
                break;
        }
    }

    uint32_t read_io(uint16_t port) {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        return (port == 0x30) ? m_p1_controller : 0xFFFFFFFF;
    }
};
