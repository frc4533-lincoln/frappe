

#if 1
/*
 * File:   buffer_demo.c
 * Author: Tasanakorn
 *
 * Created on May 22, 2013, 1:52 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "bcm_host.h"
#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
// #include <cairo/cairo.h>

#include "glhelpers.hpp"
#include "detector.hpp"
// #include "tui.hpp"

Timer master;

// #include "qpu_program.h"
// #include "qpu_info.h"

#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

#define VIDEO_FPS 30
#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480

#define CHECK_STATUS(status, msg)                                    \
    if (status != MMAL_SUCCESS)                                      \
    {                                                                \
        fprintf(stderr, msg " %s\n", mmal_status_to_string(status)); \
        exit(1);                                                     \
    }

typedef struct
{
    int width;
    int height;
    MMAL_COMPONENT_T    *camera;
    MMAL_COMPONENT_T    *encoder;
    MMAL_COMPONENT_T    *preview;
    MMAL_PORT_T         *camera_video_port;
    MMAL_PORT_T         *encoder_input_port;
    MMAL_POOL_T         *encoder_input_pool;
    MMAL_PORT_T         *encoder_output_port;
    MMAL_POOL_T         *encoder_output_pool;
    int times[16];
    float fps;
    FILE *fp;
} PORT_USERDATA;

// Catch signal and terminate cleanly
static volatile sig_atomic_t keep_running = 1;
static void ctrlc_handler(int s)
{
    (void)s;
    keep_running = 0;
}

const char *vs_camera = R"glsl(
#version 100
precision mediump float;

attribute vec4 v_pos;
attribute vec2 v_tex;
uniform float hpix;
uniform float vpix;
varying highp vec2 tex_coord;
void main()
{
    gl_Position = v_pos;
    tex_coord = v_tex;
}
)glsl";

const char *fs_camera = R"glsl(
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
static void camera_video_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    //fprintf(stderr, "INFO:%s %8x\n", __func__, buffer);
    static int frame_count = 0;
    static Timer t;
    int t0 = t.elapsed_time();
    t.set_time();
    PORT_USERDATA *ctx = (PORT_USERDATA *)port->userdata;

    if (buffer->cmd)
    {
        //printf("%s buffer %8x event %d\n", __func__, buffer, buffer->cmd);
        mmal_buffer_header_release(buffer);
    }
    else
    {
        MMAL_BUFFER_HEADER_T *new_buffer;

        frame_count++;

        MMAL_STATUS_T status;
        if (1)
        {
            mmal_buffer_header_release(buffer);
        }else{
            status = mmal_port_send_buffer(ctx->encoder_input_port, buffer);
            CHECK_STATUS(status, "send buffer failed");
            printf("frame sent to encoder\n");
        }
        if (port->is_enabled)
        {

            MMAL_POOL_T  *pool = ctx->encoder_input_pool;
            //printf("trying to get new buffer..\n");
            new_buffer = mmal_queue_get(pool->queue);
            //printf("got new buffer %8x..\n", new_buffer);

            if (new_buffer)
            {
                //printf("%s getting from pool %8x buffer %8x\n", __func__, pool, new_buffer);
                status = mmal_port_send_buffer(port, new_buffer);
            }

            if (!new_buffer || status != MMAL_SUCCESS)
            {
                fprintf(stderr, "Unable to return a buffer to the video port\n");
            }
        }

    }

    ctx->times[0] = t0;
    ctx->times[1] = t.diff_time();
}

static void encoder_input_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    PORT_USERDATA *ctx = (PORT_USERDATA *)port->userdata;
    //fprintf(stderr, "INFO:%s %8x %d\n", __func__, buffer, buffer->cmd);
    if (buffer->cmd)
    {
        printf("%s buffer %8x event %d\n", __func__, buffer, buffer->cmd);
        mmal_buffer_header_release(buffer);
    }
    else
    {
        mmal_queue_put(ctx->encoder_input_pool->queue, buffer);
    }
}

// static MMAL_BOOL_T pool_buffer_available_callback(MMAL_POOL_T *pool, MMAL_BUFFER_HEADER_T *buffer,
//                                                   void *userdata)
// {
//     fprintf(stderr, "INFO:%s pool %8x buffer %8x\n", __func__, pool, buffer);
//     MMAL_PARAM_UNUSED(userdata);
//     mmal_queue_put(pool->queue, buffer);
//     return 0;
// }

static void encoder_output_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    //fprintf(stderr, "INFO:%s %8x\n", __func__, buffer);
    static Timer t;
    t.set_time();

    MMAL_BUFFER_HEADER_T    *new_buffer;
    PORT_USERDATA           *ctx = (PORT_USERDATA *)port->userdata;
    MMAL_POOL_T             *pool = ctx->encoder_output_pool;
    // fprintf(stderr, "INFO:%s\n", __func__);

    if (buffer->cmd)
    {
        printf("%s buffer %8x event %d\n", __func__, buffer, buffer->cmd);
        mmal_buffer_header_release(buffer);
    }
    else
    {
        mmal_buffer_header_mem_lock(buffer);
        fwrite(buffer->data, 1, buffer->length, ctx->fp);
        mmal_buffer_header_mem_unlock(buffer);
        mmal_buffer_header_release(buffer);
        if (port->is_enabled)
        {
            MMAL_STATUS_T status;
            //printf("trying to get new buffer..\n");
            new_buffer = mmal_queue_get(pool->queue);
            //printf("got new buffer %8x..\n", new_buffer);

            if (new_buffer)
            {
                //printf("%s getting from pool %8x buffer %8x\n", __func__, pool, buffer);
                status = mmal_port_send_buffer(port, new_buffer);
            }

            if (!new_buffer || status != MMAL_SUCCESS)
            {
                fprintf(stderr, "Unable to return a buffer to the video port\n");
            }
        }
    }
    ctx->times[2] = t.diff_time();
}

int fill_port_buffer(MMAL_PORT_T *port, MMAL_POOL_T *pool)
{
    fprintf(stderr, "INFO:%s pool\n", __func__, pool);
    int q;
    int num = mmal_queue_length(pool->queue);
    printf("%s pool %8x queue length is %d\n", __func__, pool, num);
    for (q = 0; q < num; q++)
    {
        MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(pool->queue);
        if (!buffer)
        {
            fprintf(stderr, "Unable to get a required buffer %d from pool queue\n", q);
        }
        printf("%s getting from pool %8x buffer %8x\n", __func__, pool, buffer);
        if (mmal_port_send_buffer(port, buffer) != MMAL_SUCCESS)
        {
            fprintf(stderr, "Unable to send a buffer to port (%d)\n", q);
        }
    }
    return 0;
}

int setup_camera(PORT_USERDATA *ctx)
{
    fprintf(stderr, "INFO:%s\n", __func__);
    MMAL_STATUS_T       status;
    MMAL_ES_FORMAT_T    *format;

    // status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);
    // if (status != MMAL_SUCCESS) {
    //     fprintf(stderr, "Error: create camera %x\n", status);
    //     return -1;
    // }
    // userdata->camera = camera;
    ctx->camera_video_port = ctx->camera->output[MMAL_CAMERA_VIDEO_PORT];


    mmal_format_copy(ctx->camera_video_port->format, ctx->camera->output[0]->format);

    format = ctx->camera_video_port->format;
    //format->encoding                    = MMAL_ENCODING_I420;
    format->encoding                    = MMAL_ENCODING_OPAQUE;
    format->encoding_variant            = MMAL_ENCODING_I420;
    format->es->video.width             = VIDEO_WIDTH;
    format->es->video.height            = VIDEO_HEIGHT;
    format->es->video.crop.x            = 0;
    format->es->video.crop.y            = 0;
    format->es->video.crop.width        = VIDEO_WIDTH;
    format->es->video.crop.height       = VIDEO_HEIGHT;
    format->es->video.frame_rate.num    = VIDEO_FPS;
    format->es->video.frame_rate.den    = 1;

    ctx->camera_video_port->buffer_size = format->es->video.width * format->es->video.height * 12 / 8;
    ctx->camera_video_port->buffer_num = 2;

    fprintf(stderr, "INFO:camera video buffer_size = %d\n", ctx->camera_video_port->buffer_size);
    fprintf(stderr, "INFO:camera video buffer_num = %d\n", ctx->camera_video_port->buffer_num);

    status = mmal_port_format_commit(ctx->camera_video_port);
    CHECK_STATUS(status, "unable to commit camera video port format");

    // Make zero copy before ppol create so buffers get allocated in GPU mem
    status = mmal_port_parameter_set_boolean(ctx->camera_video_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
    CHECK_STATUS(status, "failed to set zero copy on decoder input");

    ctx->camera_video_port->userdata = (struct MMAL_PORT_USERDATA_T *)ctx;

    status = mmal_port_enable(ctx->camera_video_port, camera_video_buffer_callback);
    CHECK_STATUS(status, "unable to enable camera video port");

    status = mmal_component_enable(ctx->camera);
    CHECK_STATUS(status, "unable to enable camera");

    // fill_port_buffer(userdata->camera_video_port, userdata->camera_video_port_pool);
    // fill_port_buffer(userdata->camera_video_port, userdata->encoder_input_pool);

    status = mmal_port_parameter_set_boolean(ctx->camera_video_port, MMAL_PARAMETER_CAPTURE, 1);
    CHECK_STATUS(status, "Failed to start capture");

    fprintf(stderr, "INFO: camera created\n");
    return 0;
}

int setup_encoder(PORT_USERDATA *ctx)
{
    fprintf(stderr, "INFO:%s\n", __func__);
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *encoder = 0;
    MMAL_PORT_T *preview_input_port = NULL;

    MMAL_PORT_T *encoder_input_port = NULL, *encoder_output_port = NULL;
    MMAL_POOL_T *encoder_input_port_pool;
    MMAL_POOL_T *encoder_output_port_pool;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder);
    CHECK_STATUS(status, "unable to create encoder");

    ctx->encoder_input_port             = encoder->input[0];
    ctx->encoder_input_port->userdata   = (struct MMAL_PORT_USERDATA_T *)ctx;
    ctx->encoder_output_port            = encoder->output[0];
    ctx->encoder_output_port->userdata  = (struct MMAL_PORT_USERDATA_T *)ctx;

    mmal_format_copy(ctx->encoder_input_port->format, ctx->camera_video_port->format);
    ctx->encoder_input_port->buffer_size = ctx->encoder_input_port->buffer_size_recommended;
    ctx->encoder_input_port->buffer_num = 2;

    mmal_format_copy(ctx->encoder_output_port->format, ctx->encoder_input_port->format);

    ctx->encoder_output_port->buffer_size = ctx->encoder_output_port->buffer_size_recommended;
    ctx->encoder_output_port->buffer_num = 2;
    // Commit the port changes to the input port
    status = mmal_port_format_commit(ctx->encoder_input_port);
    CHECK_STATUS(status, "unable to commit encoder input format");

    // Only supporting H264 at the moment
    // encoder_output_port->format->encoding = MMAL_ENCODING_H264;
    ctx->encoder_output_port->format->encoding                  = MMAL_ENCODING_MJPEG;
    ctx->encoder_output_port->format->bitrate                   = 1000000;
    ctx->encoder_output_port->format->es->video.width           = ctx->width;
    ctx->encoder_output_port->format->es->video.height          = ctx->height;
    ctx->encoder_output_port->format->es->video.frame_rate.num  = 30;
    ctx->encoder_output_port->format->es->video.frame_rate.den  = 1;


    // Commit the port changes to the output port
    status = mmal_port_format_commit(ctx->encoder_output_port);
    CHECK_STATUS(status, "unable to commit encoder output port format");

    fprintf(stderr, " encoder input buffer_size = %d\n", ctx->encoder_input_port->buffer_size);
    fprintf(stderr, " encoder input buffer_num = %d\n", ctx->encoder_input_port->buffer_num);

    fprintf(stderr, " encoder output buffer_size = %d\n", ctx->encoder_output_port->buffer_size);
    fprintf(stderr, " encoder output buffer_num = %d\n", ctx->encoder_output_port->buffer_num);

    status = mmal_port_parameter_set_boolean(ctx->encoder_input_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
    CHECK_STATUS(status, "failed to set zero copy on encoder input");
    status = mmal_port_parameter_set_boolean(ctx->encoder_output_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
    CHECK_STATUS(status, "failed to set zero copy on encoder output");

    ctx->encoder_input_pool = (MMAL_POOL_T *)mmal_port_pool_create(ctx->encoder_input_port, 
                                                ctx->encoder_input_port->buffer_num, 
                                                ctx->encoder_input_port->buffer_size);
    // mmal_pool_callback_set(ctx->encoder_input_pool, pool_buffer_available_callback, NULL);

    status = mmal_port_enable(ctx->encoder_input_port, encoder_input_buffer_callback);
    CHECK_STATUS(status, "unable to enable encoder input port");
    fprintf(stderr, "INFO:Encoder input pool %8x has been created\n", ctx->encoder_input_pool);

    ctx->encoder_output_pool = (MMAL_POOL_T *)mmal_port_pool_create(ctx->encoder_output_port, 
                                                ctx->encoder_output_port->buffer_num, 
                                                ctx->encoder_output_port->buffer_size);
    // mmal_pool_callback_set(ctx->encoder_output_pool, pool_buffer_available_callback, NULL);
    status = mmal_port_enable(ctx->encoder_output_port, encoder_output_buffer_callback);
    CHECK_STATUS(status, "unable to enable encoder output port");
    fprintf(stderr, "INFO:Encoder output pool %8x has been created\n", ctx->encoder_output_pool);

    fill_port_buffer(ctx->encoder_output_port, ctx->encoder_output_pool);
    fill_port_buffer(ctx->camera_video_port, ctx->encoder_input_pool);

    fprintf(stderr, "INFO:Encoder has been created\n");
    return 0;
}

int main(int argc, char **argv)
{
    PORT_USERDATA ctx;
    MMAL_STATUS_T status;
    memset(&ctx, 0, sizeof(PORT_USERDATA));

    // clean up after ctrl-c
    signal(SIGINT, ctrlc_handler);

    ctx.width = VIDEO_WIDTH;
    ctx.height = VIDEO_HEIGHT;

    fprintf(stderr, "VIDEO_WIDTH : %i\n", ctx.width);
    fprintf(stderr, "VIDEO_HEIGHT: %i\n", ctx.height);

    State state("Hello world", 0, 0, 1024, 512);
    // bcm_host_init();
    Detector detector(state, ctx.width, ctx.height, 1.0);
    MarkerVec mv;
    detector.camera.camera_matrix = (cv::Mat1f(3, 3) << 400, 0, 320,
                                     0, 400, 240,
                                     0, 0, 1);
    detector.camera.dist_coeffs = (cv::Mat1f(1, 5) << 0, 0, 0, 0, 0);

    Cparams cparams = {
        .width = (uint16_t)ctx.width,
        .height = (uint16_t)ctx.height,
        .fps = 30,
        .format = CAMGL_Y,
    };
    Texture t_camera(state, cparams);
    Texture t_image(state, ctx.width, ctx.height, true);
    Program p_camera(vs_camera, fs_camera, "camera");
    Mesh quad_yflip({"v_pos", "v_tex"}, {3, 2},
                    {
                        -1, -1, 0, 0.0, 0.0,
                        -1, 0.875, 0, 0.0, 1.0,
                        0.25, -1, 0, 1.0, 0.0,
                        0.25, 0.875, 0, 1.0, 1.0,
                    });
    ctx.camera = gcs_get_camera(t_camera.camGL->gcs);

    if (!(ctx.fp = fopen("test.h264", "wb")))
    {
        printf("Failed to open output file\n");
        exit(1);
    }

    status = (MMAL_STATUS_T)setup_camera(&ctx);
    CHECK_STATUS(status, "Setup camera failed");

    status = (MMAL_STATUS_T)setup_encoder(&ctx);
    CHECK_STATUS(status, "Setup encoder failed");

    //
    //
    // Stage times and average over 100 frames
    // Full res h264 to file
    // No zero copy
    //     40   4331   6054   1680  11881   3173   1395   1528  30082   8 |  36198   7640  30161 |  33572   1830    315
    //     39   4156   2857   1305  10359   2728   5204   2060  28708   8 |  38005  16850  28798 |  31828   1812    317
    //     36   3221   3089   1069  14456   3593   2679   2221  30364   8 |  45293   9288  30430 |  33498   1949    296
    //  38944  25.68
    //
    // Zero copy everywhere
    //     38   4422   3052   1085  14382   2402   4007    984  30372   4 |  37638  16741  30429 |  30903   7675    650
    //     39   2923   2698   1055  14832   2333   2931   1194  28005   3 |  46955   9188  28056 |  33377  12673    737
    //     36   2934   2921   1251  16750   3540   1302   1163  29897   3 |  37184   8832  29938 |  33537   9219    778
    //  39445  25.35
    //
    // Zero copy, mjpeg
    //     31   3066  10010   2133  11631   1635   1090    750  30346   3 |  36731   6837  30383 |  34428   4889    612
    //     35   4439   4138   1048  10248   2249    910   1010  24077   3 |  37367  13393  24117 |  31729   5754    389
    //     35   2955   2772   1275  10176   1699   1026   7568  27506   3 |  37554  17098  27549 |  33416   4155    721
    //  39222  25.50

    char text[256];

    master.set_time();
    int frames = 0;
    int alltotal = 0;
    MMAL_BUFFER_HEADER_T *buffer;
    while (keep_running && frames < 100)
    {
        frames++;

        status = (MMAL_STATUS_T)t_camera.get_cam_frame();
        CHECK_STATUS(status, "Failed to get camera frame");

        int t0 = master.elapsed_time(true);

        render_pass(state.fbo, quad_yflip, p_camera, t_camera, t_image);
        glFinish();

        int t1 = master.diff_time();

        // Run the detector
        mv = detector.detect(t_image);

        int t2 = master.diff_time();

        // Show some stats
        int total = 0;
        for (int i = 0; i < 8; i++)
        {
            printf("%6d ", detector.times[i]);
            total += detector.times[i];
        }
        printf("%6d %3d | %6d %6d %6d | %6d %6d %6d\n", total, mv.size(), t0, t1, t2,
               ctx.times[0], ctx.times[1], ctx.times[2]);
        alltotal += t0;
    }
    printf("%6d %6.2f\n", alltotal / 100, 100e6 / (float)alltotal);

    printf("Sigint, closing down\n");
    gcs_destroy(t_camera.camGL->gcs);
    if (ctx.fp)
        fclose(ctx.fp);
    if (ctx.encoder)
        mmal_component_release(ctx.encoder);

    return 0;
}

#else

#include "bcm_host.h"
#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include <interface/mmal/util/mmal_connection.h>
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/vcos/vcos.h"
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>

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
void main()
{
    gl_Position = v_pos;
    tex_coord = v_tex;
}
)glsl";

const char *fs_camera = R"glsl(
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

#define CHECK_STATUS(status, msg)                                    \
    if (status != MMAL_SUCCESS)                                      \
    {                                                                \
        fprintf(stderr, msg " %s\n", mmal_status_to_string(status)); \
        goto error;                                                  \
    }

static volatile sig_atomic_t keep_running = 1;
static void ctrlc_handler(int s)
{
    (void)s;
    keep_running = 0;
}

static struct CONTEXT_T
{
    VCOS_SEMAPHORE_T semaphore;
    FILE *fp;
} context;

static void encoder_input_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    // encoder has finished with input buffer, post semaphore so
    // we can get next frame
    struct CONTEXT_T *ctx = (struct CONTEXT_T *)port->userdata;
    vcos_semaphore_post(&ctx->semaphore);
    printf("encoder input callback\n");
}
static void encoder_output_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    // mmal_queue_put(ctx->queue_encoded, buffer);
    // vcos_semaphore_post(&ctx->semaphore);
    struct CONTEXT_T *ctx = (struct CONTEXT_T *)port->userdata;
    // FIXME put the data somewhere useful
    fwrite(buffer->data, 1, buffer->length, ctx->fp);
    printf("encoder output callback, wrote %d\n", buffer->length);
    mmal_buffer_header_release(buffer);
}
static void camera_output_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    struct CONTEXT_T *ctx = (struct CONTEXT_T *)port->userdata;
    printf("camera output callback\n");
}
void dump_header(MMAL_BUFFER_HEADER_T *b)
{
    printf("cmd:%08x alloc:%08x len:%08x offset:%08x flags:%08x dts:%lld pts:%lld\n",
           b->cmd, b->alloc_size, b->length, b->offset, b->flags, b->dts, b->pts);
}

int main(int argc, char **argv)
{
    // clean up after ctrl-c
    signal(SIGINT, ctrlc_handler);

    // -------------------------------------------------------------------------
    // Detector and camera setup
    // -------------------------------------------------------------------------
    // Set up GL
    State state("Hello world", 0, 0, 1024, 512);
    printf("State\n");

    // Set up detector with basic camera parameters
    const int width = 640;
    const int height = 480;
    Detector detector(state, width, height, 1.0);
    MarkerVec mv;
    detector.camera.camera_matrix = (cv::Mat1f(3, 3) << 400, 0, 320,
                                     0, 400, 240,
                                     0, 0, 1);
    detector.camera.dist_coeffs = (cv::Mat1f(1, 5) << 0, 0, 0, 0, 0);

    Cparams cparams = {
        .width = width,
        .height = height,
        .fps = 30,
        .format = CAMGL_Y,
    };
    Texture t_image(state, width, height, true);
    Texture t_camera(state, cparams);
    Program p_camera(vs_camera, fs_camera, "camera");
    Mesh quad_yflip({"v_pos", "v_tex"}, {3, 2},
                    {
                        -1,
                        -1,
                        0,
                        0.0,
                        0.0,
                        -1,
                        0.875,
                        0,
                        0.0,
                        1.0,
                        0.25,
                        -1,
                        0,
                        1.0,
                        0.0,
                        0.25,
                        0.875,
                        0,
                        1.0,
                        1.0,
                    });
    // -------------------------------------------------------------------------
    // Encoder setup
    // -------------------------------------------------------------------------
    vcos_semaphore_create(&context.semaphore, "consumed", 1);
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *encoder = NULL;
    MMAL_COMPONENT_T *camera = NULL;
    MMAL_PARAMETER_UINT32_T param;
    MMAL_POOL_T *pool_out;
    MMAL_ES_FORMAT_T *format;
    MMAL_VIDEO_FORMAT_T *videoFormat;
    FILE *fp;
    uint64_t last_us;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder);
    CHECK_STATUS(status, "failed to create decoder");
    status = mmal_port_parameter_set_boolean(encoder->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
    CHECK_STATUS(status, "failed to set zero copy on encoder input");
    status = mmal_port_parameter_set_boolean(encoder->output[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
    CHECK_STATUS(status, "failed to set zero copy on encoder output");

    // Make context available on encoder input so we can signal when
    // a frame has been consumed
    encoder->input[0]->userdata = (MMAL_PORT_USERDATA_T *)&context;
    encoder->output[0]->userdata = (MMAL_PORT_USERDATA_T *)&context;

    // Set format of encoder
    printf("About to set up cam out 1\n");
    camera = gcs_get_camera(t_camera.camGL->gcs);
    status = mmal_port_parameter_set_boolean(camera->output[1], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
    CHECK_STATUS(status, "failed to set zero copy on camera output");
    // Set format of video output
    format = camera->output[1]->format;
    format->encoding = MMAL_ENCODING_OPAQUE;
    format->encoding_variant = MMAL_ENCODING_I420;
    videoFormat = &format->es->video;
    videoFormat->width = width;
    videoFormat->height = height;
    videoFormat->crop.x = 0;
    videoFormat->crop.y = 0;
    videoFormat->crop.width = width;
    videoFormat->crop.height = height;
    videoFormat->frame_rate.num = 30;
    videoFormat->frame_rate.den = 1;
    status = mmal_port_format_commit(camera->output[1]);
    CHECK_STATUS(status, "Failed to set output port format");
    status = mmal_port_enable(camera->output[1], camera_output_callback);
    CHECK_STATUS(status, "failed input port enable");

    mmal_format_copy(encoder->input[0]->format, camera->output[1]->format);
    mmal_format_copy(encoder->output[0]->format, encoder->input[0]->format);
    encoder->output[0]->format->encoding = MMAL_ENCODING_MJPEG;
    encoder->output[0]->format->bitrate = 10000000;
    encoder->output[0]->format->es->video.width = width;
    encoder->output[0]->format->es->video.height = height;
    encoder->output[0]->format->es->video.frame_rate.num = 0;
    encoder->output[0]->format->es->video.frame_rate.den = 1;
    // encoder->output[0]->buffer_size = encoder->output[0]->buffer_size_recommended;
    encoder->output[0]->buffer_size = 256 << 10;
    encoder->output[0]->buffer_num = encoder->output[0]->buffer_num_recommended;
    status = mmal_port_format_commit(encoder->output[0]);
    CHECK_STATUS(status, "failed format commit");
    // param = {{ MMAL_PARAMETER_INTRAPERIOD, sizeof(param)}, 0};
    // status = mmal_port_parameter_set(encoder->output[0], &param.hdr);
    // CHECK_STATUS(status, "failed set intraperiod");

    // Create pool of output buffers
    pool_out = mmal_port_pool_create(encoder->output[0], 1, encoder->output[0]->buffer_size);
    CHECK_STATUS((pool_out ? MMAL_SUCCESS : MMAL_ENOMEM), "Error allocating pool");

    // Enable input and output ports, this starts the component
    status = mmal_port_enable(encoder->input[0], encoder_input_callback);
    CHECK_STATUS(status, "failed input port enable");
    status = mmal_port_enable(encoder->output[0], encoder_output_callback);
    CHECK_STATUS(status, "failed output port enable");

    fp = fopen("test.mjpeg", "wb");
    if (!fp)
    {
        printf("Failed to open file\n");
        goto error;
    }
    context.fp = fp;

    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------
    last_us = vcos_getmicrosecs();
    while (keep_running)
    {
        // Grab frame, we are using GLES to read the camera buffer, we can
        // probably do this directly with QPU code to avoid the extra copy
        status = (MMAL_STATUS_T)t_camera.get_cam_frame();
        CHECK_STATUS(status, "Failed to get camera frame");
        // t_camera.dump_file("f.raw");

        // TODO!!! complete separate buffer handling for output 1 of camera!

        // This is the MMAL buffer header, cast to MMAL_BUFFER_HEADER_T*
        // Valid until get_cam_frame is called again
        MMAL_BUFFER_HEADER_T *cam_buf_header = (MMAL_BUFFER_HEADER_T *)t_camera.mmal_cam_buffer_header;
        dump_header(cam_buf_header);
        status = mmal_port_send_buffer(encoder->input[0], cam_buf_header);
        CHECK_STATUS(status, "failed send input buffer");

        // Send any empty buffers from the output pool to the encoder
        MMAL_BUFFER_HEADER_T *encode_out_buf_header;
        while ((encode_out_buf_header = mmal_queue_get(pool_out->queue)) != NULL)
        {
            status = mmal_port_send_buffer(encoder->output[0], encode_out_buf_header);
            CHECK_STATUS(status, "failed send output buffer");
            printf("Sent output buffer\n");
        }

        render_pass(state.fbo, quad_yflip, p_camera, t_camera, t_image);
        glFinish();

        // Run the detector
        mv = detector.detect(t_image);

        // Wait for encoder to consume input frame
        vcos_semaphore_wait(&context.semaphore);

        // Show some stats
        int total = 0;
        for (int i = 0; i < 8; i++)
        {
            printf("%6d ", detector.times[i]);
            total += detector.times[i];
        }
        uint64_t us = vcos_getmicrosecs();
        uint64_t delta = us - last_us;
        last_us = us;
        printf("%6d %3d %10lld\n", total, mv.size(), delta);
        keep_running = 0;
    }

    printf("Caught signal\n");
error:
    gcs_destroy(t_camera.camGL->gcs);
    if (encoder)
        mmal_component_release(encoder);
    if (fp)
        fclose(fp);
}

#endif

// Useful info
// http://www.jvcref.com/files/PI/documentation/html/group___mmal_buffer_header.html
// https://github.com/t-moe/rpi_mmal_examples
// https://github.com/tasanakorn/rpi-mmal-demo
// https://forums.raspberrypi.com/viewtopic.php?t=167652

// Enable CPU side messages:
// export VC_LOGLEVEL="mmal:trace"
//
// Dump GPU side messages
// sudo vcdbg log msg

// #define CHECK_STATUS(status, msg)  \
//     if (status != MMAL_SUCCESS)    \
//     {                              \
//         fprintf(stderr, msg "\n"); \
//         exit(1);                   \
//     }

// static struct CONTEXT_T
// {
//     VCOS_SEMAPHORE_T    semaphore;
//     MMAL_QUEUE_T        *queue_encoded;
//     MMAL_STATUS_T       status;
// } context;

// int main(int argc, char** argv) {

//     MMAL_COMPONENT_T    *camera = 0;
//     MMAL_COMPONENT_T    *splitter = 0;
//     MMAL_COMPONENT_T    *preview = 0;
//     MMAL_COMPONENT_T    *encoder = 0;
//     MMAL_CONNECTION_T   *camera_splitter_connection = 0;
//     MMAL_CONNECTION_T   *splitter_encoder_connection = 0;
//     MMAL_CONNECTION_T   *splitter_preview_connection = 0;
//     int fd;
//     const char *filename = "output.mjpeg";

//     // Initialize MMAL
//     bcm_host_init();
//     vcos_semaphore_create(&context.semaphore, "example", 1);

//     // Create components
//     CHECK_STATUS(mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera), "create camera failed");
//     CHECK_STATUS(mmal_component_create(MMAL_COMPONENT_DEFAULT_SPLITTER, &splitter), "create splitter failed");
//     CHECK_STATUS(mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder), "create encoder failed");
//     CHECK_STATUS(mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &preview), "create preview failed");

//     CHECK_STATUS(mmal_port_parameter_set_boolean(camera->output[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE), "failed zero copy on camera out");
//     CHECK_STATUS(mmal_port_parameter_set_boolean(splitter->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE), "failed zero copy on splitter in");
//     CHECK_STATUS(mmal_port_parameter_set_boolean(splitter->output[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE), "failed zero copy on splitter out 0");
//     CHECK_STATUS(mmal_port_parameter_set_boolean(splitter->output[1], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE), "failed zero copy on splitter out 0");
//     CHECK_STATUS(mmal_port_parameter_set_boolean(preview->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE), "failed zero copy on preview in");
//     CHECK_STATUS(mmal_port_parameter_set_boolean(encoder->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE), "failed zero copy on encoder in");
//     CHECK_STATUS(mmal_port_parameter_set_boolean(encoder->output[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE), "failed zero copy on encoder out");

//     {
//         MMAL_PARAMETER_CAMERA_CONFIG_T cam_config =
//         {
//             { MMAL_PARAMETER_CAMERA_CONFIG, sizeof (cam_config)},
//             .max_stills_w = 1280,
//             .max_stills_h = 720,
//             .stills_yuv422 = 0,
//             .one_shot_stills = 1,
//             .max_preview_video_w = 1280,
//             .max_preview_video_h = 720,
//             .num_preview_video_frames = 3,
//             .stills_capture_circular_buffer_height = 0,
//             .fast_preview_resume = 0,
//             .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
//         };
//         mmal_port_parameter_set(camera->control, &cam_config.hdr);
//     }

//     // Close the output file
//     close(fd);

//     return 0;
// }
