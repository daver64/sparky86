
#include "system_bus.h"
#include "x86cpu.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

struct GlobalState 
{
    // Custom destructor to safely free the core allocations later
    ~GlobalState() {
        for (auto* core : cores) {
            delete core;
        }
    }
    bool running{true};
    SystemBus system_bus;
    std::vector<X86Cpu*> cores;
    sl::Colour draw_colour;
};

GlobalState global_state;

sl::Colour decode_colour(uint32_t pixel)
{
    // Guest pixel format is 0xRRGGBBAA
    sl::Colour colour;
    colour.red = (pixel >> 24) & 0xFF;
    colour.green = (pixel >> 16) & 0xFF;
    colour.blue = (pixel >> 8) & 0xFF;
    colour.alpha = pixel & 0xFF;
    return colour;
}
int main()
{
    if (!sl::set_gfx_mode(sl::GFX_AUTODETECT_WINDOWED, 800, 600))
    {
        return -1;
    }
    sl::Event event;
    bool running = true;
    std::ifstream file("pmode_core.bin", std::ios::binary);
    if (!file)
    {
        std::cerr << "Missing binary payload: pmode_core.bin\n";
        return -1;
    }
    sl::set_window_title("Sparky86!");
    std::vector<uint8_t> payload((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    global_state.cores.emplace_back(new X86Cpu(global_state.system_bus,0));
    global_state.cores[0]->load_program(payload, 0x0000);
    global_state.cores[0]->set_up_real_mode(0x0000, 0x0000, 0x0000, 0xFFFF);
    std::thread cpu_thread(&X86Cpu::run, global_state.cores[0]);
    std::vector<GpuPacket> gpu_packets;
    while (running)
    {
        while (sl::poll_event(&event))
        {
            if (event.type() == sl::Event::Type::quit)
                running = false;
            sl::display_handle_event(event);
        }

        sl::clear_to_colour(sl::screen, sl::Colour{146, 200, 62});
        global_state.system_bus.process_gpu_queue(gpu_packets);
        for(auto gpu_packet : gpu_packets)
        {
            switch(gpu_packet.opcode)
            {
                case GPU_CMD_CLEAR_SCREEN:
                {
                    sl::Colour colour=decode_colour(gpu_packet.arg1);
                    sl::clear_to_colour(sl::screen, colour);
                } break;

                case GPU_CMD_DRAW_LINE:
                {
                    sl::line(sl::screen,
                        gpu_packet.arg1,gpu_packet.arg2,
                        gpu_packet.arg3,gpu_packet.arg4,global_state.draw_colour);
                } break;

                case GPU_CMD_DRAW_RECT:
                {
                    sl::rect(sl::screen,gpu_packet.arg1,gpu_packet.arg2,gpu_packet.arg3,
                        gpu_packet.arg4,global_state.draw_colour);
                } break;

                case GPU_CMD_DRAW_COLOUR:
                {
                    global_state.draw_colour=decode_colour(gpu_packet.arg1);
                } break;

            }
        }
        // ... draw your frame ...
        sl::show_video_bitmap();
        sl::end_frame();
        if(global_state.cores[0]->is_halted())
            break;
    }
    // Ask the guest to exit; force-stop the emulator if it doesn't comply
    global_state.system_bus.request_quit();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!global_state.cores[0]->is_halted() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!global_state.cores[0]->is_halted())
    {
        std::cerr << "Guest ignored quit request, forcing stop\n";
        global_state.cores[0]->force_stop();
    }
    cpu_thread.join();

}