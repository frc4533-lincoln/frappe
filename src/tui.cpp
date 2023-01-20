
#include "tui.hpp"



Tui::Tui(State &_state, Detector &_detector)
:   state   (_state),
    detector(_detector)
{
}

void Tui::enable()
{
    // Set up curses
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, 1);
    keypad(stdscr, 1);
}

void Tui::loop_start_ui()
{
    // Read from keyboard
    int wh, ww;
    getmaxyx(stdscr, wh, ww);
    printw("%d %d ", wh, ww);
    int c = getch();
    printw(" done");
    if (c != ERR)
    {
        switch (c)
        {
            case '1': detector.showpass = 0; break;
            case '2': detector.showpass = 1; break;
            case '3': detector.showpass = 2; break;
            case '4': detector.showpass = 3; break;
            case '5': detector.showpass = 4; break;
            case '6': detector.showpass = 5; break;
            case 'c': detector.params.corner_refine = !detector.params.corner_refine; break;
            case 'b': draw_boxes = !draw_boxes; break;
            case 's': detector.scale_mode = !detector.scale_mode; break;
            case 'f': detector.draw_fid = !detector.draw_fid; break;
            case 'a': detector.params.scale_adapt = !detector.params.scale_adapt; break;
            case 'q': detector.safe = !detector.safe; break;
            case 't': detector.tui = !detector.tui; break;
            case 'g': detector.gl = !detector.gl; break;
            case 'e': detector.nextract = !detector.nextract; break;
            case 'x': detector.grab = true; break;
            case KEY_UP: 
                if (--pidx < 0) pidx = 0;
                break;
            case KEY_DOWN:
                if (++pidx > maxpidx) pidx = maxpidx;
                break;
            case KEY_LEFT:
                if (pidx == 0) detector.params.relax_suppress -= 0.01;
                if (pidx == 1) detector.params.corner_thr -= 0.01;
                if (pidx == 2) detector.params.edge_minval -= 0.01;
                if (pidx == 3) detector.params.edge_maxval -= 0.01;
                if (pidx == 4) detector.params.xo -= 0.01;
                if (pidx == 5) detector.params.yo -= 0.01;
                if (pidx == 6) detector.params.boxidx -= 1;
                if (pidx == 7) detector.params.alpha -= 0.01;
                if (pidx == 8) detector.params.lratio -= 0.01;
                if (pidx == 9) detector.params.xw -= 1;
                if (pidx == 10) detector.params.yw -= 1;
                if (pidx == 11) {auto &sf = detector.params.scale_factor;
                    if      (sf==1.0f)      sf=0.75f;
                    else if (sf==0.75f)     sf=2.0f/3.0f;
                    else if (sf==2.0f/3.0f) sf=0.5f;
                    else if (sf==0.5f)      sf=1.0f/3.0f;
                    else if (sf==1.0f/3.0f) sf=0.25f;
                }
                break;
            case KEY_RIGHT:
                if (pidx == 0) detector.params.relax_suppress += 0.01;
                if (pidx == 1) detector.params.corner_thr += 0.01;
                if (pidx == 2) detector.params.edge_minval += 0.01;
                if (pidx == 3) detector.params.edge_maxval += 0.01;
                if (pidx == 4) detector.params.xo += 0.001;
                if (pidx == 5) detector.params.yo += 0.001;
                if (pidx == 6) detector.params.boxidx += 1;
                if (pidx == 7) detector.params.alpha += 0.01;
                if (pidx == 8) detector.params.lratio += 0.01;
                if (pidx == 9) detector.params.xw += 1;
                if (pidx == 10) detector.params.yw += 1;
                if (pidx == 11) {auto &sf = detector.params.scale_factor;
                    if      (sf==0.75f)     sf=1.0f;
                    else if (sf==2.0f/3.0f) sf=0.75f;
                    else if (sf==0.5f)      sf=2.0f/3.0f;
                    else if (sf==1.0f/3.0f) sf=0.5f;
                    else if (sf==0.25f)     sf=1.0f/3.0f;
                }
                break;
        }
        //if (stages > plist.size()) stages = plist.size();
    }
    for(int i = 0; i <= maxpidx; i++)
    {
        mvprintw(wh - 27 + i, 0, "%c", i == pidx ? '>' : ' ');
        mvprintw(wh - 27 + i, 1, "%s %f", pnames[i].c_str(), 
            i == 0 ? detector.params.relax_suppress :
            i == 1 ? detector.params.corner_thr : 
            i == 2 ? detector.params.edge_minval : 
            i == 3 ? detector.params.edge_maxval : 
            i == 4 ? detector.params.xo : 
            i == 5 ? detector.params.yo : 
            i == 6 ? (float)detector.params.boxidx : 
            i == 7 ? detector.params.alpha : 
            i == 8 ? detector.params.lratio : 
            i == 9 ? detector.params.xw : 
            i == 10 ? detector.params.yw : 
            i == 11 ? detector.params.scale_factor : 
            0.0 );
    }
    mvprintw(wh - 15, 0, "Pass:         (c) %d", detector.showpass);
    mvprintw(wh - 14, 0, "Cnr refine:   (c) %d", detector.params.corner_refine);
    mvprintw(wh - 13, 0, "Boxes:        (b) %d", draw_boxes);
    mvprintw(wh - 12, 0, "Scale mode:   (s) %d", detector.scale_mode);
    mvprintw(wh - 11, 0, "Fid:          (f) %d", detector.draw_fid);
    mvprintw(wh - 10, 0, "Tui:          (f) %d", detector.tui);
    mvprintw(wh - 9, 0,  "Safe:         (q) %d", detector.safe);
    mvprintw(wh - 8, 0,  "GL:           (g) %d", detector.gl);
    mvprintw(wh - 7, 0,  "Adapt:        (a) %d", detector.params.scale_adapt);
    mvprintw(wh - 6, 0,  "New extract:  (e) %d", detector.nextract);
    move(wh - 10, 0);

}

void Tui::loop_end_ui()
{
}