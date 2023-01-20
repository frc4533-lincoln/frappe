//
//  glhelpers.hpp
//  gles2play
//
//  Created by Simon Jones on 20/03/2022.
//

#ifndef glhelpers_hpp
#define glhelpers_hpp

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <curses.h>

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <set>


// Uncomment for diagnostic messages
//#define DBGMSG


#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "bcm_host.h"
#include "user-vcsm.h"
#include "mailbox.h"
#include "gcs.h"

#ifdef __cplusplus
extern "C" {
#endif
#define VCOS_LOG_CATEGORY (&app_log_category)
#include "interface/vcos/vcos.h"
extern VCOS_LOG_CAT_T app_log_category;
#ifdef __cplusplus
}
#endif
#include "qpu_base.h"
#include "qpu_program.h"
#include "qpu_info.h"


//#include <drm_fourcc.h>
//#include <linux/dma-buf.h>
//#include <linux/dma-heap.h>

#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/imgcodecs.hpp"





#include "stb_image.h"




// void set_time();
// int diff_time();


class Timer
{
public:
    Timer(){verbose=false;}
    void set_time(bool v=false)
    {
        verbose = v;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        t = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
        if (verbose) printf("set:%llu\n", t);
    }
    int diff_time()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t d = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
        int delta = d - t;
        if (verbose) printf("diff:%llu %llu %d\n", d, t, delta);
        t = d;
        return delta;
    }
private:
    uint64_t t;
    bool verbose;
};

uint32_t float2bits(float x);





#define GLCHECK(...) {if(int e=glGetError()){printf(__VA_ARGS__);exit(1);}}





typedef struct EGL_Setup
{
	EGLDisplay  display;
	EGLSurface  surface;
	EGLContext  context;
	int         version_minor;
	int         version_major;
} EGL_Setup;


#define CHECK_STATUS_V(STATUS, MSG) \
       if (STATUS != VCOS_SUCCESS) { \
               printf("%s\n", MSG); \
               exit(1); \
       }
#define CHECK_EVAL(EVAL, ...) {if (!(EVAL)) {printf(__VA_ARGS__);exit(1);}}
 


// RPi Camera interface
#define CAMGL_SUCCESS			0
#define CAMGL_QUIT				1
#define CAMGL_ERROR				2
#define CAMGL_ALREADY_STARTED	3
#define CAMGL_START_FAILED		4
#define CAMGL_NOT_STARTED		5
#define CAMGL_GL_ERROR			6
#define CAMGL_NO_FRAMES			7

#define MAX_SIMUL_FRAMES 4

typedef enum CamGL_FrameFormat
{
	CAMGL_RGB,
	CAMGL_Y,
	CAMGL_YUV
} CamGL_FrameFormat;

struct Cparams
{
    uint16_t    width;
    uint16_t    height;
    uint16_t    fps;
    CamGL_FrameFormat format;
};
typedef struct CamGL_Frame
{
	CamGL_FrameFormat format;
	uint16_t width;
	uint16_t height;
	GLuint textureRGB;
	GLuint textureY;
	GLuint textureU;
	GLuint textureV;
} CamGL_Frame;

typedef struct CamGL_FrameInternal
{
	void *mmalBufferHandle;
	CamGL_FrameFormat format;
	EGLImageKHR eglImageRGB;
	EGLImageKHR eglImageY;
	EGLImageKHR eglImageU;
	EGLImageKHR eglImageV;
} CamGL_FrameInternal;

typedef struct CamGL
{
	// Flags
	bool started; // Camera is running
	bool quit; // Signal for thread to quit
	bool error; // Error Flag

	// GPU Camera Stream Interface
	GCS *gcs;

	// EGL / GL
	CamGL_Frame frame;

	// Realtime Threading
	VCOS_MUTEX_T accessMutex; // For synchronising access to all fields

	// Table of EGL images corresponding to MMAL opaque buffer handles
	CamGL_FrameInternal frames[MAX_SIMUL_FRAMES];
} CamGL;


class Texture;
class Framebuffer
{
public:
    Framebuffer();
    void bind(Texture &t);
    void gen();
private:
    GLuint fbo;
};

class State
{
public:
    State(std::string name, int x, int y, int width, int height);
    ~State(){}
    
    int window_should_close()
    {
        int vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        // vpwidth     = vp[2];
        // vpheight    = vp[3];
        return 0;
    }
    void swap_buffers()
    {
        eglSwapBuffers(setup.display, setup.surface);
    }
    void bind_fb_screen(int w = 0, int h = 0);
    void poll_events()
    {
    }
    void terminate()
    {
        eglTerminate(setup.display);
    }

    void setup_perf_counters();
    void clear_perf_counters();
    void report_perf_counters(int y, int x);


    //GLuint create_program(const char *vs, const char *fs);
    //std::map<std::string, GLuint> attribute_map, uniform_map;
    int                     vpwidth;
    int                     vpheight;
    EGL_DISPMANX_WINDOW_T   window;
    int                     mb;
    QPU_BASE                base;
    QPU_HWConfiguration     hwConfig;
	QPU_UserProgramInfo     upInfo;
    bool                    enableQPU[12];
    EGL_Setup               setup;
    GLuint                  vao;
    Framebuffer             fbo;
    // Attribute and uniform map from name to addr
    
};

class Program
{
public:
    Program(const char *vs, const char *fs, const char *label);
    ~Program(){}
    void use_program()
    {
        glUseProgram(p);
    }
    void set_uniform(std::string name, std::vector<float> v);
    std::map<std::string, GLuint> attribute_map, uniform_map;
    std::map<std::string, std::vector<float> > uniforms;
private:
    int res;
    GLuint p;
};

class VPUprogram
{
public:
    VPUprogram(State &_state, std::string elf_file);
    ~VPUprogram();
    uint32_t execute(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4, uint32_t r5);
// private:
    State       &state;
    unsigned    handle;
    unsigned    bus_address;
    uint8_t     *virt_address;
};

class QPUprogram
{
public:
    QPUprogram(State &_state, std::string bin_file, int _exu, int _tc, int _tr, int _w, int _h, int _mw, int _cs = 0);
    uint32_t execute(Texture &in, Texture &out, bool force_update = false, float scale_factor = 1.0, bool scaling = false);
    uint32_t execute_sc2(Texture &in, Texture &out, bool force_update = false, float scale_factor = 1.0, bool scaling = false);
    uint32_t execute_scaled(Texture &in, Texture &out, bool force_update = false, float scale_factor = 1.0, bool scaling = false);
    uint32_t execute(Texture &in, Texture &out, int owidth, int oheight, int ostride);
    uint32_t execute(Texture &in, Texture &out, std::vector<float> &m, int num);
    void set_uniform(int index, uint32_t value);
    State                   &state;
    QPU_PROGRAM             program;
	QPU_PROGMEM             progmemSetup;
    QPU_PerformanceState    perfState;
    bool                    perflog;
    int                     uniforms;
    int                     tile_rows, tile_cols, tile_width, tile_height, num_instances;
    int                     min_width;
    uint32_t                last_src_addr, last_dst_addr;
    std::vector<uint32_t>   uvalues;
    int                     constants;
};

class Mesh
{
public:
    Mesh(
         const std::vector<std::string> pack,
         const std::vector<int> _asize);
    Mesh(
         const std::vector<std::string> pack,
         const std::vector<int> _asize,
         std::vector<float> vertices);
    ~Mesh();
    void draw(Program &p, GLuint mode=GL_TRIANGLE_STRIP);
    void set_vertices(std::vector<float> vertices);
private:
    GLuint                      vbo_id;
    std::vector<std::string>    packing;
    std::vector<int>            asize;
    unsigned                    floats_per_vertex;
    unsigned                    vertex_count;
};




class Texture
{
public:
    Texture(State &_state, int w, int h, bool interp);
    Texture(State &_state, Cparams _cp);
    void bind(GLuint tunit)
    {
        glActiveTexture(tunit);
        glBindTexture(GL_TEXTURE_2D, texture);
        GLCHECK("tex bind %x", e);
    }
    void interp(bool mode);
    uint32_t lock() 
    {
        return mem_lock(state.mb, vc_handle);
    }
    void unlock() 
    {
        mem_unlock(state.mb, vc_handle);
    }
    char *user_lock()
    {
        user_buffer = (char *) vcsm_lock_cache(vcsm_info.vcsm_handle, VCSM_CACHE_TYPE_HOST, NULL);
        return user_buffer;
    }
    void user_unlock(int no_flush=0)
    {
        vcsm_unlock_ptr_sp(user_buffer, no_flush);
    }
    // The cache manipulation functions must use the usr opaque handle, not
    // the VC one
    void invalidate()
    {
        // Invalidate lines in texture range
        struct vcsm_user_clean_invalid_s tcache = {};
        tcache.s[0].handle  = vcsm_info.vcsm_handle;
        tcache.s[0].cmd     = 1;                        // Invalidate cache
        tcache.s[0].addr    = (uint32_t)user_buffer;
        tcache.s[0].size    = vcsm_info.width * vcsm_info.height * 4;
        vcsm_clean_invalid(&tcache);
    }
    void clean()
    {
        // Write dirty lines to memory
        struct vcsm_user_clean_invalid_s tcache = {};
        tcache.s[0].handle  = vcsm_info.vcsm_handle;
        tcache.s[0].cmd     = 2;                        // Clean cache
        tcache.s[0].addr    = (uint32_t)user_buffer;
        tcache.s[0].size    = vcsm_info.width * vcsm_info.height * 4;
        vcsm_clean_invalid(&tcache);
    }
    void clean_invalidate()
    {
        // Invalidate and write dirty lines to memory
        struct vcsm_user_clean_invalid_s tcache = {};
        tcache.s[0].handle  = vcsm_info.vcsm_handle;
        tcache.s[0].cmd     = 3;                        // Invalidate and clean cache
        tcache.s[0].addr    = (uint32_t)user_buffer;
        tcache.s[0].size    = vcsm_info.width * vcsm_info.height * 4;
        vcsm_clean_invalid(&tcache);
    }
    int get_cam_frame();
    void load_texture_data(const char *fname);
    void set_texture_data(uint8_t *data);
    void dump(int size = 0);
    void dump_float(int size = 0);
    void dump_file(const char *fname);
    void set(uint8_t data);
    GLuint                      texture;
    int                         width, height, buffer_width, buffer_height, channels;
    egl_image_brcm_vcsm_info    vcsm_info;
    uint32_t                    bus_address;
    int                         vc_handle;
    Cparams                     cp;
    CamGL                       *camGL;
    // Structure to hold info for shared videocore buffer that will back the texture
    EGLImageKHR                 egl_buffer;
    char                        *user_buffer;   // Userspace pointer to buffer
    State                       &state;
private:
};




void render_pass(Framebuffer &f, Mesh &m, Program &p, Texture &in, Texture &out, 
                GLuint mode=GL_TRIANGLE_STRIP);


uint32_t vpu_pass(VPUprogram &p, Texture &inbuf, Texture &outbuf);
void dump(State &state);
void dumpvrf(uint32_t addr, uint32_t size = 1024);

void dumphex(uint32_t addr, char *vaddr, int size);

int pot(int i);


char *get_file(const char *fname);



#endif /* glhelpers_hpp */
