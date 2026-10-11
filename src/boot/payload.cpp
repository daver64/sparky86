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
        // Clear screen with a background color hex code
        clear_screen(rgba(100,100,100,255));
        // Draw a bounding frame line from (10, 20) to (300, 20)
        line(10,50,100,50);

        // Draw a square primitive block
        rect(xpos, ypos, xpos+100, ypos+100);

        present();
    }
}