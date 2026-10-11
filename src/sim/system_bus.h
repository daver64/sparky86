#pragma once
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sl.h>
#include <utility>
#include <vector>
// Fixed offsets relative to the start of Shared RAM (0xA0000)
constexpr uint32_t GPU_REG_READ_PTR = 0x0000;  // Host updates this
constexpr uint32_t GPU_REG_WRITE_PTR = 0x0004; // Guest updates this
constexpr uint32_t GPU_RING_BASE = 0x0010;     // Packets start at 0xA0010
constexpr uint32_t GPU_RING_MAX_SLOTS = 256;   // Max packets in flight
constexpr uint16_t PORT_SYSTEM_STATUS = 0x40;  // Guest reads: bit 0 set = host requests quit
constexpr uint32_t SYSTEM_STATUS_QUIT = 1;

// GPU Opcode Constants
enum GpuOpcode : uint32_t
{
    GPU_CMD_CLEAR_SCREEN = 1,
    GPU_CMD_DRAW_LINE = 2,
    GPU_CMD_DRAW_RECT = 3,
    GPU_CMD_PRESENT = 4,
    GPU_CMD_DRAW_COLOUR = 5,
    GPU_CMD_TEXT_BEGIN = 6, // arg1=x, arg2=y, arg3=length
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

class SystemBus
{
  private:
    std::mutex m_bus_lock;
    std::vector<uint8_t> m_shared_ram;
    bool m_gpu_dirty = false;
    std::atomic<bool> m_quit_requested{false};

    // Thread-safe helpers to read/write 32-bit integers from raw memory vectors
    uint32_t read_ram32(uint32_t offset)
    {
        uint32_t val;
        std::memcpy(&val, &m_shared_ram[offset], 4);
        return val;
    }

    void write_ram32(uint32_t offset, uint32_t val)
    {
        std::memcpy(&m_shared_ram[offset], &val, 4);
    }

  public:
    SystemBus() : m_shared_ram(128 * 1024, 0)
    {
    }

    // Expose memory for your X86Cpu memory handlers
    uint8_t read_shared_ram(uint32_t offset)
    {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        return offset < m_shared_ram.size() ? m_shared_ram[offset] : 0;
    }

    void write_shared_ram(uint32_t offset, uint8_t val)
    {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        if (offset < m_shared_ram.size())
            m_shared_ram[offset] = val;
    }

    void write_io(uint16_t port, uint32_t value)
    {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        if (port == 0x10)
        { // Video Card Doorbell Port
            m_gpu_dirty = true;
        }
    }
    void request_quit() { m_quit_requested = true; }

    uint32_t read_io(uint16_t port)
    {
        if (port == PORT_SYSTEM_STATUS)
            return m_quit_requested ? SYSTEM_STATUS_QUIT : 0;
        std::lock_guard<std::mutex> lock(m_bus_lock);

        switch (port)
        {
        //case 0x30:                  // Player 1 Controller input port
        //    return m_p1_controller; // Make sure m_p1_controller is defined in your fields (default 0xFF)
        default:
            return 0xFFFFFFFF; // Fallback for unmapped hardware ports
        }
    }
    // Called natively by your main simlib loop thread before swapping buffers
    void process_gpu_queue(std::vector<GpuPacket> &gpu_packets)
    {
        std::lock_guard<std::mutex> lock(m_bus_lock);
        if (!m_gpu_dirty)
            return;

        uint32_t r_ptr = read_ram32(GPU_REG_READ_PTR);
        uint32_t w_ptr = read_ram32(GPU_REG_WRITE_PTR);

        // Consume exactly one complete frame (up to and including its PRESENT packet).
        // With no complete frame queued, leave gpu_packets alone so the last frame is redrawn.
        std::vector<GpuPacket> frame;
        bool complete = false;
        while (r_ptr != w_ptr)
        {
            GpuPacket pkt;
            std::memcpy(&pkt, &m_shared_ram[GPU_RING_BASE + (r_ptr * sizeof(GpuPacket))], sizeof(GpuPacket));
            r_ptr = (r_ptr + 1) % GPU_RING_MAX_SLOTS;
            if (pkt.opcode == GPU_CMD_PRESENT)
            {
                complete = true;
                break;
            }
            frame.emplace_back(pkt);
        }
        if (!complete)
            return;
        gpu_packets = std::move(frame);

        // Sync the final processed pointer state back to the Shared RAM for the x86 view
        write_ram32(GPU_REG_READ_PTR, r_ptr);
        m_gpu_dirty = (r_ptr != w_ptr);
    }
};