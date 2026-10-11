#include "system_bus.h"
#include "x86cpu.h"
#include <thread>
#include <vector>
#include <fstream>
#include <iterator>
#include <iostream>

std::vector<uint8_t> load_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Missing binary payload: " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

int run_cpu() {
    try {
        SystemBus shared_bus;
        
        // Define how many cores your fantasy console will run
        const int NUM_CORES = 4;
        
        // 1. Instantiate the CPU objects (each gets its own isolated 64KB RAM)
        std::vector<X86Cpu*> cores;
        for (int i = 0; i < NUM_CORES; ++i) {
            cores.push_back(new X86Cpu(shared_bus, i));
        }

        // 2. Load binaries and initialize execution frames
        auto payload = load_file("pmode_core.bin");
        for (int i = 0; i < NUM_CORES; ++i) {
            // You can load the same program into all cores (SMP style)
            // or different programs per core if you want asymmetric tasks (AMP style)
            cores[i]->load_program(payload, 0x0000);
            cores[i]->set_up_real_mode(0x0000, 0x0000, 0x0000, 0xFFFF);
        }

        std::cout << "[Host] Booting " << NUM_CORES << " 32-bit Protected Mode Cores...\n";

        // 3. Launch each core inside its own native OS thread
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_CORES; ++i) {
            threads.emplace_back(&X86Cpu::run, cores[i]);
        }

        // 4. Wait for all cores to hit their 'hlt' instructions and park safely
        for (int i = 0; i < NUM_CORES; ++i) {
            threads[i].join();
        }

        std::cout << "[Host] All cores safely halted.\n";
        
        // Verify the data written by the cores to the shared bus region
        uint32_t shared_word = shared_bus.read_shared_ram(0) |
                              (shared_bus.read_shared_ram(1) << 8) |
                              (shared_bus.read_shared_ram(2) << 16) |
                              (shared_bus.read_shared_ram(3) << 24);

        std::cout << "[Host] Final Shared RAM Data at 0xA0000: 0x" << std::hex << shared_word << std::dec << "\n";

        // Clean up memory allocation
        for (auto* core : cores) delete core;

    } catch (const std::exception& e) {
        std::cerr << "Runtime failure: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
