//
//  main.cpp
//  gles2play
//
//  Created by Simon Jones on 19/03/2022.
//

#include <stdio.h>
#include <dirent.h>
#include <string>
#include <sched.h>
#include <glob.h>

#define GL_SILENCE_DEPRECATION
#include "glhelpers.hpp"
#include "detector.hpp"





class Stats
{
public:
    Stats(int _size) : size(_size)
    {
        mean.resize(size, 0);
        M2.resize(size, 0);
        sigma.resize(size, 0);
        count = 0;
    }
    void update(int *ticks)
    {
        count++;
        for(int i = 0; i < size; i++)
        {
            double d = ticks[i] * 0.001;
            double delta = d - mean[i];
            mean[i] += delta / count;
            M2[i] += delta * (d - mean[i]);
            if (count > 1)
                sigma[i] = sqrt(M2[i] / (count - 1));
        }
    }
    int size;
    int count;
    std::vector<double> mean;
    std::vector<double> M2;
    std::vector<double> sigma;
};


int main(int argc, char **argv)
{


    printf("Executing as pid:%d\n", getpid());

    float ct = 0;
    float et = 0;
    float scale_factor = 1.0;
    int scale_adapt = 0;
    int mode = 1;
    uint32_t thr_all = 0;
    char *dirname = 0;
    char *dumpdirname = 0;
    char *csvname = 0;
    int scale_mode = 0;
    float beta = 0;
    float alpha = 0;
    bool quiet = false;
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
        else if (!strcmp(argv[i], "-a"))
        {
            if (i < argc - 1)
            {
                alpha = atof(argv[++i]);
            }
            else
            {
                printf("Missing alpha value!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-b"))
        {
            if (i < argc - 1)
            {
                beta = atof(argv[++i]);
            }
            else
            {
                printf("Missing beta value!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-s"))
        {
            if (i < argc - 1)
            {
                scale_mode = atoi(argv[++i]);
            }
            else
            {
                printf("Missing scale mode!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-sa"))
        {
            if (i < argc - 1)
            {
                scale_adapt = atoi(argv[++i]);
            }
            else
            {
                printf("Missing scale adapt mode!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-sf"))
        {
            if (i < argc - 1)
            {
                scale_factor = atof(argv[++i]);
            }
            else
            {
                printf("Missing scale factor!\n");
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
            if (i < argc - 1)
            {
                dumpdirname = argv[++i];
            }
            else
            {
                printf("Missing mode!\n");
                exit(1);
            }
        }
        else if (!strcmp(argv[i], "-q"))
        {
            quiet = true;
        }
        else if (!strcmp(argv[i], "-o"))
        {
            if (i < argc - 1)
            {
                csvname = argv[++i];
            }
            else
            {
                printf("Missing csv filename!\n");
                exit(1);
            }
        }
        else
        {
            dirname = argv[i];
        }
    }

    if (!dirname)
    {
        printf( "usage: small <options> <dir>\n"
                "   -ct <val>   corner threshold (0.2)\n"
                "   -et <val>   edge threshold (0.2)\n"
                "   -sa <val>   scaling adapt\n"
                "       0       off\n"
                "       1       on (default)\n"
                "   -sf <val>   scale factor\n"
                "   -a  <val>   alpha (corner adjust)\n"   
                "   -b  <val>   beta (target min edge size)\n"
                "   -m <val>    mode (1)\n"
                "       mode 0 - qpu\n"
                "       mode 1 - qpu, refine corners\n"
                "       mode 2 - gl\n"
                "       mode 3 - gl, refine corners\n"
                "   -d <dir>    dump rendered frames in dir\n"
                "   -o <file>   output csv"
                "   -q          quiet mode\n"
                );
        exit(1);
    }

    printf("ct:%f, et:%f, m:%d, dir:%s\n", ct, et, mode, dirname);



    // Set up GL, with a 512x512 window
    const int width = 640;
    const int height = 480;
    State state("Hello world", 0, 0, 1024, 512);


    Texture t_image(state, 640, 480, true);
    Detector detector(state, width, height, 1.0);


    // Use default params if not specified on command line
    if (ct > 0)     detector.params.corner_thr = ct;
    if (et > 0)     detector.params.edge_minval = et;
    if (beta > 0)   detector.params.beta = beta;
    if (alpha > 0)  detector.params.alpha = alpha;

    detector.scale_mode = scale_mode;
    detector.params.scale_factor = scale_factor;
    detector.params.scale_adapt = scale_adapt;
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
        //detector.safe = true;
    }


    DIR *d;
    struct dirent *dir;
    d = opendir(dirname);
    if (!d)
    {
        printf("Failed to open %s because %d\n", dirname, errno);
        exit(1);
    }
    closedir(d);


    DIR *dd = 0;
    if (dumpdirname)
    {
        dd = opendir(dumpdirname);
        if (!dd)
        {
            printf("Failed to open %s because %d\n", dumpdirname, errno);
            exit(1);
        }
        closedir(dd);
    }



    std::set<std::string> fnames;
    // ##FIXME## Strange error were some files in an NFS directory were not returned.
    // Possibly related to this?? https://www.php.net/manual/en/function.readdir.php
    // Bug went away when printf put in, and stayed away after taken out! Also did not
    // manifest in separate program using same style after printf here, when it had before.
    // Some sort of caching issue?
    //
    // Bug report above suggested glob() not affected
    //
    // while ((dir = readdir(d)) != NULL)
    // {
    //     //printf("%08x %s\n", dir->d_type, dir->d_name);
    //     if (dir->d_type == DT_REG)
    //     {
    //         fnames.insert(dir->d_name);
    //     }
    // }

    char buf[16384];
    sprintf(buf, "%s/*.jpg", dirname);
    glob_t globbuf;
    glob(buf, 0, NULL, &globbuf);
    printf("Found %d files\n", globbuf.gl_pathc);
    for(int i = 0; i < globbuf.gl_pathc; i++)
    {
        fnames.insert(globbuf.gl_pathv[i]);
    }


    
    // Container for mean + std running calc
    Stats stats(9);
    int frames = 0;
    int large = 0;
    int small = 0;
    Timer ts;



    FILE *csv = 0;
    if (csvname)
    {
        csv = fopen(csvname, "w");
        if (!csv)
        {
            printf("Failed to open CSV output file errno:%d\n", errno);
            exit(1);
        }
    }

    printf("Got %d file names\n", fnames.size());
    for(auto a : fnames)
    {
        std::string full_name = a;
            
        if (!quiet) printf("%s  ", full_name.c_str());

        t_image.load_texture_data(full_name.c_str());

        if (frames == 0)
        {
            // First image run twice to warm up caches
            MarkerVec mv = detector.detect(t_image);
        }


        frames += 1;       

        ts.set_time();
        MarkerVec mv = detector.detect(t_image);
        int d = ts.diff_time();
        // Times are:
        //  0   set uniforms
        //  1   sobel gradients, gradient angle descretisation
        //  2   find local maxima edge pixels, structure matrix, min eigenvalue
        //  3   promote weak edges surrounded by strong
        //  4   create empty tile mask
        //  5   find borders
        //  6   sort points clockwise, eliminate unsquare, refine corners
        //  7   extract codes
        auto &t = detector.times;
        int all = 0;
        for(int i = 0; i < 8; all += t[i++]);
        t[8] = all;

        stats.update(t);



        float temp = (float)getTemperature(state.mb) / 1000.0;
        // 1 1110 0000 0000 0000 1010
        // | |||                 ||||_ under-voltage
        // | |||                 |||_ currently throttled
        // | |||                 ||_ arm frequency capped
        // | |||                 |_ soft temperature reached
        // | |||_ under-voltage has occurred since last reboot
        // | ||_ throttling has occurred since last reboot
        // | |_ arm frequency capped has occurred since last reboot
        // |_ soft temperature reached since last reboot
        uint32_t thr = get_throttled(state.mb);
        thr_all |= thr;


        for (size_t i = 0; i < mv.size(); i++)
            if(mv[i].id >= 0)
            {
                if (mv[i].id < 100)
                    large++;
                else
                    small++;
            }
        if (!quiet)
        {
            printf("%5d %5d %5d %5d %5d %5d %5d %5d %5d %5d %6.3f %6.3f %6.3f %6x %6.3f %6.3f %6.3f ", 
                t[0],t[1],t[2],t[3],t[4],t[5],t[6],t[7],
                all, d, stats.mean[8], stats.sigma[8], temp, thr, detector.min_length, detector.min_length_candidate, 
                detector.params.scale_factor);

            for (size_t i = 0; i < mv.size(); i++)
                if(mv[i].id >= 0)
                    printf("%3d ", mv[i].id);
            printf("\n");
        }

        if (dd)
        {
            // We are dumping rendered output
            full_name = std::string(dumpdirname) + "/" + a;
            cv::Mat result = detector.render_to_mat(t_image);
            cv::imwrite(full_name, result);
        }

        if (csv && mv.size())
        {
            for (int i = 0; i < mv.size(); i++)
            {
                auto &m = mv[i];
                fprintf(csv, "%5d, %3d, %8.3f, %8.3f, %8.3f, %8.3f, %8.3f, %8.3f, %8.3f, %8.3f\n", frames - 1, m.id,
                    m.p[0].x, m.p[0].y, m.p[1].x, m.p[1].y, m.p[2].x, m.p[2].y, m.p[3].x, m.p[3].y);
            }
        }

    }
    if (thr_all & 0xf)
        printf("*** CPU throttling occurred during benchmark! ***\n");
    printf("\n-ct %4.2f -et %4.2f -sa %d ", detector.params.corner_thr, detector.params.edge_minval, detector.params.scale_adapt);
    if (!detector.params.scale_adapt) printf("-sf %4.2f ", detector.params.scale_factor);
    printf("-b %5.1f -m %d %s\n", detector.params.beta, mode, dirname);
    printf("Frames: %5d detected: %5d tpf: %6.3f avg time: %6.3f std time: %6.3f\n",
        frames, large + small, 
        (float)(large + small) / frames, stats.mean[8], stats.sigma[8]);

    printf(" Scale  Sobel   Edge   Mask   Bdrs Refine Extrct Decode\n");
    printf("%6.3f %6.3f %6.3f %6.3f %6.3f %6.3f %6.3f %6.3f\n",
        stats.mean[0], stats.mean[1], stats.mean[2], stats.mean[3], stats.mean[4], stats.mean[5], stats.mean[6], stats.mean[7]);


    // printf("Scaling:            %6.3f %6.3f\n", stats.mean[0], stats.sigma[0]);
    // printf("Sobel grad angle:   %6.3f %6.3f\n", stats.mean[1], stats.sigma[1]);
    // printf("Edge, corner:       %6.3f %6.3f\n", stats.mean[2], stats.sigma[2]);
    // printf("Mask gen:           %6.3f %6.3f\n", stats.mean[3], stats.sigma[3]);
    // printf("Find borders:       %6.3f %6.3f\n", stats.mean[4], stats.sigma[4]);
    // printf("Refine corners:     %6.3f %6.3f\n", stats.mean[5], stats.sigma[5]);
    // printf("Extract:            %6.3f %6.3f\n", stats.mean[6], stats.sigma[6]);
    // printf("Decode:             %6.3f %6.3f\n", stats.mean[7], stats.sigma[7]);

    if (csv)
        fclose(csv);

    state.terminate();
    return 0;
}

