
#include "TextEditor.h"
#include "keymap.h"
#include "system_bus.h"
#include "x86cpu.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

// gui interface, will go in simgui.h eventually...
void update_editor(X86Cpu &cpu);
extern TextEditor editor;
bool open_file(const std::string &name);
void simgui_shutdown();
bool restart_guest(bool paused);

struct GlobalState
{
    // Custom destructor to safely free the core allocations later
    ~GlobalState()
    {
        for (auto *core : cores)
        {
            delete core;
        }
    }
    bool running{true};
    SystemBus system_bus;
    std::vector<X86Cpu *> cores;
    sl::Colour draw_colour;
    // Text being reassembled from TEXT_BEGIN / TEXT_DATA packets
    int text_x{0};
    int text_y{0};
    uint32_t text_len{0};
    std::string text;
    std::thread cpu_thread;
    std::vector<GpuPacket> gpu_packets;
};

GlobalState global_state;

// Replaces the running guest with a fresh one loaded from the rebuilt pmode_core.bin.
// Must be called from the UI thread, which is the only user of the core pointer.
bool restart_guest(bool paused)
{
    std::ifstream file(std::string(SPARKY_SOURCE_DIR) + "/pmode_core.bin", std::ios::binary);
    if (!file)
        return false;
    std::vector<uint8_t> payload((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // The old guest is discarded, so there is no point waiting for it to quit politely
    global_state.cores[0]->force_stop();
    global_state.cpu_thread.join();
    delete global_state.cores[0];

    global_state.system_bus.reset();
    global_state.gpu_packets.clear();
    global_state.text_len = 0;
    global_state.text.clear();
    global_state.draw_colour = sl::Colour{};

    X86Cpu *cpu = new X86Cpu(global_state.system_bus, 0);
    cpu->load_program(payload, 0x0000);
    cpu->set_up_real_mode(0x0000, 0x0000, 0x0000, 0xFFFF);
    cpu->set_paused(paused);
    global_state.cores[0] = cpu;
    global_state.cpu_thread = std::thread(&X86Cpu::run, cpu);
    return true;
}

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
// Draws the debugger panel; only called while the CPU is parked so register reads are safe
static void draw_debug_panel(X86Cpu &cpu)
{
    X86Cpu::Registers r = cpu.registers();
    sl::rectfill(sl::screen, 0, 548, 800, 600, sl::Colour{0, 0, 0, 200});
    const sl::Colour white{255, 255, 255};
    sl::gprintf(8, 550, white, "PAUSED  instr %llu   F5 run  F10 step  F11 step 100",
                (unsigned long long)cpu.instruction_count());
    sl::gprintf(8, 562, white, "EAX %08x EBX %08x ECX %08x EDX %08x", r.eax, r.ebx, r.ecx, r.edx);
    sl::gprintf(8, 574, white, "ESI %08x EDI %08x EBP %08x ESP %08x", r.esi, r.edi, r.ebp, r.esp);
    sl::gprintf(8, 586, white, "EIP %08x EFL %08x  code %02x %02x %02x %02x %02x %02x", r.eip, r.eflags, r.code[0],
                r.code[1], r.code[2], r.code[3], r.code[4], r.code[5]);
}

int main(int argc, char **argv)
{
    bool start_paused = false;
    std::string program_path = "pmode_core.bin";
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--step" || arg == "-s")
            start_paused = true;
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: sparky86 [--step] [program.bin]\n"
                         "  --step   load the program paused, ready to single-step\n"
                         "Keys: F5 run/pause, F10 step one instruction, F11 step 100\n";
            return 0;
        }
        else
            program_path = arg;
    }

    if (!sl::set_gfx_mode(sl::GFX_AUTODETECT_WINDOWED, 800, 600))
    {
        return -1;
    }
    sl::Event event;
    bool running = true;
    std::ifstream file(program_path, std::ios::binary);
    if (!file)
    {
        std::cerr << "Missing binary payload: " << program_path << "\n";
        return -1;
    }

    sl::gui_init();

    auto lang = TextEditor::LanguageDefinition::CPlusPlus();

    editor.SetLanguageDefinition(lang);
    // editor.SetPalette(TextEditor::GetLightPalette());

    open_file("boot.asm");
    sl::set_vsync(false);
    sl::set_fps(60);

    sl::set_window_title("Sparky86!");
    std::vector<uint8_t> payload((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    global_state.cores.emplace_back(new X86Cpu(global_state.system_bus, 0));
    global_state.cores[0]->load_program(payload, 0x0000);
    global_state.cores[0]->set_up_real_mode(0x0000, 0x0000, 0x0000, 0xFFFF);
    global_state.cores[0]->set_paused(start_paused);

    global_state.cpu_thread = std::thread(&X86Cpu::run, global_state.cores[0]);

    while (running)
    {
        while (sl::poll_event(&event))
        {
            if (event.type() == sl::Event::Type::quit)
                running = false;
            // Debugger hotkeys are handled by the host and not sent to the guest
            if (event.type() == sl::Event::Type::key_down)
            {
                X86Cpu &cpu = *global_state.cores[0];
                switch (event.key())
                {
                case sl::Event::Key::f5:
                    if (!event.key_repeat())
                        cpu.set_paused(!cpu.is_paused());
                    break;
                case sl::Event::Key::f10:
                    cpu.set_paused(true);
                    cpu.step(1);
                    break;
                case sl::Event::Key::f11:
                    cpu.set_paused(true);
                    cpu.step(100);
                    break;
                default:
                    break;
                }
            }
            const auto hotkey = event.key();
            const bool is_hotkey =
                hotkey == sl::Event::Key::f5 || hotkey == sl::Event::Key::f10 || hotkey == sl::Event::Key::f11;
            if (!is_hotkey && (event.type() == sl::Event::Type::key_down || event.type() == sl::Event::Type::key_up))
            {
                uint8_t code = map_key_to_guest(event.key());
                if (code)
                    global_state.system_bus.push_key_event(code, event.type() == sl::Event::Type::key_down,
                                                           event.key_repeat());
            }
            sl::display_handle_event(event);
            sl::gui_handle_event(event);
        }

        sl::clear_to_colour(sl::screen, sl::Colour{146, 200, 62});
        global_state.system_bus.process_gpu_queue(global_state.gpu_packets);
        for (auto gpu_packet : global_state.gpu_packets)
        {
            switch (gpu_packet.opcode)
            {
            case GPU_CMD_CLEAR_SCREEN: {
                sl::Colour colour = decode_colour(gpu_packet.arg1);
                sl::clear_to_colour(sl::screen, colour);
            }
            break;

            case GPU_CMD_DRAW_LINE: {
                sl::line(sl::screen, gpu_packet.arg1, gpu_packet.arg2, gpu_packet.arg3, gpu_packet.arg4,
                         global_state.draw_colour);
            }
            break;

            case GPU_CMD_DRAW_RECT: {
                sl::rect(sl::screen, gpu_packet.arg1, gpu_packet.arg2, gpu_packet.arg3, gpu_packet.arg4,
                         global_state.draw_colour);
            }
            break;

            case GPU_CMD_TEXT_BEGIN: {
                global_state.text_x = gpu_packet.arg1;
                global_state.text_y = gpu_packet.arg2;
                global_state.text_len = std::min(gpu_packet.arg3, GPU_TEXT_MAX);
                global_state.text.clear();
            }
            break;

            case GPU_CMD_TEXT_DATA: {
                const uint32_t words[4] = {gpu_packet.arg1, gpu_packet.arg2, gpu_packet.arg3, gpu_packet.arg4};
                for (int i = 0; i < 16 && global_state.text.size() < global_state.text_len; ++i)
                    global_state.text.push_back(static_cast<char>((words[i / 4] >> ((i % 4) * 8)) & 0xFF));
                if (global_state.text.size() == global_state.text_len && global_state.text_len > 0)
                {
                    // "%s" so guest text is never treated as a format string
                    sl::gprintf(global_state.text_x, global_state.text_y, global_state.draw_colour, "%s",
                                global_state.text.c_str());
                    global_state.text_len = 0;
                    global_state.text.clear();
                }
            }
            break;

            case GPU_CMD_DRAW_COLOUR: {
                global_state.draw_colour = decode_colour(gpu_packet.arg1);
            }
            break;
            }
        }
        if (global_state.cores[0]->is_paused() && global_state.cores[0]->is_parked())
            draw_debug_panel(*global_state.cores[0]);

        update_editor(*global_state.cores[0]);

        sl::show_video_bitmap();
        sl::end_frame();
        global_state.system_bus.signal_vblank();
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
    global_state.cpu_thread.join();

    simgui_shutdown();
    sl::gui_shutdown();
    sl::wait_for_graphics();
    sl::shutdown();
}