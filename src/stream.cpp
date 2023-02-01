


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



#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <zmq.hpp>
#include <string>

#include "bcm_host.h"
#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"

#include "glhelpers.hpp"
#include "detector.hpp"
#include "qpu_base.h"

Timer master;


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



//https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
#include <memory>
template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    std::unique_ptr<char[]> buf( new char[ size ] ); 
    snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}


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

        //buffer->pts = vcos_getmicrosecs64();
        //printf("new video frame   %lld+%lld\n", buffer->pts, get_usecs() - buffer->pts);
        status = mmal_port_send_buffer(ctx->encoder_input_port, buffer);
        CHECK_STATUS(status, "send buffer failed");

        //printf("frame sent to encoder\n");
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



static void encoder_output_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    //fprintf(stderr, "INFO:%s %8x\n", __func__, buffer);
    static Timer t;
    t.set_time();

    MMAL_BUFFER_HEADER_T    *new_buffer;
    PORT_USERDATA           *ctx = (PORT_USERDATA *)port->userdata;
    MMAL_POOL_T             *pool = ctx->encoder_output_pool;
    //fprintf(stderr, "INFO:%s\n", __func__);

    if (buffer->cmd)
    {
        printf("%s buffer %8x event %d\n", __func__, buffer, buffer->cmd);
        mmal_buffer_header_release(buffer);
    }
    else
    {
        //printf("new encoded frame %lld+%lld\n", buffer->pts, get_usecs() - buffer->pts);
        if (ctx->fp)
        {
            mmal_buffer_header_mem_lock(buffer);
            fwrite(buffer->data, 1, buffer->length, ctx->fp);
            mmal_buffer_header_mem_unlock(buffer);
        }
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
                fprintf(stderr, "Unable to return a buffer to the output port\n");
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

    // The camera has already been set up, here we just set up the video port
    // by copying the format across and setting up the buffers
    ctx->camera_video_port = ctx->camera->output[MMAL_CAMERA_VIDEO_PORT];
    mmal_format_copy(ctx->camera_video_port->format, ctx->camera->output[0]->format);

    format = ctx->camera_video_port->format;

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

    // status = mmal_component_enable(ctx->camera);
    // CHECK_STATUS(status, "unable to enable camera");

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

    // encoder_output_port->format->encoding = MMAL_ENCODING_H264;
    //ctx->encoder_output_port->format->encoding                  = MMAL_ENCODING_H264;
    ctx->encoder_output_port->format->encoding                  = MMAL_ENCODING_MJPEG;
    ctx->encoder_output_port->format->bitrate                   = 5000000;
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

    // H264 parameters, these seem to be necessary for gstreamer to join an already running stream
    // FIXME can't get h264 latency anything like as low as the ~80ms for mjpeg
    // mmal_port_parameter_set_boolean(ctx->encoder_output_port, MMAL_PARAMETER_VIDEO_ENCODE_INLINE_HEADER, 1);
    // MMAL_PARAMETER_UINT32_T param = {{ MMAL_PARAMETER_INTRAPERIOD, sizeof(param)}, 1};
    // mmal_port_parameter_set(ctx->encoder_output_port, &param.hdr);



    ctx->encoder_input_pool = (MMAL_POOL_T *)mmal_port_pool_create(ctx->encoder_input_port, 
                                                ctx->encoder_input_port->buffer_num, 
                                                ctx->encoder_input_port->buffer_size);
    status = mmal_port_enable(ctx->encoder_input_port, encoder_input_buffer_callback);
    CHECK_STATUS(status, "unable to enable encoder input port");
    fprintf(stderr, "INFO:Encoder input pool %8x has been created\n", ctx->encoder_input_pool);

    ctx->encoder_output_pool = (MMAL_POOL_T *)mmal_port_pool_create(ctx->encoder_output_port, 
                                                ctx->encoder_output_port->buffer_num, 
                                                ctx->encoder_output_port->buffer_size);
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

    // -------------------------------------------------------------------
    // Set up decoder and camera
    // -------------------------------------------------------------------
    ctx.width   = VIDEO_WIDTH;
    ctx.height  = VIDEO_HEIGHT;

    State state("Hello world", 0, 0, 1024, 512);
    Detector detector(state, ctx.width, ctx.height, 1.0);
    detector.params.scale_adapt = true;
    MarkerVec mv;

    Cparams cparams = {
        .width  = (uint16_t)ctx.width,
        .height = (uint16_t)ctx.height,
        .fps = 30,
        .format = CAMGL_Y,
    };
    Texture t_camera(state, cparams);

    ctx.camera = gcs_get_camera(t_camera.camGL->gcs);



    // -------------------------------------------------------------------
    // Set up TCP socket to listen for connection and stream MJPEG to it
    // -------------------------------------------------------------------
    struct sockaddr_in saddr = {
        .sin_family = AF_INET,
        .sin_port   = htons(2222),
        .sin_addr   = {0}
    };
    bool listening = false;
    int sock_listen = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_listen >= 0)
    {
        int val = 1;
        setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
        if (bind(sock_listen, (struct sockaddr *)&saddr, sizeof(saddr)) >= 0)
        {
            // successful bind, we can now try and listen
            if (!listen(sock_listen, 0))
            {
                // now listening
                listening = true;
            }
            else
                printf("Failed to listen on socket %d\n", errno);
        }
        else
            printf("Failed to bind on socket %d\n", errno);
    }
    else
        printf("Failed to create socket %d\n", errno);



    // -------------------------------------------------------------------
    // Set up MMAL pipeline from camera to encoder
    // -------------------------------------------------------------------
    status = (MMAL_STATUS_T)setup_camera(&ctx);
    CHECK_STATUS(status, "Setup camera failed");

    status = (MMAL_STATUS_T)setup_encoder(&ctx);
    CHECK_STATUS(status, "Setup encoder failed");

 
    // -------------------------------------------------------------------
    // Set up ZMQ sockets
    // -------------------------------------------------------------------
    zmq::context_t zmqctx;
    zmq::socket_t fidpub(zmqctx, zmq::socket_type::pub);
    fidpub.bind("tcp://*:2223");


    // -------------------------------------------------------------------
    // Main loop. Grab frame from camera and run the detector on the 
    // frame. Send any detected fiducials over a ZMQ PUB/SUB socket.
    // Each frame, see if a request has been made on the TCP video streaming
    // request socket, if so set that up.
    // Also send stats over another ZMQ PUB.SUB socket.
    // -------------------------------------------------------------------
    master.set_time();
    int frames      = 0;
    int alltotal    = 0;
    MMAL_BUFFER_HEADER_T *buffer;
    while (keep_running)
    {
        // See if there has been a connection on the socket
        if (listening)
        {
            int sfd = accept(sock_listen, 0, 0);
            if (sfd >= 0)
            {
                if (!(ctx.fp = fdopen(sfd, "w")));
                {
                    printf("Failed to open socket file %d\n", errno);
                }
            }
        }


        frames++;

        status = (MMAL_STATUS_T)t_camera.get_cam_frame();
        CHECK_STATUS(status, "Failed to get camera frame");

        int t0 = master.elapsed_time(true);

        // Run the detector
        mv = detector.detect(t_camera);

        printf("%16lld+%5lld ", t_camera.pts, get_usecs() - t_camera.pts);

        int t1 = master.diff_time();

        // Show some stats
        int total = 0;
        for (int i = 0; i < 8; i++)
        {
            printf("%6d ", detector.times[i]);
            total += detector.times[i];
        }
        printf("%6d %3d | %6d %6d %6d | %6d %6d\n", total, mv.size(), t0, t1,
               ctx.times[0], ctx.times[1], ctx.times[2]);

        std::string msgtxt = string_format("%lld %lld %d %d ", 
            t_camera.pts, get_usecs() - t_camera.pts,
            mv.size(), total);
        for (int i = 0; i < mv.size(); i++)
        {
            auto &m = mv[i];
            msgtxt += string_format("%d %f %f %f %f %f %f %f %f ",
                m.id, 
                m.p[0].x, m.p[0].y, 
                m.p[1].x, m.p[1].y, 
                m.p[2].x, m.p[2].y, 
                m.p[3].x, m.p[3].y);
        }
        zmq::message_t msg(msgtxt.size());
        memcpy(msg.data(), msgtxt.data(), msgtxt.size());
        fidpub.send(msg);

        
        alltotal += t0;
    }
    printf("%6d %6.2f\n", alltotal / 100, 100e6 / (float)alltotal);

    printf("Sigint, closing down\n");
    gcs_destroy(t_camera.camGL->gcs);
    if (ctx.fp)
        fclose(ctx.fp);
    if (ctx.encoder)
        mmal_component_release(ctx.encoder);
    if (sock_listen >= 0)
        // We have now finished with the server socket
        close(sock_listen);

    return 0;
}



