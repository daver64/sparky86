#include "sparky86.h"

extern "C" void _start()
{
    int xpos=100;
    int ypos=100;
    int dx=1;
    int dy=1;
    int max_x=700;
    int max_y=500;
    while(!quit_requested())
    {
        xpos+=dx;
        ypos+=dy;
        if(xpos<1 || xpos>max_x)
        {
            dx=-dx;
        }
        if(ypos<1 || ypos>max_y)
        {
            dy=-dy;
        }
        clear_screen(rgba(100,100,100,255));
        draw_colour(rgba(0,255,0,255));
        line(10,50,100,50);
        rect(xpos, ypos, xpos+100, ypos+100);
        textprintf(10, 10, "pos %d,%d  [%04x] %s", xpos, ypos, xpos, "ok");
        present();
    }
}