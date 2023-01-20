

#include "detector.hpp"

#include "shaders.hpp"

#include <map>
#include <dirent.h>

#define OPENCVSUBPIX

// This is of marginal use on RPI3 but very effective in RPI0
#define BMASK


#define offset32bit(x, y) (((x) << 2) + ((y) << 10))
#define offset(x, y, z) (((x) << 1) + ((y) << (z)))

Detector::Detector(State &_state, int _width, int _height, int tc, int tr, int tw, int th)
:   state       (_state),
    width       (_width),
    height      (_height),
    t_buffer({  Texture(_state, _width, _height, false), 
                Texture(_state, _width, _height, false)}),
    // This allows for up to 32 fiducials detected per image
    t_fid   (   Texture(_state, 128, 64, true)),
    t_output(   Texture(_state, _width, _height, true)),
    // Smallest VCSM buffer available, this is large enough for 1024x1024 mask
    t_mask  (   Texture(_state, 64, 64, false)),
    t_codes (   Texture(_state, 64, 64, false)),
    t_decode(   Texture(_state, 64, 64, false)),


    m_fid   (   Mesh({"v_pos", "v_tex"}, {3, 2}))


{
    p_shi_tomasi    = new Program(get_file("../shaders/vs.glsl"), get_file("../shaders/sobel_shi_tomasi.glsl"), "shi_tomasi");
    p_suppress      = new Program(get_file("../shaders/vs.glsl"), get_file("../shaders/suppress.glsl"), "suppress");
    p_null          = new Program(get_file("../shaders/vs.glsl"), get_file("../shaders/null.glsl"), "");
    p_showcorners   = new Program(get_file("../shaders/vs_tex.glsl"), get_file("../shaders/showcorners.glsl"), "showcorners");
    p_showcontours  = new Program(get_file("../shaders/vs_tex.glsl"), get_file("../shaders/showcontours.glsl"), "showcontours");
    p_colour        = new Program(get_file("../shaders/vs_simple.glsl"), get_file("../shaders/colour.glsl"), "colour");
    dict            = new Dictionary(t_codes);

    p_qpu_blit          = new QPUprogram(state, "qpu_blit_tiled.bin", 0, 10, 15, 64, 32, 64);
    p_qpu_shi_tomasi    = new QPUprogram(state, "qpu_shi_tomasi_opt_tiled.bin", 0, 10, 15, 64, 32, 64);
    p_qpu_shi_tomasi_scale  = new QPUprogram(state, "qpu_shi_tomasi_scale_tiled.bin", 33, 10, 15, 64, 32, 64, 1024);
    //p_qpu_suppress      = new QPUprogram(state, "qpu_suppress_tiled.bin",   2, 10, 15, 64, 32, 64);
    p_qpu_suppress      = new QPUprogram(state, "qpu_suppress_opt_tiled.bin",   2, 10, 15, 64, 32, 64);
    p_qpu_warp          = new QPUprogram(state, "qpu_warp.bin", 16, 8, 4, 16, 16, 64);
    p_vpu_functions     = new VPUprogram(state, "functions.elf");

    // Set up the uniforms that don't change
    int potw = t_buffer[0].buffer_width;
    int poth = t_buffer[0].buffer_height;
    p_shi_tomasi->set_uniform("hpix", {1.0f / potw});
    p_shi_tomasi->set_uniform("vpix", {1.0f / poth});

    p_suppress->set_uniform("hpix", {1.0f / potw});
    p_suppress->set_uniform("vpix", {1.0f / poth});
    // Colour of lines
    p_colour->set_uniform("colour", {0.0,1.0,1.0,1.0});



    // Timing steps
    //  0   set uniforms, rescale
    //  1   sobel, shi tomasi
    //  2   edges, corners
    //  3   mask
    //  4   borders
    //  5   refine corners
    //  6   extract
    //  7   decode


    // The textures on RPI are VCSM buffer backed, which are raster scan order. 
    // It appears that performing render to texture which targets a vcsm texture
    // results in a y-flip. The MESA driver DMABUF backed texure does not 
    // suffer from this.
    //
    // In order to get around this, we invert the y-flip using the texture
    // coordinates.
    y_flip  = true;


    // We need to account for the fact that the input width and height 
    // may not be power-of-two, but the texture must be, so we are not using the 
    // whole texture, but just the correct number of pixels.
    float hratio = (float)width / (float)potw;
    float vratio = (float)height / (float)poth;
    float xmin = -1.0;
    float ymin = -1.0;
    float xmax = -1.0 + hratio * 2.0;
    float ymax = -1.0 + vratio * 2.0;
    float smin = 0.0;
    float smax = hratio;

    float tmin = y_flip ? 1.0 - vratio  : vratio;
    float tmax = y_flip ? 1.0           : 0.0;

    // The mesh consist of a triangle strip comprising two triangles
    m_quad = new Mesh({"v_pos", "v_tex"}, {3, 2}, {
        xmin,   ymin,   0,  smin,   tmax,
        xmin,   ymax,   0,  smin,   tmin,
        xmax,   ymin,   0,  smax,   tmax,
        xmax,   ymax,   0,  smax,   tmin
    });
    
    //-----------------------------------------
    min_length      = 0;

    xmax = -1.0 + hratio * 2.0 * params.scale_factor;
    ymax = -1.0 + vratio * 2.0 * params.scale_factor;

    m_quad_scale = new Mesh({"v_pos", "v_tex"}, {3, 2}, {
        xmin,   ymin,   0,  smin,   tmax,
        xmin,   ymax,   0,  smin,   tmin,
        xmax,   ymin,   0,  smax,   tmax,
        xmax,   ymax,   0,  smax,   tmin
    });
    

    state.setup_perf_counters();

    // See if we are an RPI0
    {
        DIR *dir = opendir("/sys/devices/armv6_1176");
        if (dir)
            gl_extract_thresh = 7;
        else
            gl_extract_thresh = 25;
        printf("Setting extract threshold to %d\n", gl_extract_thresh);
    }


    tui             = false;
    safe            = false;
    gl              = false;
    draw_fid        = false;
    draw_atlas      = false;
    grab            = false;
    nextract        = true;
    showpass        = -1;




    // Force no scaling for first frame
    scaled_frames   = params.max_scaled_frames;
}

void Detector::textwin(Texture &buf)
{
    if(0 && tui)
    {
        uint16_t *pix   = (uint16_t *)buf.user_lock();
        int stride      = buf.buffer_width;
        int width       = buf.width;
        int height      = buf.height;

        int shift = 9;
        if (stride == 64) shift = 7;
        if (stride == 128) shift = 8;
        if (stride == 256) shift = 9;
        if (stride == 512) shift = 10;
        if (stride == 1024) shift = 11;
        for(int y = 0; y < 64; y++)
        {
            move(y, 0);
            for(int x = 0; x < 64; x++)
            {
                uint16_t p = pix[offset(x + params.xw, y + params.yw, shift)];
                printw("%04x ", p);
            }
        }
        buf.user_unlock();
    }
}



//--------------------------------------------------------------------------------------
//
// Fiducial detection 
//
MarkerVec Detector::detect(Texture &input, Texture &output)
{
    detect(input);
    // render to output

    return mv;
}

MarkerVec Detector::detect(Texture &input)
{
    times[15] = 0;
    main_timer.set_time();
    



    if (input.width != width || input.height != height)
    {
        printf("Mismatch between input texture size and detector size input:%d %d detector:%d %d\n",
            input.width, input.height, width, height);
        endwin();
        exit(1);
    }

    // -----------------------------------------------------------------------
    // Adaptive scaling
    //
    // Adjust the scale factor if running scale_adapt mode
    //
    // Beta is the minimum length that the smallest edge shuld have in the rescaled
    // image
    //
    // If there have been more than max_scaled_frames, we switch to full scale to 
    // mitigate against the situation where a large fiducial allows scale down to happen,
    // then an additional small fiducial enters the frame

    if (params.scale_adapt)
    {
        // Default to second largest scale factor
        params.scale_factor = sfactors[sfactors.size() - 4];
        if (min_length > 0)
        {
            // There was a detection last frame, see if we can work with scaled down image
            params.scale_factor = sfactors[sfactors.size() - 1];
            float factor = params.beta / min_length;
            for (int i = 0; i < sfactors.size(); i++)
            {
                if (factor < sfactors[i])
                {
                    params.scale_factor = sfactors[i];
                    break;
                }
            }
            //printf("beta:%6.2f min_length:%6.2f factor:%6.2f sf:%6.2f\n", params.beta, min_length, factor, params.scale_factor);
        }
        if (scaled_frames >= params.max_scaled_frames)
            params.scale_factor = 1.0f;
        

        // FIXME!! Adjust the render mesh for scaling (only need if using gl for scale)

    }

    if (params.scale_factor < 1.0f)
        scaled_frames++;
    else
        scaled_frames = 0;


    // Set up the uniforms that may have changed
    p_suppress->set_uniform("relax_suppress", {params.relax_suppress});
    p_suppress->set_uniform("corner_thr", {params.corner_thr});
    p_suppress->set_uniform("minval", {params.edge_minval});


    p_qpu_suppress->set_uniform(0, float2bits(params.corner_thr));
    p_qpu_suppress->set_uniform(1, float2bits(params.edge_minval));

    state.clear_perf_counters();
    


    if(tui)state.report_perf_counters(67, 90);

    times[0] = main_timer.diff_time();    

    // input.user_lock();
    // input.user_unlock();

    // Capture downscaled input if monitoring
    state.clear_perf_counters();
    mvprintw(66, 90, "     inst       cyc  load%%   ast%%   tst%%       tex  miss%%        ic  miss%%        l2  miss%%   ");
    //                --------- --------- -------- -------- --------- --------- --------- --------- 
    if (showpass==0) 
    {
        render_pass(state.fbo, *m_quad, *p_null, input, t_output); 
        glFinish();
        times[15] = main_timer.diff_time();
    }
    if (showpass==5) 
    {
        render_pass(state.fbo, *m_quad, *p_null, t_buffer[1], t_output); 
        glFinish();
        times[15] = main_timer.diff_time();
    }




    if (grab) {glFinish(); input.dump_file("p0.bin");}

    //--------------------------------------------------------------------
    // Perform Canny edge detection and Shi Tomasi corner detection.
    // The detectors are interleaved because they both use Sobel
    // and we also maximise use of data that is streamed to and from
    // memory anyway
    //
    // First stage - Sobel x, y gradients, gradient vector, normalise,
    // and discretise the gradient angle
    state.clear_perf_counters();
    GLCHECK("pre render pass\n");



    if (params.scale_factor < 1.0)
    {
        if (gl) 
            render_pass(state.fbo, *m_quad_scale, *p_shi_tomasi, input, t_buffer[0]);
        else 
        {   
            p_qpu_shi_tomasi_scale->execute_sc2(input, t_buffer[0], true, params.scale_factor, true);
        }
    }
    else
    {
        if (gl) render_pass(state.fbo, *m_quad, *p_shi_tomasi, input, t_buffer[0]);
        else
        {    
            p_qpu_shi_tomasi->execute(input, t_buffer[0], true, params.scale_factor);
        }
    }

    if (safe) glFinish();
    times[1] = main_timer.diff_time();
    if(tui)state.report_perf_counters(68, 90);

    if (grab) {glFinish(); t_buffer[0].dump_file("p1.bin");}
    
    if (showpass==1) 
    {
        render_pass(state.fbo, *m_quad, *p_null, t_buffer[0], t_output); 
        glFinish();
        times[15] = main_timer.diff_time();
        textwin(t_buffer[1]);
    }

    //--------------------------------------------------------------------
    // Suppress weak edges and get the Shi-Tomasi minimum eigenvalue
    // from the structure matrix
    state.clear_perf_counters();
    if (gl) render_pass(state.fbo, *m_quad, *p_suppress, t_buffer[0], t_buffer[1]);
    else
    {
        p_qpu_suppress->execute(t_buffer[0], t_buffer[1], true, params.scale_factor);
    }
    if (safe) glFinish();
    times[2] = main_timer.diff_time();
    if(tui)state.report_perf_counters(69, 90);
    if (grab) {glFinish(); t_buffer[1].dump_file("p2.bin");}

    if (showpass==2) 
    {
        render_pass(state.fbo, *m_quad, *p_null, t_buffer[1], t_output); 
        glFinish();
        times[15] = main_timer.diff_time();
        textwin(t_buffer[0]);
    }

 

    if (showpass==3) 
    {
        render_pass(state.fbo, *m_quad, *p_showcorners, t_buffer[1], t_output); 
        glFinish();
        times[15] = main_timer.diff_time();
        textwin(t_buffer[1]);
    }



    if(gl)glFinish();
#ifdef BMASK
    if (params.scale_factor == 1.0)
    {
        // The cost of the masking operation only makes sense when there is no downscale cost
        p_vpu_functions->execute(VPU_MAKE_MASK, t_buffer[1].bus_address, 4096, t_mask.bus_address, width, height);

        // Bandwidth test - consistant 1.55-1.75ms
        // 1024 blocks of 4096 bytes = 4194304 / 1.65e-3 = 2.54GBytes/s 
        // Theoretical LPDDR2 @450MHz = 450e6 * 2 * 4 = 3.6GBytes
        //p_vpu_functions->execute(VPU_BANDWIDTH, t_buffer[1].bus_address, 4096, 0x400, 0, 0);
    }
#endif

    times[3] = main_timer.diff_time();

    //--------------------------------------------------------------------
    // Trace the outer contours using Suzuki findContours code from OpenCV
    // modified to use interleaved data and keep track of locations of 
    // corner pixels on the contours
    mv.clear();
    find_borders<Texture>(t_buffer[1], mv, t_mask, params.scale_factor);

    times[4] = main_timer.diff_time();
    if (grab) {glFinish(); t_buffer[1].dump_file("p3.bin");}
    
    if (showpass==4) 
    {
        render_pass(state.fbo, *m_quad, *p_showcontours, t_buffer[1], t_output); 
        glFinish();
        times[15] = main_timer.diff_time();
        if(tui)textwin(t_buffer[1]);
    }
    

    //--------------------------------------------------------------------
    // Order and refine the corners, eliminating ones that are too distorted
    //
    // Sort marker corners
    for(auto &m : mv)
    {
        sort_points_clockwise(m);
    }
    // Remove candidates with too large a difference in lengths of sides and
    // scale correctly back to input size
    eliminate_unsquare();

    // Refine corner position using original resolution input
    refine_corners(input);
    times[5] = main_timer.diff_time();



    //--------------------------------------------------------------------
    // Warp the identified candidates and run decode with error correction
    //
    // Extract codes
    if (!nextract)
        extract(input);
    else
        extract2(input);
    //glFinish();
    times[6] = main_timer.diff_time();

    // Decode hamming - single symbol - 550us, 15 symbols 5100us
    // VPU - single 100us, 15 ~900us
    decode_vpu();
    //decode_hamming();


    // Find the smallest edge length of all candidates
    // if (mv.size())
    // {
    //     min_length_candidate = 100000000;
    //     for(auto &m : mv)
    //     {
    //         for(int i = 0; i < 4; i++)
    //         {
    //             auto dx = m.p[i].x - m.p[(i + 1) % 4].x;
    //             auto dy = m.p[i].y - m.p[(i + 1) % 4].y;
    //             float len = dx * dx + dy * dy;
    //             if (len < min_length_candidate)
    //                 min_length_candidate = len;
    //         }
    //     }
    //     min_length_candidate = sqrt(min_length_candidate);
    // }
    // else
    // {
    //     min_length_candidate = 0;
    // }

    // Remove any candidates that have not been identified
    mv_pre_elim = mv;
    eliminate_invalid();   

    // Find the smallest edge length of identified candidates
    if (mv.size())
    {
        min_length = 100000000;
        for(auto &m : mv)
        {
            for(int i = 0; i < 4; i++)
            {
                auto dx = m.p[i].x - m.p[(i + 1) % 4].x;
                auto dy = m.p[i].y - m.p[(i + 1) % 4].y;
                float len = dx * dx + dy * dy;
                if (len < min_length)
                    min_length = len;
            }
        }
        min_length = sqrt(min_length);
    }
    else
    {
        min_length = 0;
    }



    times[7] = main_timer.diff_time();


    if(tui)state.report_perf_counters(74, 90);
    
    if(tui)
    {
        mvprintw(10, 0, "Candidates %3d ", mv.size());
        for(int i = 0; i < mv.size(); i++)
            mvprintw(11 + i, 0, "%3x %6.2f % 6.1f % 6.1f % 6.1f % 6.1f % 6.1f % 6.1f % 6.1f % 6.1f", 
                mv[i].id, mv[i].sad,
                mv[i].p[0].x, mv[i].p[0].y, 
                mv[i].p[1].x, mv[i].p[1].y, 
                mv[i].p[2].x, mv[i].p[2].y, 
                mv[i].p[3].x, mv[i].p[3].y);
    }

    grab = false;

    // Sort the candidates by ID number
    std::sort(mv.begin(), mv.end());
    return mv;
}





void drawFrameAxes(cv::Mat &image, cv::Mat &cameraMatrix, cv::Mat &distCoeffs,
                   cv::Mat &rvec, cv::Mat &tvec, float length, int thickness, float scale_factor)
{

    //int type = image.type();
    //int cn = CV_MAT_CN(type);
    //cv::CV_CheckType(type, cn == 1 || cn == 3 || cn == 4,
    //             "Number of channels must be 1, 3 or 4" );

    //CV_Assert(image.getMat().total() > 0);
    //CV_Assert(length > 0);

    // project axes points
    // Flip Z to compensate for Y increasing downwards giving left-handed coordinate 
    // (###FIXME!! CHECK! Somewhere there is a coordinate system flip, OpenCV is supposed to
    // be right-handed)
    // NOTE!!
    // Firther investigation - axes sometimes flip randomly depending on orientation, 
    std::vector<cv::Point3f> axesPoints;
    axesPoints.push_back(cv::Point3f(0, 0, 0));
    axesPoints.push_back(cv::Point3f(length, 0, 0));
    axesPoints.push_back(cv::Point3f(0, length, 0));
    axesPoints.push_back(cv::Point3f(0, 0, length));
    std::vector<cv::Point2f> imagePoints;
    cv::projectPoints(axesPoints, rvec, tvec, cameraMatrix, distCoeffs, imagePoints);
    imagePoints[0].x *= scale_factor;
    imagePoints[1].x *= scale_factor;
    imagePoints[2].x *= scale_factor;
    imagePoints[3].x *= scale_factor;
    imagePoints[0].y *= scale_factor;
    imagePoints[1].y *= scale_factor;
    imagePoints[2].y *= scale_factor;
    imagePoints[3].y *= scale_factor;

    mvprintw(60, 100, "%f %f (%f %f) (%f %f)    ", scale_factor, scale_factor, imagePoints[0].x, imagePoints[0].y, imagePoints[1].x, imagePoints[1].y);
    // draw axes lines
    // Colours the wrong way round because buffer is effectively RGB rather than BGR as Opencv
    // expects
    cv::line(image, imagePoints[0], imagePoints[1], cv::Scalar(255, 0, 0), thickness);
    cv::line(image, imagePoints[0], imagePoints[2], cv::Scalar(0, 255, 0), thickness);
    cv::line(image, imagePoints[0], imagePoints[3], cv::Scalar(0, 0, 255), thickness);
    cv::putText(image, "x", imagePoints[1], cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 0, 0, 255), 0.8, cv::LINE_8, true);
    cv::putText(image, "y", imagePoints[2], cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0, 255), 0.8, cv::LINE_8, true);
    cv::putText(image, "z", imagePoints[3], cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255, 255), 0.8, cv::LINE_8, true);
}

void Detector::render(Texture &input, bool axes)
{
    if (axes)
    {
        char *pix = t_output.user_lock();
        cv::Mat cvbuf(t_output.buffer_height, t_output.buffer_width, CV_8UC4, pix);
        for(auto &m : mv)
        {
            //cv::line(cvbuf, cv::Point2f(0,0), cv::Point2f(100,100), cv::Scalar(0,255,255), 3);
            //drawFrameAxes(cvbuf, camera.camera_matrix, camera.dist_coeffs, m.rvec, m.tvec, 20.0, 1, scale_factor);
            float scale = 0.5;
            cv::Size s = cv::getTextSize(std::to_string(m.id), cv::FONT_HERSHEY_SIMPLEX, scale, cv::LINE_8, 0);
            cv::Point loc((m.p[0].x + m.p[2].x - s.width) / 2, (m.p[0].y + m.p[2].y - s.height) / 2);
            cv::putText(cvbuf, std::to_string(m.id), loc, cv::FONT_HERSHEY_SIMPLEX, scale, 
                cv::Scalar(255,255,0), 1, cv::LINE_8, true);
        }
        t_output.user_unlock();
    }
    do_render(input);
    glFinish();
    times[14] = main_timer.diff_time();
}

cv::Mat Detector::render_to_mat(Texture &input)
{
    // Return an opencv mat of the input texture with boxes and text rendered into it
    char *pix = input.user_lock();
    cv::Mat cvbuf(input.buffer_height, input.buffer_width, CV_8UC4, pix);
    for(auto &m : mv)
    {
        //cv::line(cvbuf, cv::Point2f(0,0), cv::Point2f(100,100), cv::Scalar(0,255,255), 3);
        //drawFrameAxes(cvbuf, camera.camera_matrix, camera.dist_coeffs, m.rvec, m.tvec, 20.0, 1, scale_factor);

        cv::line(cvbuf, cv::Point2f(m.p[0].x, m.p[0].y), cv::Point2f(m.p[1].x, m.p[1].y), cv::Scalar(0,255,255), 1);
        cv::line(cvbuf, cv::Point2f(m.p[1].x, m.p[1].y), cv::Point2f(m.p[2].x, m.p[2].y), cv::Scalar(0,255,255), 1);
        cv::line(cvbuf, cv::Point2f(m.p[2].x, m.p[2].y), cv::Point2f(m.p[3].x, m.p[3].y), cv::Scalar(0,255,255), 1);
        cv::line(cvbuf, cv::Point2f(m.p[3].x, m.p[3].y), cv::Point2f(m.p[0].x, m.p[0].y), cv::Scalar(0,255,255), 1);
        float scale = 0.5;
        cv::Size s = cv::getTextSize(std::to_string(m.id), cv::FONT_HERSHEY_SIMPLEX, scale, cv::LINE_8, 0);
        cv::Point loc((m.p[0].x + m.p[2].x - s.width) / 2, (m.p[0].y + m.p[2].y - s.height) / 2);
        cv::putText(cvbuf, std::to_string(m.id), loc, cv::FONT_HERSHEY_SIMPLEX, scale, 
            cv::Scalar(255,255,0), 1, cv::LINE_8, true);
    }
    input.user_unlock();
    // Get just the relevant part of the image
    cv::Mat result(cvbuf, cv::Rect(0, 0, input.width, input.height));
    // Image is flipped, reverse that
    cv::flip(result, result, 0);
    return result;
}

void Detector::eliminate_unsquare()
{
    // Upscale coordinates to represent origin data, then check for maximum ratio
    // of lengths
    MarkerVec new_mv;
    new_mv.reserve(mv.size());
    for(auto &m : mv)
    {
        float l[4];
        float min = 99999;
        float max = 0;
        for(int i = 0; i < 4; i++)
        {
            m.p[i].x /= params.scale_factor;
            m.p[i].y /= params.scale_factor;
        }
        for(int i = 0; i < 4; i++)
        {
            float dx = m.p[i].x - m.p[(i + 1) % 4].x;
            float dy = m.p[i].y - m.p[(i + 1) % 4].y;
            l[i] = sqrt(dx * dx + dy * dy);
            if (l[i] < min) min = l[i];
            if (l[i] > max) max = l[i];
        }
        if (max < params.lratio * min)
            new_mv.push_back(m);
    }
    mv = new_mv;
}

void Detector::eliminate_invalid()
{
    MarkerVec new_mv;
    new_mv.reserve(mv.size());
    for(auto &m : mv)
    {
        if (m.id >=0)
            new_mv.push_back(m);
    }
    mv = new_mv;
}

void Detector::calculate_extrinsics(float size)
{
    float s2 = size / 2;
    std::vector<cv::Point3f> obj = {cv::Point3f(-s2, s2, 0),
                                    cv::Point3f(s2, s2, 0),
                                    cv::Point3f(s2,-s2, 0),
                                    cv::Point3f(-s2, -s2, 0)};
    for(int i = 0; i < mv.size(); i++)
    {
        auto &m = mv[i];
        std::vector<cv::Point2f> p;
        p.push_back({m.p[0].x, m.p[0].y});
        p.push_back({m.p[1].x, m.p[1].y});
        p.push_back({m.p[2].x, m.p[2].y});
        p.push_back({m.p[3].x, m.p[3].y});
        //cv::Mat dc;
        cv::Mat rv, tv;
        auto &cm = camera.camera_matrix;
        //mvprintw(67 + i, 200, "%8.3f %8.3f %8.3f %8.3f ", cm.at<float>(0,0), cm.at<float>(0,2),cm.at<float>(1,1),cm.at<float>(1,2));
        cv::solvePnP(obj, p, camera.camera_matrix, camera.dist_coeffs, rv, tv);
        rv.convertTo(m.rvec, CV_32F);
        tv.convertTo(m.tvec, CV_32F);
        // mvprintw(67 + i, 250, ">>>% 8.3f % 8.3f % 8.3f                 ", 
        //     m.tvec.at<float>(0), m.tvec.at<float>(1), m.tvec.at<float>(2));
    }        
}


void Detector::refine_corners(Texture &buf)
{
    // The corner detection algo finds the average location of the cluster of 
    // corner pixels. There are typically 2, 3, 4 corner pixels, arranged around 
    // the radius of the corner. This tends to pull the mean location in
    // towards the centre of the box. Here we use a modified version of the 
    // OpenCV cornerSubPix routine on the original resolution input to refine
    // the actual location

    // There is no need to perform any cache operations, because the input
    // buffer is only ever written by the CPU. This saves about 500us on
    // Rockpi4 and ~700us on the RPi3
    char *pix = buf.user_buffer;

    if (mv.size())
    {


        std::vector<cv::Point2f> pts;
        std::vector<cv::Point2f> pts_orig;
        std::vector<cv::Point2f> pts2;

        for(int i = 0; i < mv.size(); i++)
        {
            pts.push_back({mv[i].p[0].x, mv[i].p[0].y});
            pts.push_back({mv[i].p[1].x, mv[i].p[1].y});
            pts.push_back({mv[i].p[2].x, mv[i].p[2].y});
            pts.push_back({mv[i].p[3].x, mv[i].p[3].y});
        }
        pts_orig = pts;

        float total_error = 0.0;
        float max_error = 0.0;
        float mean_error = 0.0;




        if (params.corner_refine)
        {
            // We use a modified version of cornerSubPix that can accept four channel
            // input. It locally creates single channel for the interpolation, avoiding the
            // need for an extractChannel operation on the whole image, which takes
            // >1ms on RPI3, more on RPI0
            //
            // The window we use depends on the scale factor, the error is likely larger with big scaledowns
            // so we increase the search area. This costs more time, but we have already gained by
            // using a larger scaledown
            int window = 2.0 / params.scale_factor;
            //int window = 2;
            pts2 = pts;
            cv::TermCriteria criteria = cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 4, 0.1);
            cv::Mat a(buf.buffer_height, buf.buffer_width, CV_8UC4, pix);
            localcornerSubPix(a, pts2, cv::Size(window, window), cv::Size(-1, -1), criteria);
        }
        else
        {
            // If not doing true corner refinement, we use an approximation. Given that we will generally
            // have 2-4 corner points around the corner on the boundary, the mean position will be
            // pulled towards the centre of the quadrangle. We extend the corner location outwards from
            // the centre by some amount that is scale dependent. This is not as accurate as 
            // cornerSubPix, but much cheaper.
            const float corner_factor = params.alpha / params.scale_factor;
            for(int i = 0; i < mv.size(); i++)
            {
                cv::Point2f &p0 = pts[i * 4];
                cv::Point2f &p1 = pts[i * 4 + 1];
                cv::Point2f &p2 = pts[i * 4 + 2];
                cv::Point2f &p3 = pts[i * 4 + 3];
                cv::Vec2f c0 = (p0 + p2) / 2.0;
                cv::Vec2f c1 = (p1 + p3) / 2.0;
                cv::Vec2f d0 = cv::Vec2f(p0) - c0;
                cv::Vec2f d1 = cv::Vec2f(p1) - c1;
                cv::Vec2f d2 = cv::Vec2f(p2) - c0;
                cv::Vec2f d3 = cv::Vec2f(p3) - c1;
                p0 = corner_factor * cv::normalize(d0) + d0 + c0;
                p1 = corner_factor * cv::normalize(d1) + d1 + c1;
                p2 = corner_factor * cv::normalize(d2) + d2 + c0;
                p3 = corner_factor * cv::normalize(d3) + d3 + c1;
            }
        }


        for(int i = 0; i < mv.size(); i++)
        {
            if (params.corner_refine)
            {
                auto d0 = cv::norm(pts[i*4] - pts2[i*4]);
                auto d1 = cv::norm(pts[i*4+1] - pts2[i*4+1]);
                auto d2 = cv::norm(pts[i*4+2] - pts2[i*4+2]);
                auto d3 = cv::norm(pts[i*4+3] - pts2[i*4+3]);
                total_error += d0 + d1 + d2 + d3;

                max_error = fmax(max_error, d0);
                max_error = fmax(max_error, d1);
                max_error = fmax(max_error, d2);
                max_error = fmax(max_error, d3);
                mean_error += d0 / (mv.size() * 4);

                if (tui)
                {
                    mvprintw(i + 10, 100, "%2d %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f | % 6.2f % 6.2f % 6.2f % 6.2f | % 6.2f % 6.2f % 6.2f % 6.2f   ",
                    i,
                    mv[i].p[0].x, pts[i*4].x, mv[i].p[0].y, pts[i*4].y, 
                    mv[i].p[1].x, pts[i*4+1].x, mv[i].p[1].y, pts[i*4+1].y, 
                    mv[i].p[2].x, pts[i*4+2].x, mv[i].p[2].y, pts[i*4+2].y, 
                    mv[i].p[3].x, pts[i*4+3].x, mv[i].p[3].y, pts[i*4+3].y,
                    cv::norm(pts_orig[i*4] - pts2[i*4]),
                    cv::norm(pts_orig[i*4+1] - pts2[i*4+1]),
                    cv::norm(pts_orig[i*4+2] - pts2[i*4+2]),
                    cv::norm(pts_orig[i*4+3] - pts2[i*4+3]),
                    cv::norm(pts[i*4] - pts2[i*4]),
                    cv::norm(pts[i*4+1] - pts2[i*4+1]),
                    cv::norm(pts[i*4+2] - pts2[i*4+2]),
                    cv::norm(pts[i*4+3] - pts2[i*4+3])
                    );
                }

                mv[i].p[0] = {pts2[i*4].x, pts2[i*4].y};
                mv[i].p[1] = {pts2[i*4+1].x, pts2[i*4+1].y};
                mv[i].p[2] = {pts2[i*4+2].x, pts2[i*4+2].y};
                mv[i].p[3] = {pts2[i*4+3].x, pts2[i*4+3].y};
            }
            else
            {
                mv[i].p[0] = {pts[i*4].x, pts[i*4].y};
                mv[i].p[1] = {pts[i*4+1].x, pts[i*4+1].y};
                mv[i].p[2] = {pts[i*4+2].x, pts[i*4+2].y};
                mv[i].p[3] = {pts[i*4+3].x, pts[i*4+3].y};
            }
        }
        if(tui)mvprintw(9, 100, "Total error %8.2f    max error %8.2f   mean_error %8.2f", total_error, max_error, mean_error);
    }

}

void Detector::do_render(Texture &buf)
{

    if (draw_fid)
    {
        t_fid.bind(GL_TEXTURE0);
        state.bind_fb_screen();
        glClearColor(0.1, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        Mesh full({"v_pos", "v_tex"}, {3, 2}, 
            {   -1, -1, 0,  0,  0,
                 1, -1, 0,  1,  0,
                -1,  1, 0,  0,  1,
                 1,  1, 0,  1,  1});
        full.draw(*p_null, GL_TRIANGLE_STRIP);
        
        std::vector<float> v;
        v.clear();
        float fid_xs = 0.5;
        for(int i = 0; i < 8; i++)
        {
            float m = -1.0 + (float)i / 4.0;
            v.push_back(-1.0); v.push_back(m); v.push_back(0.0);
            v.push_back( 1.0); v.push_back(m); v.push_back(0.0);
        }
        for(int i = 0; i < 16; i++)
        {
            float m = -1.0 + (float)i / 4.0 * fid_xs;
            v.push_back(m); v.push_back(-1.0); v.push_back(0.0);
            v.push_back(m); v.push_back( 1.0); v.push_back(0.0);
        }
        Mesh lines({"v_pos"}, {3}, v);
        p_colour->set_uniform("colour", {0.0,1.0,1.0,1.0});
        lines.draw(*p_colour, GL_LINES);

    }
    else
    {
        t_output.bind(GL_TEXTURE0);
        state.bind_fb_screen();
        glClearColor(1.0, 0.0, 1.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        // Mesh full({"v_pos", "v_tex"}, {3, 2}, 
        //     {   -1, -1, 0,  0,  0,
        //          1, -1, 0,  1,  0,
        //         -1,  1, 0,  0,  1,
        //          1,  1, 0,  1,  1});
        // full.draw(*p_null, GL_TRIANGLE_STRIP);
        m_quad->draw(*p_null);
        // Draw boxes
        if(mv.size())
        {
            std::vector<float> v;
            const float xf = 2.0 / t_buffer[0].buffer_width;
            const float yf = 2.0 / t_buffer[0].buffer_height;
            for(int j = 0; j < mv.size(); j++)
            {
                auto b = mv[j];
                for(int i = 0; i < 4; i++)
                {
                    v.push_back(b.p[i].x * xf - 1.0);
                    v.push_back(b.p[i].y * yf - 1.0);
                    v.push_back(0);
                    v.push_back(b.p[(i + 1) % 4].x * xf - 1.0);
                    v.push_back(b.p[(i + 1) % 4].y * yf - 1.0);
                    v.push_back(0);
                }
            }

            Mesh lines({"v_pos"}, {3}, v);
            p_colour->set_uniform("colour", {0.0,1.0,1.0,1.0});
            lines.draw(*p_colour, GL_LINES);

            v.clear();
            for(int j = 0; j < mv_pre_elim.size(); j++)
            {
                auto b = mv_pre_elim[j];
                if (b.id >= 0)
                    continue;

                for(int i = 0; i < 4; i++)
                {
                    v.push_back(b.p[i].x * xf - 1.0);
                    v.push_back(b.p[i].y * yf - 1.0);
                    v.push_back(0);
                    v.push_back(b.p[(i + 1) % 4].x * xf - 1.0);
                    v.push_back(b.p[(i + 1) % 4].y * yf - 1.0);
                    v.push_back(0);
                }
            }
            lines.set_vertices(v);
            p_colour->set_uniform("colour", {1.0,1.0,0.0,1.0});
            lines.draw(*p_colour, GL_LINES);

        }
    }
}

void Detector::extract(Texture &buf)
{
    // Decode of test image takes ~2.2ms, all targets decoded. GPU pass take 1.9ms.
    // Limiting GPU pass to just a single target gets time down to 1.6ms
    //
    // Time taken using cv::warpPerspective vs gles. This depends on input data
    // size and number of fiducials much more with the cv version
    //      
    //      rpi3                    rpi0
    //      cv      gles            cv      gles
    //  1   120     1600            500     3300
    //  2   220                     1000
    //  3   320                     1500
    //  4   350                     2000
    //  5                           2500
    //  6
    //  7                           
    //  8                           4000
    //..
    //  25  1600    1800            7000    3800
    //
    // In other words, the fixed overhead of the gl drawcall makes it not worth it on the 
    // RPI3. 
    if(mv.size())
        if (mv.size() >= gl_extract_thresh)
        {
            // Warp all the detected boxes into a regular grid, up to a limit of
            // 32 markers.
            const float xf = 1.0 / buf.buffer_width;
            const float yf = (y_flip ? -1.0 : 1.0) / buf.buffer_height;
            const float cnr = xf * 8;


            std::vector<float> v;
            int limit = mv.size() > maxlimit ? maxlimit : mv.size();
            for(int i = 0; i < limit; i++)
            {
                auto &b = mv[i];
                float x[2], y[2], s[4], t[4];
                x[0] = -1 + 0.25 * (i & 7);
                x[1] = -1 + 0.25 * ((i & 7) + 1);
                y[0] = -1 + 0.5 * (i >> 3);
                y[1] = -1 + 0.5 * ((i >> 3) + 1);
                for(int j = 0; j < 4; j++)
                {
                    s[j] = b.p[j].x * xf + params.xo;
                    t[j] = b.p[j].y * yf + params.yo;
                }
                v.push_back(x[0]); v.push_back(y[0]), v.push_back(0); v.push_back(s[1]); v.push_back(t[1]);
                v.push_back(x[0]); v.push_back(y[1]), v.push_back(0); v.push_back(s[0]); v.push_back(t[0]);
                v.push_back(x[1]); v.push_back(y[0]), v.push_back(0); v.push_back(s[2]); v.push_back(t[2]);
                v.push_back(x[1]); v.push_back(y[0]), v.push_back(0); v.push_back(s[2]); v.push_back(t[2]);
                v.push_back(x[0]); v.push_back(y[1]), v.push_back(0); v.push_back(s[0]); v.push_back(t[0]);
                v.push_back(x[1]); v.push_back(y[1]), v.push_back(0); v.push_back(s[3]); v.push_back(t[3]);
            }
            m_fid.set_vertices(v);
            buf.interp(true);
            render_pass(state.fbo, m_fid, *p_null, buf, t_fid, GL_TRIANGLES);
            glFinish();
            if (grab) {glFinish(); t_fid.dump_file("p4.bin");}

        }
        else
        {

            // Warp all the detected boxes into a regular grid, up to a limit of
            // 32 markers.
            const float xf = 1.0 / buf.buffer_width;
            const float yf = (y_flip ? -1.0 : 1.0) / buf.buffer_height;
            const float cnr = xf * 8;


            std::vector<float> v;
            int limit = mv.size() > maxlimit ? maxlimit : mv.size();

            //buf.user_lock();
            //t_fid.user_lock();
            cv::Mat cvbuf(buf.buffer_height, buf.buffer_width, CV_8UC4, buf.user_buffer);
            cv::Mat cvfid(t_fid.buffer_height, t_fid.buffer_width, CV_8UC4, t_fid.user_buffer);
            for(int i = 0; i < limit; i++)
            {
                auto &b = mv[i];

                cv::Point2f in[4], out[4];
                for(int j = 0; j < 4; j++)
                {
                    in[j].x = b.p[j].x;// * xf + params.xo;
                    in[j].y = b.p[j].y;// * yf + params.yo;
                }
                float x[2], y[2];
                x[0] = (-1 + 0.25 * (i & 7))       * t_fid.buffer_width * 0.5 + t_fid.buffer_width * 0.5;
                x[1] = (-1 + 0.25 * ((i & 7) + 1)) * t_fid.buffer_width * 0.5 + t_fid.buffer_width * 0.5;
                y[0] = (-1 + 0.5 * (i >> 3))       * t_fid.buffer_height * 0.5 + t_fid.buffer_height * 0.5;
                y[1] = (-1 + 0.5 * ((i >> 3) + 1)) * t_fid.buffer_height * 0.5 + t_fid.buffer_height * 0.5;


                out[0].x = 0;   out[0].y = 0;
                out[1].x = 15;  out[1].y = 0;
                out[2].x = 15;  out[2].y = 15;
                out[3].x = 0;   out[3].y = 15;
                cv::Mat m = cv::getPerspectiveTransform(in, out);
                cv::Mat fidroi(cvfid, cv::Rect(x[0], y[0], 16, 16));
                //printf("% 6.2f % 6.2f\n", x[0], y[0]);
                cv::warpPerspective(cvbuf, fidroi, m, fidroi.size(), cv::INTER_LINEAR);

            }
            //buf.user_unlock();
            //t_fid.user_unlock();
            t_fid.invalidate();

        }
}


void Detector::extract2(Texture &buf)
{
    aux_timer.set_time(false);
    int tm[3] = {0};
    if(mv.size())
    {

        //endwin();
        // Warp all the detected boxes into a regular grid, up to a limit of
        // 32 markers.
        // const float xf = 1.0 / buf.buffer_width;
        // const float yf = (y_flip ? -1.0 : 1.0) / buf.buffer_height;
        const float xf = 1.0 / buf.buffer_width;
        const float yf = (y_flip ? -1.0 : 1.0) / buf.buffer_height;


        std::vector<float> t(mv.size() * 9);
        int limit = mv.size() > maxlimit ? maxlimit : mv.size();

        for(int i = 0; i < limit; i++)
        {
            auto &b = mv[i];

            cv::Point2f in[4], out[4];
            for(int j = 0; j < 4; j++)
            {
                in[j].x = b.p[j].x * xf;// + params.xo;
                in[j].y = b.p[j].y * yf;// + params.yo;
            }

            float xp = 0.0;
            out[1].x = -xp;     out[1].y = -xp;
            out[2].x = 16 + xp; out[2].y = -xp;
            out[3].x = 16 + xp; out[3].y = 16 + xp;
            out[0].x = -xp;     out[0].y = 16 + xp;
            cv::Mat m = cv::getPerspectiveTransform(in, out);
            tm[0] += aux_timer.diff_time();
            cv::invert(m, m);
            t[i * 9 + 0] = (float)m.at<double>(0, 0);
            t[i * 9 + 1] = (float)m.at<double>(0, 1);
            t[i * 9 + 2] = (float)m.at<double>(0, 2);
            t[i * 9 + 3] = (float)m.at<double>(1, 0);
            t[i * 9 + 4] = (float)m.at<double>(1, 1);
            t[i * 9 + 5] = (float)m.at<double>(1, 2);
            t[i * 9 + 6] = (float)m.at<double>(2, 0);
            t[i * 9 + 7] = (float)m.at<double>(2, 1);
            t[i * 9 + 8] = (float)m.at<double>(2, 2);
            tm[1] += aux_timer.diff_time();
        }
        p_qpu_warp->execute(buf, t_fid, t, mv.size());
        tm[2] = aux_timer.diff_time();
        //t_fid.dump();
        //exit(1);

        //buf.user_unlock();
        //t_fid.user_unlock();
        //t_fid.invalidate();
        if (grab) 
        {   
            t_fid.dump_file("p4.bin");
            for(int i = 0; i < limit; i++)
                printf("%2d, %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f\n", i, mv[i].p[0].x, mv[i].p[0].y, mv[i].p[1].x, mv[i].p[1].y, mv[i].p[2].x, mv[i].p[2].y, mv[i].p[3].x, mv[i].p[3].y);
        }

    }
    //printf("%2d %4d %4d %4d ", mv.size(), tm[0], tm[1], tm[2]);
}



void Detector::decode()
{
    // Texture t_fid has mv.size() candidates in 32x32 sized regions
    int limit = mv.size() > maxlimit ? maxlimit : mv.size();
    if (limit)
    {
        uint32_t *pix = (uint32_t *)t_fid.user_lock();

        for(int idx = 0; idx < limit; idx++)
        {
            int samples[256] = {0};
            int ss[64];
            int value[256];
            int min = 255, max = 0;
            for(int y = 0; y < 8; y++)
            {
                for(int x = 0; x < 8; x++)
                {
                    // The texture is 128x64. Each fiducial occupies 16x16, so each bit
                    // is a 2x2 region
                    //int offset = ((idx & 7) << 5) | ((idx & 0x18) << 10) | (x << 2) | (y << 10);
                    int offset = ((idx & 7) << 4) | ((idx & 0x18) << 8) | (x << 1) | (y << 8);
                    uint32_t d0 = (*(pix + offset + 0x00)) & 0xff;
                    uint32_t d1 = (*(pix + offset + 0x01)) & 0xff;
                    uint32_t d2 = (*(pix + offset + 0x80)) & 0xff;
                    uint32_t d3 = (*(pix + offset + 0x81)) & 0xff;
                    int so = (y << 5) | (x << 2);
                    samples[so       ] = d0;
                    samples[so | 0x01] = d1;
                    samples[so | 0x10] = d2;
                    samples[so | 0x11] = d3;
                    ss[(y << 3) | x] = d0 + d1 + d2 + d3;
                    if (d0 < min) min = d0; if (d0 > max) max = d0;
                    if (d1 < min) min = d1; if (d1 > max) max = d1;
                    if (d2 < min) min = d2; if (d2 > max) max = d2;
                    if (d3 < min) min = d3; if (d3 > max) max = d3;

                    if (tui)
                    {
                        move(64 - 2 * (y + (idx & 0x18)), 100 + 4 * x + 32 * (idx & 7));
                        if (d0 > 0x50) attron(A_BOLD); printw("%2x", d0);  attroff(A_BOLD);
                        if (d1 > 0x50) attron(A_BOLD); printw("%2x", d1);  attroff(A_BOLD);
                        move(64 - 2 * (y + (idx & 0x18)) - 1, 100 + 4 * x + 32 * (idx & 7));
                        if (d2 > 0x50) attron(A_BOLD); printw("%2x", d2);  attroff(A_BOLD);
                        if (d3 > 0x50) attron(A_BOLD); printw("%2x", d3);  attroff(A_BOLD);
                    }
                }
            }
            // Threshold half way between min and max
            int thr = 0.5 * (max - min) + min;
            for(int i = 0; i < 64; i++)
                ss[i] = ss[i] > thr * 4;


            if(tui)
            {
                for(int y = 0; y < 8; y++)
                    for(int x = 0; x < 8; x++)
                    {
                        move(30 - (y + (idx & 0x18)), 100 + 2 * x + 16 * (idx & 7));
                        printw("%c", ss[(y << 3) | x] ? 'X' : '.');
                        //printw("%4x", ss[(y << 3) | x]);
                    }
            }
            // Extract the bits in each of the four orientations
            uint64_t bits[4] = {0};
            for(int y = 1; y < 7; y++)
            {
                for(int x = 1; x < 7; x++)
                {
                    int yf = 7 - y;
                    bits[0] = (bits[0] << 1) | (uint64_t)ss[(yf << 3) | x];
                    bits[1] = (bits[1] << 1) | (uint64_t)ss[(x << 3) | (7 - yf)];
                    bits[2] = (bits[2] << 1) | (uint64_t)ss[((7 - yf) << 3) | (7 - x)];
                    bits[3] = (bits[3] << 1) | (uint64_t)ss[((7 - x) << 3) | yf];
                }
            }
            // Lookup each code until success. If found, put in id field of marker
            uint64_t code = 0;
            for(int i = 0; i < 4; i++)
            {
                auto it = dict->cmap.find(bits[i]);
                if (it != dict->cmap.end())
                {
                    mv[idx].id = (int)it->second;
                    code = bits[i];
                    break;       
                }
            }
            if(tui)mvprintw(67 + idx, 215, "%2x %3d %09lx %09lx %09lx %09lx", 
                thr, mv[idx].id, bits[0], bits[1], bits[2], bits[3]);


        }
        t_fid.user_unlock();
    }
}


void Detector::decode_hamming()
{
    // Texture t_fid has mv.size() candidates in 16x16 sized regions
    int limit = mv.size() > maxlimit ? maxlimit : mv.size();
    if (limit)
    {
        uint32_t *pix = (uint32_t *)t_fid.user_lock();

        for(int idx = 0; idx < limit; idx++)
        {
            int ss[64];
            int value[256];
            int min = 255, max = 0;
            for(int y = 0; y < 8; y++)
            {
                for(int x = 0; x < 8; x++)
                {
                    // The texture is 128x64. Each fiducial occupies 16x16, so each bit
                    // is a 2x2 region.
                    // int offset = ((idx & 7) << 5) | ((idx & 0x18) << 10) | (x << 2) | (y << 10);
                    int offset = ((idx & 7) << 4) | ((idx & 0x18) << 8) | (x << 1) | (y << 8);
                    uint32_t d0 = (*(pix + offset + 0x00)) & 0xff;
                    uint32_t d1 = (*(pix + offset + 0x01)) & 0xff;
                    uint32_t d2 = (*(pix + offset + 0x80)) & 0xff;
                    uint32_t d3 = (*(pix + offset + 0x81)) & 0xff;
                    int so = (y << 5) | (x << 2);
                    ss[(y << 3) | x] = d0 + d1 + d2 + d3;
                    if (d0 < min) min = d0; if (d0 > max) max = d0;
                    if (d1 < min) min = d1; if (d1 > max) max = d1;
                    if (d2 < min) min = d2; if (d2 > max) max = d2;
                    if (d3 < min) min = d3; if (d3 > max) max = d3;

                    if (tui)
                    {
                        move(64 - 2 * (y + (idx & 0x18)), 100 + 4 * x + 32 * (idx & 7));
                        if (d0 > 0x50) attron(A_BOLD); printw("%2x", d0);  attroff(A_BOLD);
                        if (d1 > 0x50) attron(A_BOLD); printw("%2x", d1);  attroff(A_BOLD);
                        move(64 - 2 * (y + (idx & 0x18)) - 1, 100 + 4 * x + 32 * (idx & 7));
                        if (d2 > 0x50) attron(A_BOLD); printw("%2x", d2);  attroff(A_BOLD);
                        if (d3 > 0x50) attron(A_BOLD); printw("%2x", d3);  attroff(A_BOLD);
                    }
                }
            }
            // Threshold half way between min and max
            int thr = 0.5 * (max - min) + min;
            for(int i = 0; i < 64; i++)
                ss[i] = ss[i] > thr * 4;

            // Extract the bits in each of the four orientations
            uint64_t bits[4] = {0};
            for(int y = 1; y < 7; y++)
            {
                for(int x = 1; x < 7; x++)
                {
                    int xf = 7 - x;
                    int yf = 7 - y;
                    bits[0] = (bits[0] << 1) | (uint64_t)ss[(y << 3)   | x];
                    bits[1] = (bits[1] << 1) | (uint64_t)ss[(x << 3)   | yf];
                    bits[2] = (bits[2] << 1) | (uint64_t)ss[(yf << 3)    | xf];
                    bits[3] = (bits[3] << 1) | (uint64_t)ss[(xf << 3)    | y];
                }
            }
            // printf("%010lx %010lx %010lx %010lx\n", bits[0], bits[1], bits[2], bits[3]);
            // exit(1);
            // Find closest in dict
            mv[idx].id = dict->hamming(bits, params.max_hamming, &mv[idx].dist);

            if(tui)mvprintw(67 + idx, 215, "%2x %3x %3d %09lx %09lx %09lx %09lx", 
                thr, mv[idx].id, mv[idx].dist, bits[0], bits[1], bits[2], bits[3]);
            
        }
        t_fid.user_unlock();
    }
}

void Detector::decode_vpu()
{
    // Texture t_fid has mv.size() candidates in 16x16 sized regions
    
    // Load the codebook into the VPU. FIXME!! This doesn't work, the
    // VRF is corrupted between this call and the decode calls
    //p_vpu_functions->execute(VPU_LOAD_CODE, t_codes.bus_address, 0, 0, 0, 0);
    int limit = mv.size() > maxlimit ? maxlimit : mv.size();
    if (limit)
    {
        for(int idx = 0; idx < limit; idx++)
        {

           // Perform code lookup, returning ID with smallest Hamming
           // distance from code that is less than min requirement.
           //
           // This is much faster than using the CPU; more than 5x.
           // ~150us per code, vs 800us-1ms on CPU
            mv[idx].id = p_vpu_functions->execute(VPU_DECODE, 
                            t_fid.bus_address, 
                            t_codes.bus_address, 
                            t_decode.bus_address, 
                            params.max_hamming, idx);
            // The ID has the orientation encoded in the top bits 9:8, 
            // rotate the quadrangle points so order is correct
            // top left, top right, bottom right, bottom left
            if (mv[idx].id != -1)
            {
                int rot = (mv[idx].id >> 8);
                mv[idx].id &= 0xff;
                if (rot == 3) {auto p = mv[idx].p; mv[idx].p = {p[0], p[1], p[2], p[3]};}
                if (rot == 0) {auto p = mv[idx].p; mv[idx].p = {p[1], p[2], p[3], p[0]};}
                if (rot == 1) {auto p = mv[idx].p; mv[idx].p = {p[2], p[3], p[0], p[1]};}
                if (rot == 2) {auto p = mv[idx].p; mv[idx].p = {p[3], p[0], p[1], p[2]};}
            }

            if(tui)mvprintw(67 + idx, 215, "%3x  ", mv[idx].id);
            
        }
    }
}



struct Coord
{
    Coord() : x(0), y(0) {}
    Coord(int _x, int _y) : x(_x), y(_y) {}
    int x;
    int y;
    Coord operator+(const Coord &b) 
    {
        Coord c;
        c.x = this->x + b.x;
        c.y = this->y + b.y;
        return c;
    }
};

Coord rloc(Coord &p, Coord &q, int i)
{
    const int twiddle[9] = {0, 7, 6, 1, -1, 5, 2, 3, 4};
    int dx  = q.x - p.x;
    int dy  = q.y - p.y;
    int idx = twiddle[dy * 3 + dx + 4];
    // 0 1 2    0 7 6
    // 3 4 5 -> 1 . 5
    // 6 7 8    2 3 4
    const Coord rout[8] = {
        {-1, 0},
        {-1, 1},
        { 0, 1},
        { 1, 1},
        { 1, 0},
        { 1,-1},
        { 0,-1},
        {-1,-1}
    };
    return p + rout[(idx + i) % 8];
}

//-----------------------------------------------------------------
// find_borders mostly from opencv code with additions to build a list of the
// contours which have exactly four corners in them

//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.




// initializes 8-element array for fast access to 3x3 neighborhood of a pixel
#define  CV_INIT_3X3_DELTAS( deltas, step, nch )            \
    ((deltas)[0] =  (nch),  (deltas)[1] = -(step) + (nch),  \
     (deltas)[2] = -(step), (deltas)[3] = -(step) - (nch),  \
     (deltas)[4] = -(nch),  (deltas)[5] =  (step) - (nch),  \
     (deltas)[6] =  (step), (deltas)[7] =  (step) + (nch))



int idx(int16_t *pivot, int16_t *current, int step, int pspace)
{
    long diff = current - pivot;
    if (diff == pspace)         return 0;
    if (diff == -step + pspace) return 1;
    if (diff == -step)          return 2;
    if (diff == -step - pspace) return 3;
    if (diff == -pspace)        return 4;
    if (diff == step - pspace)  return 5;
    if (diff == step)           return 6;
    if (diff == step + pspace)  return 7;
    return 0;
}

int Y(long i, int step) {return (int)abs(i / step);}
int X(long i, int step) {return (int)abs(i % step) >> 1;}



//#define DEBUG
//#define DEBUGP

template <class T>
uint32_t find_borders(Texture &buf, MarkerVec &mv, T &t_mask, float scale_factor)
{

    //endwin();
    static int loops = 0;

    const float vrange = 1.0;

    int stride  = buf.buffer_width;
    int width   = buf.width * scale_factor;
    int height  = buf.height * scale_factor;

    int shift = 9;
    if (stride == 64)  shift = 7;
    if (stride == 128)  shift = 8;
    if (stride == 256)  shift = 9;
    if (stride == 512)  shift = 10;
    if (stride == 1024) shift = 11;

    // Make buffer available to CPU side
    int16_t     *ptr = (int16_t *)buf.user_buffer;
    //buf.user_lock();
#ifdef BMASK
    t_mask.invalidate();
    char        *mask = (char *)t_mask.user_buffer;
#endif

    //endwin();    
    //dumphex(0, mask, 2048);
    //exit(1);
    // for(int x = 0; x < g.cols; g.at<int16_t>(0, x++) = 0);
    // for(int y = 0; y < g.rows; g.at<int16_t>(y, 0) = 0, g.at<int16_t>(y++, g.cols - 1) = 0);
    
    
    //int16_t     *ptr = (int16_t *)g.ptr(0);
    int         pspace = 2;
    int16_t     *org = ptr;
    int         step = stride * pspace;
    #define MAX_SIZE 16
    int         deltas[MAX_SIZE];

    // Make the borders black, this needs to two pixels wide, since
    // each renderpass of the QPU produces pixels that depend on their
    // neighbours with no protection from reading from outside image bounds
    for(int x = 0; x < width; x++) 
    {
        ptr[x * pspace] = 0;
        ptr[x * pspace + step] = 0;
        ptr[(height - 2) * step + x * pspace] = 0;
        ptr[(height - 1) * step + x * pspace] = 0;
    }
    for(int y = 0; y < height; y++) 
    {
        ptr[y * step] = 0;
        ptr[y * step + 1] = 0;
        ptr[y * step + (width - 2) * pspace] = 0;
        ptr[y * step + (width - 1) * pspace] = 0;
    }

#ifdef DEBUGP
    char *dp = (char*)ptr;
    char fname[32]; sprintf(fname,"dump%02d.bin", loops++);
    FILE *fp = fopen(fname, "w");
    fwrite(dp, 1, 2097152, fp);
    fclose(fp);
    if (loops == 5)
    {
        endwin();exit(1);
    }
#endif
    // deltas holds quick access table of offsets for each location anticlockwise from location to right of current.
    // e.g  3 2 1
    //      4 . 0
    //      5 6 7
    CV_INIT_3X3_DELTAS(deltas, step, pspace);
    memcpy(deltas + 8, deltas, 8 * sizeof( deltas[0]));
    
    int nbd, lnbd;
    nbd = 1;
    
    int16_t     p, lp;
    int prev_s = -1, s, s_end;

    // ptr points to start of line, i0 points to current pixel
    int16_t *i0, *i1 = 0, *i2 = 0, *i3, *i4 = 0;

    const int climit = 16;
    int16_t *cpoints[16];
    // std::vector<int16_t *> cpoints;
    // cpoints.reserve(climit);

    int candidates = 0;
    int cthresh = 10;
    bool do_mask = scale_factor == 1.0;
    for(int y = 2; y < height - 2; y++)
    {
        // Start at row 1
        ptr += step;

        lnbd    = 0;
        lp      = 0;


        for(int x = 2; x < width - 2; x++)
        {
#ifdef BMASK
            // Skip empty blocks
            //if (do_mask && ((x & 0xf) == 0) && !mask[(y >> 4) * 64 + (x >> 4)])
            if (((x & 0xf) == 0) && !mask[(y >> 4) * 64 + (x >> 4)])
            {
                x |= 0xf;
                continue;
            }
#endif

            p = ptr[x << 1];
      
            // 1a If fij = 1 and fi, j-1 = 0, then decide that the pixel (i, j) is the border following
            // starting point of an outer border, increment NBD, and (i2, j2) = (i, j - 1)
            if ((p == 255) && !lp && (lnbd <= 0))
            {
                int corners = 0;
                //cpoints.clear();
                nbd++;
                i2 = ptr + (x - 1) * pspace;
                i0 = ptr + x * pspace;

                // Get index into delta table for starting point. Delta table is anticlockwise
                // with increasing index
                // 3.1 Starting from (i2, j2), look around clockwise the pixels in the neighborhood of (i, j) and find 
                // a nonzero pixel. Let (i1, j1) be the first found nonzero pixel. If no nonzero pixel is found, 
                // assign -NBD to fij and go to (4)
                //
                // NOTE: we know that *i2 is zero, since that is the condition that gets here, so, contrary
                // to the algo description, we only need to start from the next element after i2 in clockwise
                // order.
                //
                // At this point i2 is always i0 - 1, which is index 4
                //int t_idx = 4;//idx(i0, i2, step, pspace);

                s_end = s = 4;
                do
                {
                    s = (s - 1) & 7;
                    i1 = i0 + deltas[s];
                }
                while(*i1 == 0 && s != s_end);

                int loops = 0;
                if (s == s_end)
                {
                    *i0 = -nbd;
                    if (i0[1])
                    {
                        //if (corners < climit) 
                        cpoints[corners % 16] = i0;
                        corners++;
                    }
                }
                else
                {
                    // 3.2 (i2, j2,) = (i1, j1,) and (i3, j3) = (i, j).
                    i3 = i0;
                    prev_s = s ^ 4;

                    //printf("before loop t_idx:%2d\n", t_idx);
                    //while(loops++ < 1000)
                    while(1)
                    {
                        // if ((i3 < org + 2 * step) || (i3 > org + step * (height - 2)))
                        //     // Don't follow edge off buffer
                        //     break;

                        // 3.3 Starting from the next element of the pixel (i2, j2) in the counterclock-wise order,
                        // examine counterclockwise the pixels in the neighborhood of the current pixel (i3, j3) to
                        // find a nonzero pixel and let the first one be (i4, j4).
                        //
                        // NOTE: 

                        s_end = s;
                        s = std::min(s, MAX_SIZE - 1);
                        while(s < MAX_SIZE - 1)
                        {
                            i4 = i3 + deltas[++s];
                            if (*i4)
                                break;
                        }
                        s &= 7;
                        if ((unsigned)(s - 1) < (unsigned)s_end)
                        {
                            *i3 = -nbd;
                            loops++;
                            if (i3[1])
                            {
                                //if (corners < climit)
                                cpoints[corners % 16] = i3;
                                corners++;
                            }
                        }
                        else if (*i3 == 255)
                        {
                            *i3 = nbd;
                            loops++;
                            if (i3[1])
                            {
                                //if (corners < climit)
                                cpoints[corners % 16] = i3;
                                corners++;
                            }
                        }

#ifdef DEBUGP
                        printf("%3d %3d %3d %3d %4x %2x%2x%2x%2x%2x%2x%2x%2x%2x %08x %d\n", x, y, X(i3 - org, step), Y(i3 - org, step), (*i3)&0xffff, 
                            *(i3+1-step-pspace) &0xff,
                            *(i3+1-step)&0xff,
                            *(i3+1-step+pspace)&0xff,
                            *(i3+1-pspace)&0xff,
                            *(i3+1)&0xff,
                            *(i3+1+pspace)&0xff,
                            *(i3+1+step-pspace)&0xff,
                            *(i3+1+step)&0xff,
                            *(i3+1+step+pspace)&0xff,
                            *(uint32_t*)i3,
                            corners);
#endif
                        prev_s = s;
                        if ((i4 == i0) && (i3 == i1))
                            break;
                        
                        i3 = i4;
                        s = (s + 4) & 7;
                    }
                }
                if ((corners >= 4) && (loops >= 20))
                {
                    int c = check_candidate(cpoints, corners > climit ? climit : corners, mv, org, step);

                    candidates += c;
                }
                // 4 If fij != 1, then LNBD = |fij| and resume the raster scan from the pixel (i, j + 1). The algorithm
                // terminates when the scan reachesthe lower right corner of the picture.
            }
            if (p)
                lnbd = p;
            lp = p;
        }
    }
    
#ifdef DEBUG
    endwin();
    fp = fopen("dumpout.bin", "w");
    fwrite(dp, 1, 2097152, fp);
    fclose(fp);
    exit(1);
#endif
    // Release buffer to GPU and flush cache
    //  Results for map4x4_640x480 over 2000 tests
    //  Method      Speed       Faults
    //  none        14.999      3.5
    //  clean       15.074      0.0
    //  invalidate  15.105      0.0
    //  cl_inv      15.078      0.0
    //  unlock      16.676      0.0
    buf.clean();
    //buf.user_unlock();
    //buf.invalidate();
    //buf.clean_invalidate();
    //buf.user_unlock();
    // No flush for mask, since we did no writes


    return candidates;
}

//#define DEBUG
int check_candidate(int16_t **cpoints, int corners, MarkerVec &mv, int16_t *org, int step)
{

    auto n = corners;
    const int k = 4;
    std::vector<cv::Point2f> cp(n);
    for(int i = 0; i < n; i++)
    {
        cp[i] = {(float)X(cpoints[i] - org, step), (float)Y(cpoints[i] - org, step)};
#ifdef DEBUG
        printf("%3d x:%8.2f y:%8.2f\n", i, cp[i].x,cp[i].y);
#endif
    }


    // For true candidates, we know that there will be a few (2,3,4) corner pixels clustered
    // close together before a gap. There may also be wrap around.
    //
    // Rather than full kmeans, do very simple:
    // Assign first cpixel to first corner, keep adding successive cpixels to mean
    // until dist from mean exceeds gap threshold of 4 pixels
    
    cv::Point2f     p[4];
    p[0]            = cp[0];
    int ci          = 0;
    int num         = 1;
    int clusters    = 1;
    const float md  = 4.0;

    //FIXME adding for diganostics
    std::vector<cv::Point2f> cpint;
    for(int i = 1; i < n; i++)
    {
        cpint.push_back(cp[i]);
        float d = cv::norm(p[ci] - cp[i]);
        if (d < md)
        {
            p[ci] = (p[ci] * num + cp[i]) / ++num;
        }
        else
        {
            ci = (ci + 1) % 4;
            if (ci == 0)
            //if ((ci == 0) && (cv::norm(p[0] - cp[i]) < md))
            {
                // Wrapped
                p[0] = (p[0] * num + cp[i]) / ++num;
            }
            else
            {
                clusters++;
                num = 1;
                p[ci] = cp[i];
            }
        }
    }
#ifdef DEBUG

    printf("(%8.2f,%8.2f) (%8.2f,%8.2f) (%8.2f,%8.2f) (%8.2f,%8.2f)\n",
        p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);
#endif
    Marker mk;
    if (clusters == 4)
    {
        for(int i = 0; i < 4; i++)
        {
            mk.p[i] = {p[i].x, p[i].y};
        }
        mk.cp = cpint;
        mv.push_back(mk);
    }

    return 1;

}


float orientation(const Point2f &a, const Point2f &b, const Point2f &c)
{
    return (a.x * b.y - a.y * b.x) + (b.x * c.y - b.y * c.x) + (c.x * a.y - c.y * a.x);
}

void sort_points_clockwise(Marker &m)
{
    Point2f &a = m.p[0];
    Point2f &b = m.p[1];
    Point2f &c = m.p[2];
    Point2f &d = m.p[3];
    if (orientation(a, b, c) < 0.0)
    {
        if (orientation(a, c, d) < 0.0)
            return;
        else if (orientation(a, b, d) < 0.0)
            std::swap(d, c);
        else
            std::swap(a, d);
    }
    else if (orientation(a, c, d) < 0.0)
    {
        if (orientation(a, b, d) < 0.0)
            std::swap(b, c);
        else
            std::swap(a, b);
    }
    else
        std::swap(a, c);
}



/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/
//#include "precomp.hpp"

#include <opencv2/highgui/highgui_c.h>
namespace cv{
static const uchar*
adjustRect( const uchar* src, size_t src_step, int pix_size,
           Size src_size, Size win_size,
           Point ip, Rect* pRect )
{
    Rect rect;

    if( ip.x >= 0 )
    {
        src += ip.x*pix_size;
        rect.x = 0;
    }
    else
    {
        rect.x = -ip.x;
        if( rect.x > win_size.width )
            rect.x = win_size.width;
    }

    if( ip.x < src_size.width - win_size.width )
        rect.width = win_size.width;
    else
    {
        rect.width = src_size.width - ip.x - 1;
        if( rect.width < 0 )
        {
            src += rect.width*pix_size;
            rect.width = 0;
        }
        CV_Assert( rect.width <= win_size.width );
    }

    if( ip.y >= 0 )
    {
        src += ip.y * src_step;
        rect.y = 0;
    }
    else
        rect.y = -ip.y;

    if( ip.y < src_size.height - win_size.height )
        rect.height = win_size.height;
    else
    {
        rect.height = src_size.height - ip.y - 1;
        if( rect.height < 0 )
        {
            src += rect.height*src_step;
            rect.height = 0;
        }
    }

    *pRect = rect;
    return src - rect.x*pix_size;
}


enum { SUBPIX_SHIFT=16 };

struct scale_fixpt
{
    int operator()(float a) const { return cvRound(a*(1 << SUBPIX_SHIFT)); }
};

struct cast_8u
{
    uchar operator()(int a) const { return (uchar)((a + (1 << (SUBPIX_SHIFT-1))) >> SUBPIX_SHIFT); }
};

struct cast_flt_8u
{
    uchar operator()(float a) const { return (uchar)cvRound(a); }
};

template<typename _Tp>
struct nop
{
    _Tp operator()(_Tp a) const { return a; }
};

template<typename _Tp, typename _DTp, typename _WTp, class ScaleOp, class CastOp>
void getRectSubPix_Cn_(const _Tp* src, size_t src_step, Size src_size,
                       _DTp* dst, size_t dst_step, Size win_size, Point2f center, int cn )
{
    ScaleOp scale_op;
    CastOp cast_op;
    Point ip;
    _WTp a11, a12, a21, a22, b1, b2;
    float a, b;
    int i, j, c;

    center.x -= (win_size.width-1)*0.5f;
    center.y -= (win_size.height-1)*0.5f;

    ip.x = cvFloor( center.x );
    ip.y = cvFloor( center.y );

    a = center.x - ip.x;
    b = center.y - ip.y;
    a11 = scale_op((1.f-a)*(1.f-b));
    a12 = scale_op(a*(1.f-b));
    a21 = scale_op((1.f-a)*b);
    a22 = scale_op(a*b);
    b1 = scale_op(1.f - b);
    b2 = scale_op(b);

    src_step /= sizeof(src[0]);
    dst_step /= sizeof(dst[0]);

    if( 0 <= ip.x && ip.x < src_size.width - win_size.width &&
       0 <= ip.y && ip.y < src_size.height - win_size.height)
    {
        // extracted rectangle is totally inside the image
        src += ip.y * src_step + ip.x*cn;
        win_size.width *= cn;

        for( i = 0; i < win_size.height; i++, src += src_step, dst += dst_step )
        {
            for( j = 0; j <= win_size.width - 2; j += 2 )
            {
                _WTp s0 = src[j]*a11 + src[j+cn]*a12 + src[j+src_step]*a21 + src[j+src_step+cn]*a22;
                _WTp s1 = src[j+1]*a11 + src[j+cn+1]*a12 + src[j+src_step+1]*a21 + src[j+src_step+cn+1]*a22;
                dst[j] = cast_op(s0);
                dst[j+1] = cast_op(s1);
            }

            for( ; j < win_size.width; j++ )
            {
                _WTp s0 = src[j]*a11 + src[j+cn]*a12 + src[j+src_step]*a21 + src[j+src_step+cn]*a22;
                dst[j] = cast_op(s0);
            }
        }
    }
    else
    {
        Rect r;
        src = (const _Tp*)adjustRect( (const uchar*)src, src_step*sizeof(*src),
                                     sizeof(*src)*cn, src_size, win_size, ip, &r);

        for( i = 0; i < win_size.height; i++, dst += dst_step )
        {
            const _Tp *src2 = src + src_step;
            _WTp s0;

            if( i < r.y || i >= r.height )
                src2 -= src_step;

            for( c = 0; c < cn; c++ )
            {
                s0 = src[r.x*cn + c]*b1 + src2[r.x*cn + c]*b2;
                for( j = 0; j < r.x; j++ )
                    dst[j*cn + c] = cast_op(s0);
                s0 = src[r.width*cn + c]*b1 + src2[r.width*cn + c]*b2;
                for( j = r.width; j < win_size.width; j++ )
                    dst[j*cn + c] = cast_op(s0);
            }

            for( j = r.x*cn; j < r.width*cn; j++ )
            {
                s0 = src[j]*a11 + src[j+cn]*a12 + src2[j]*a21 + src2[j+cn]*a22;
                dst[j] = cast_op(s0);
            }

            if( i < r.height )
                src = src2;
        }
    }
}


static void getRectSubPix_8u32f
( const uchar* src, size_t src_step, Size src_size,
 float* dst, size_t dst_step, Size win_size, Point2f center0, int cn )
{
    Point2f center = center0;
    Point ip;

    center.x -= (win_size.width-1)*0.5f;
    center.y -= (win_size.height-1)*0.5f;

    ip.x = cvFloor( center.x );
    ip.y = cvFloor( center.y );

    if( cn == 1 &&
       0 <= ip.x && ip.x + win_size.width < src_size.width &&
       0 <= ip.y && ip.y + win_size.height < src_size.height &&
       win_size.width > 0 && win_size.height > 0 )
    {
        float a = center.x - ip.x;
        float b = center.y - ip.y;
        a = MAX(a,0.0001f);
        float a12 = a*(1.f-b);
        float a22 = a*b;
        float b1 = 1.f - b;
        float b2 = b;
        double s = (1. - a)/a;

        src_step /= sizeof(src[0]);
        dst_step /= sizeof(dst[0]);

        // extracted rectangle is totally inside the image
        src += ip.y * src_step + ip.x;

        for( ; win_size.height--; src += src_step, dst += dst_step )
        {
            float prev = (1 - a)*(b1*src[0] + b2*src[src_step]);
            for( int j = 0; j < win_size.width; j++ )
            {
                float t = a12*src[j+1] + a22*src[j+1+src_step];
                dst[j] = prev + t;
                prev = (float)(t*s);
            }
        }
    }
    else
    {
        getRectSubPix_Cn_<uchar, float, float, nop<float>, nop<float> >
        (src, src_step, src_size, dst, dst_step, win_size, center0, cn );
    }
}
void localgetRectSubPix( InputArray _image, Size patchSize, Point2f center,
                       OutputArray _patch, int patchType )
{
    //CV_INSTRUMENT_REGION();

    Mat image = _image.getMat();
    int depth = image.depth(), cn = image.channels();
    int ddepth = patchType < 0 ? depth : CV_MAT_DEPTH(patchType);

    //CV_Assert( cn == 1 || cn == 3 );

    _patch.create(patchSize, CV_MAKETYPE(ddepth, cn));
    Mat patch = _patch.getMat();


    if( depth == CV_8U && ddepth == CV_8U )
        getRectSubPix_Cn_<uchar, uchar, int, scale_fixpt, cast_8u>
        (image.ptr(), image.step, image.size(), patch.ptr(), patch.step, patch.size(), center, cn);
    else if( depth == CV_8U && ddepth == CV_32F )
        getRectSubPix_8u32f
        (image.ptr(), image.step, image.size(), patch.ptr<float>(), patch.step, patch.size(), center, cn);
    else if( depth == CV_32F && ddepth == CV_32F )
        getRectSubPix_Cn_<float, float, float, nop<float>, nop<float> >
        (image.ptr<float>(), image.step, image.size(), patch.ptr<float>(), patch.step, patch.size(), center, cn);
    else
        CV_Error( CV_StsUnsupportedFormat, "Unsupported combination of input and output formats");
}

void localcornerSubPix( InputArray _image, InputOutputArray _corners,
                       Size win, Size zeroZone, TermCriteria criteria )
{
    // Hack to work with four channel data and just extract the single channel of
    // the relevant regions. 
    //CV_INSTRUMENT_REGION();

    const int MAX_ITERS = 100;
    int win_w = win.width * 2 + 1, win_h = win.height * 2 + 1;
    int i, j, k;
    int max_iters = (criteria.type & CV_TERMCRIT_ITER) ? MIN(MAX(criteria.maxCount, 1), MAX_ITERS) : MAX_ITERS;
    double eps = (criteria.type & CV_TERMCRIT_EPS) ? MAX(criteria.epsilon, 0.) : 0;
    eps *= eps; // use square of error in comparison operations

    cv::Mat src = _image.getMat(), cornersmat = _corners.getMat();
    int count = cornersmat.checkVector(2, CV_32F);
    CV_Assert( count >= 0 );
    Point2f* corners = cornersmat.ptr<Point2f>();

    if( count == 0 )
        return;

    CV_Assert( win.width > 0 && win.height > 0 );
    CV_Assert( src.cols >= win.width*2 + 5 && src.rows >= win.height*2 + 5 );
    //CV_Assert( src.channels() == 1 );

    Mat maskm(win_h, win_w, CV_32F), subpix_buf(win_h+2, win_w+2, CV_32F), sp_inter_buf(win_h+2, win_w+2, CV_32FC4);
    float* mask = maskm.ptr<float>();

    for( i = 0; i < win_h; i++ )
    {
        float y = (float)(i - win.height)/win.height;
        float vy = std::exp(-y*y);
        for( j = 0; j < win_w; j++ )
        {
            float x = (float)(j - win.width)/win.width;
            mask[i * win_w + j] = (float)(vy*std::exp(-x*x));
        }
    }

    // make zero_zone
    if( zeroZone.width >= 0 && zeroZone.height >= 0 &&
        zeroZone.width * 2 + 1 < win_w && zeroZone.height * 2 + 1 < win_h )
    {
        for( i = win.height - zeroZone.height; i <= win.height + zeroZone.height; i++ )
        {
            for( j = win.width - zeroZone.width; j <= win.width + zeroZone.width; j++ )
            {
                mask[i * win_w + j] = 0;
            }
        }
    }

    // do optimization loop for all the points
    for( int pt_i = 0; pt_i < count; pt_i++ )
    {
        Point2f cT = corners[pt_i], cI = cT;
        int iter = 0;
        double err = 0;

        do
        {
            Point2f cI2;
            double a = 0, b = 0, c = 0, bb1 = 0, bb2 = 0;

            //getRectSubPix(src, Size(win_w+2, win_h+2), cI, subpix_buf, subpix_buf.type());
            localgetRectSubPix(src, Size(win_w+2, win_h+2), cI, sp_inter_buf, sp_inter_buf.type());
            extractChannel(sp_inter_buf, subpix_buf, 0);
            const float* subpix = &subpix_buf.at<float>(1,1);

            // process gradient
            for( i = 0, k = 0; i < win_h; i++, subpix += win_w + 2 )
            {
                double py = i - win.height;

                for( j = 0; j < win_w; j++, k++ )
                {
                    double m = mask[k];
                    double tgx = subpix[j+1] - subpix[j-1];
                    double tgy = subpix[j+win_w+2] - subpix[j-win_w-2];
                    double gxx = tgx * tgx * m;
                    double gxy = tgx * tgy * m;
                    double gyy = tgy * tgy * m;
                    double px = j - win.width;

                    a += gxx;
                    b += gxy;
                    c += gyy;

                    bb1 += gxx * px + gxy * py;
                    bb2 += gxy * px + gyy * py;
                }
            }

            double det=a*c-b*b;
            if( fabs( det ) <= DBL_EPSILON*DBL_EPSILON )
                break;

            // 2x2 matrix inversion
            double scale=1.0/det;
            cI2.x = (float)(cI.x + c*scale*bb1 - b*scale*bb2);
            cI2.y = (float)(cI.y - b*scale*bb1 + a*scale*bb2);
            err = (cI2.x - cI.x) * (cI2.x - cI.x) + (cI2.y - cI.y) * (cI2.y - cI.y);
            cI = cI2;
            if( cI.x < 0 || cI.x >= src.cols || cI.y < 0 || cI.y >= src.rows )
                break;
        }
        while( ++iter < max_iters && err > eps );

        // if new point is too far from initial, it means poor convergence.
        // leave initial point as the result
        if( fabs( cI.x - cT.x ) > win.width || fabs( cI.y - cT.y ) > win.height )
            cI = cT;

        corners[pt_i] = cI;
    }
}
}
