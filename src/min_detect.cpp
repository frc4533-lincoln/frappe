//
//  main.cpp
//  gles2play
//
//  Created by Simon Jones on 19/03/2022.
//

#include <stdio.h>
#include <dirent.h>
#include <string>


#define GL_SILENCE_DEPRECATION
#include "glhelpers.hpp"
#include "detector.hpp"


void set_timev(uint64_t &t)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    t = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
int diff_timev(uint64_t &t)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t d = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    int delta = d - t;
    t = d;
    return delta;
}



int main(int argc, char **argv)
{



    float ct = 0;
    float et = 0;
    int mode = 1;
    char *fname = 0;
    bool dump = false;
    for(int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-ct"))
        {
            if (i < argc - 1)
            {
                ct = atof(argv[++i]);
            }
            else
            {
                printf("Missing corner threshold value!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-et"))
        {
            if (i < argc - 1)
            {
                et = atof(argv[++i]);
            }
            else
            {
                printf("Missing edge threshold value!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-m"))
        {
            if (i < argc - 1)
            {
                mode = atoi(argv[++i]);
            }
            else
            {
                printf("Missing mode!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-d"))
        {
            dump = true;
        }
        else
        {
            fname = argv[i];
        }
    }

    if (!fname)
    {
        printf( "usage: small <options> <image file>\n"
                "   -ct <val>   corner threshold (0.2)\n"
                "   -et <val>   edge threshold (0.2)\n"
                "   -m <val>    mode (1)"
                "       mode 0 - qpu\n"
                "       mode 1 - qpu, refine corners\n"
                "       mode 2 - gl\n"
                "       mode 3 - gl, refine corners\n"
                "   -d          dump intermediate results\n"
                );
        exit(1);
    }

    printf("ct:%f, et:%f, m:%d, dir:%s\n", ct, et, mode, fname);


    // Set up GL, with a 512x512 window
    const int width = 640;
    const int height = 480;
    State state("Hello world", 0, 0, 1024, 512);
    Texture t_image(state, width, height, true);
    Detector detector(state, width, height, 1.0);

    if (mode == 0)
    {
        detector.gl = false;
        detector.params.corner_refine = false;
    }
    if (mode == 1)
    {
        detector.gl = false;
        detector.params.corner_refine = true;
    }
    if (mode == 2)
    {
        detector.gl = true;
        detector.params.corner_refine = false;
    }
    if (mode == 3)
    {
        detector.gl = true;
        detector.params.corner_refine = true;
    }
    if (dump)
    {
        detector.grab = true;
    }

    t_image.load_texture_data(fname);
    //t_image.dump_file("t0.bin");
    MarkerVec mv;

    mv = detector.detect(t_image);
    printf("detected %d\n", (int)mv.size());
    //exit(1);
    int frames = 0;
    double mean = 0.0;
    double M2 = 0.0;
    double sigma = 0.0;
    int faults = 0;
    for(int j = 0; j < 2000; j++)
    {
        frames++;
        mv = detector.detect(t_image);

        if (mv.size() != 15)
            faults++;

        // printf("detected %d\n", (int)mv.size());
        auto &t = detector.times;
        int all = 0;
        for(int i = 0; i < 8; i++, all += t[i]);

        double delta = all - mean;
        mean += delta / frames;
        M2 += delta * (all - mean);
        sigma = 0.0;
        if (frames > 1)
            sigma = sqrt(M2 / (frames - 1));



        printf("%5d %5d %5d %5d %5d %5d %5d %5d %5d %6.3f %6.3f %5d %6.1f ", 
            t[0],t[1],t[2],t[3],t[4],t[5],t[6],t[7], all, mean*0.001, sigma*0.001, (int)mv.size(), (float)faults * 100 / 2000);
        //state.swap_buffers();
        for(int i = 0; i < mv.size(); i++)
            printf("%3d ", mv[i].id);
        printf("\n");
        if (dump)
            break;
    }

    state.terminate();
    return 0;
}

