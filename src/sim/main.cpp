
#include "system_bus.h"
#include "x86cpu.h"

struct GlobalState 
{
    bool running{true};
    SystemBus system_bus;
    std::vector<X86Cpu*> cores;
    
};

int main()
{
    if (!sl::set_gfx_mode(sl::GFX_AUTODETECT_WINDOWED, 800, 600))
    {
        return -1;
    }
    sl::Event event;
    bool running = true;
    while (running)
    {
        while (sl::poll_event(&event))
        {
            if (event.type() == sl::Event::Type::quit)
                running = false;
            sl::display_handle_event(event);
        }

        sl::clear_to_colour(sl::screen, sl::Colour{146, 200, 62});
        // ... draw your frame ...
        sl::show_video_bitmap();
        sl::end_frame();
    }
}