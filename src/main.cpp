//
//  main.cpp
//  gles2play
//
//  Created by Simon Jones on 19/03/2022.
//

#include <stdio.h>



#define CAMERA

#define TUI


#define GL_SILENCE_DEPRECATION
#include "glhelpers.hpp"
#include "detector.hpp"
#include "tui.hpp"



#include "qpu_program.h"
#include "qpu_info.h"

const char *vs_camera = R"glsl(
#version 100
precision mediump float;

attribute vec4 v_pos;
attribute vec2 v_tex;
uniform float hpix;
uniform float vpix;
varying highp vec2 tex_coord;
varying highp vec2 tc0, tc1, tc2, tc3, tc5, tc6, tc7, tc8;
void main()
{
    gl_Position = v_pos;
    tex_coord = v_tex;
    tc0 = vec2(v_tex.x - hpix, v_tex.y + vpix);
    tc1 = vec2(v_tex.x       , v_tex.y + vpix);
    tc2 = vec2(v_tex.x + hpix, v_tex.y + vpix);
    tc3 = vec2(v_tex.x - hpix, v_tex.y );
    tc5 = vec2(v_tex.x + hpix, v_tex.y );
    tc6 = vec2(v_tex.x - hpix, v_tex.y - vpix);
    tc7 = vec2(v_tex.x       , v_tex.y - vpix);
    tc8 = vec2(v_tex.x + hpix, v_tex.y - vpix);
}
)glsl";

const char *fs_camera= R"glsl(
#version 100
#extension GL_OES_EGL_image_external : require
precision mediump float;
precision mediump sampler2D;
varying vec2 tex_coord;
uniform samplerExternalOES tex;
void main()
{
    gl_FragColor = texture2D(tex, tex_coord);
}
)glsl";

const char *vs_tex2 = R"glsl(
#version 100
precision mediump float;

attribute vec4 v_pos;
attribute vec2 v_tex;
varying highp vec2 tex_coord;
void main()
{
    gl_Position = v_pos;
    tex_coord = v_tex;
}
)glsl";


//#include "aruco_nano.h"



int main(int argc, char **argv)
{


    bool entui = false;
    if (argc == 2 && !strcmp(argv[1], "-t"))
    {
        entui = true;
    }

    const int width = 640;
    const int height = 480;

    // Set up GL
    State state("Hello world", 0, 0, 1024, 512);
    printf("State\n");

    // Set up detector with basic camera parameters
    Detector detector(state, width, height, 1.0);
    detector.camera.camera_matrix = (cv::Mat1f(3, 3) <<  400,    0,      320,
                                                         0,      400,    240, 
                                                         0,      0,      1);
    detector.camera.dist_coeffs = (cv::Mat1f(1, 5) << 0, 0, 0, 0, 0);

    printf("Detector\n");


#ifdef TUI
    // Set up UI, mostly text
    Tui tui(state, detector);
    if (entui)
        tui.enable();
#endif

#ifndef STEST
    Texture t_image(state, 640, 480, true);
    //t_image.load_texture_data("../map4x4_single_640x480.png");
    //t_image.load_texture_data("../map4x4_640x480.png");
    //t_image.load_texture_data("../aruco_mip_36h12_00000a.jpg");
    t_image.load_texture_data("../../fiducial_test/fiducial_test_data_home/out0028.jpg");
    //t_image.load_texture_data("../../fiducial_test/fiducial_test_data_ada/ada_01156.jpg");
    //t_image.load_texture_data("../../fiducial_test/fiducial_test_data_micro/cam0_000100.jpg");
    //t_image.load_texture_data("../../fiducial_test/fiducial_test_data_small/cam3_001250.jpg");
#else
    Texture t_image(state, width, height, true);
    t_image.load_texture_data("../angles_faint_640x480.png");
#endif


#ifdef CAMERA
    Cparams cparams = {
        .width      = 640,
        .height     = 480,
        .fps        = 30,
        .format     = CAMGL_Y,
    };
    Texture t_camera(state, cparams);
    Program p_camera(vs_camera, fs_camera, "camera");
#endif



    
    Mesh quad({"v_pos", "v_tex"}, {3, 2},
    {
        -1, -1,  0, 0.0, 1.0,
        -1,  0.875,  0, 0.0, 0.0,
         0.25, -1,  0, 1.0, 1.0,
         0.25,  0.875,  0, 1.0, 0.0,
    });
    Mesh quad_yflip({"v_pos", "v_tex"}, {3, 2},
    {
        -1, -1,  0, 0.0, 0.0,
        -1,  0.875,  0, 0.0, 1.0,
         0.25, -1,  0, 1.0, 0.0,
         0.25,  0.875,  0, 1.0, 1.0,
    });


    int rolling[64] = {0};
    int ridx = 0;


    // QPUprogram p_qpublit(state, "qpu_debug_tiled.bin", tile_rows, tile_cols);

    // Initial pass to warm caches
    MarkerVec mv;
    mv = detector.detect(t_image);
    while (!state.window_should_close())
    {
#ifdef TUI
        if (entui)
            tui.loop_start_ui();
#endif


        // If we are using the camera, copy the frame into the input buffer
#ifdef CAMERA
        t_camera.get_cam_frame();
        t_camera.dump_file("t1.bin");
        exit(1);
        // render_pass(state.fbo, quad_yflip, p_camera, t_camera, t_image);
        // glFinish();
#endif

        //----------------------------
        //set_time();


        // Run the detector
#ifdef CAMERA
        mv = detector.detect(t_camera);
        // mv = detector.detect(t_image);
#else
        mv = detector.detect(t_image);
#endif

        // Calculate the extrinsics with marker size of 40mm
        detector.calculate_extrinsics(40.0);

        // Render to screen
        detector.render(t_image, true);


        int total = 0;
        for(int i = 0; i < 16; i++)
        {
            if(entui)mvprintw(67 + i, 60, "%6d %s", detector.times[i], detector.stages[i].c_str());
            if (i < 10)
                total += detector.times[i];
            if(!entui)
                printf("%6d ", detector.times[i]);
        }
        //if (!entui)printf("\n");

        rolling[ridx] = total;
        ridx = (ridx + 1) % 64;
        float avg = 0; 
        for (int i = 0; i < 64; avg += rolling[i++]);
        avg = avg / 64.0 * 1e-6;
        float fps = 1.0 / avg;
        float temp = 0;//float)getTemperature(state.mb) / 1000.0;
        if(entui)
            mvprintw(67 + 17, 60, "%6d %8.3f %8.3f    ", total, avg * 1000.0, fps);
        else
            printf("%6d %8.3f %8.3f %6.2f %6.2f %6.2f detected:%2d\n", total, avg * 1000.0, fps, temp, 
                detector.min_length, detector.params.scale_factor, (int)mv.size());


#ifdef TUI
        if (entui)
            tui.loop_end_ui();
#endif
        // Swap front and back buffers
        state.swap_buffers();

        // Poll for and process events
        state.poll_events();


        //exit(1);

    }

    state.terminate();
    return 0;
}

