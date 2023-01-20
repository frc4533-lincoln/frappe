
#ifndef TUI_HPP
#define TUI_HPP

#include "glhelpers.hpp"
#include "detector.hpp"

class Tui
{
public:
    Tui(State &_state, Detector &_detector);

    void loop_start_ui();
    void loop_end_ui();
    void enable();

    State       &state;
    Detector    &detector;

    int connect = 1;
    int draw_boxes = 1;
    int showcorners = 1;
    int draw_fid = 0;
    int pidx = 0;
    int maxpidx = 11;
    std::vector<std::string> pnames = {
        "relax_suppress",
        "corner_thresh ",
        "edge_minval   ",
        "edge_maxval   ",
        "xo            ",
        "yo            ",
        "boxidx        ",
        "alpha         ",
        "lratio        ",
        "xwin          ",
        "ywin          ",
        "scale_factor  ",
        };

};



#endif