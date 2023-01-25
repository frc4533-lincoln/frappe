//
//  glhelpers.cpp
//  gles2play
//
//  Created by Simon Jones on 20/03/2022.
//
#include "glhelpers.hpp"



#include <elf.h>

#include "qpu_registers.h"


// Global for timing
// uint64_t t;
// void set_time()
// {
//     struct timespec ts;
//     clock_gettime(CLOCK_MONOTONIC, &ts);
//     t = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
// }
// int diff_time()
// {
//     struct timespec ts;
//     clock_gettime(CLOCK_MONOTONIC, &ts);
//     int64_t d = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
//     int delta = d - t;
//     t = d;
//     return delta;
// }



uint32_t float2bits(float x)
{
    return *((uint32_t*)(&x));
}



int setupEGL(EGL_Setup *setup, EGLNativeWindowType window)
{
	EGLBoolean  estatus;
	EGLConfig   config;
	EGLint      num_configs;
	const EGLint config_attributes[] =
	{
		EGL_SURFACE_TYPE,	EGL_WINDOW_BIT,
		EGL_RED_SIZE,		8,
		EGL_GREEN_SIZE,	    8,
		EGL_BLUE_SIZE,		8,
		EGL_ALPHA_SIZE,	    8,
		EGL_DEPTH_SIZE,		0,
		EGL_NONE
	};
	const EGLint context_attributes[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

    GLCHECK("GL error before init: error 0x%04x\n", e);

	// Get display
	setup->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	CHECK_EVAL(setup->display != EGL_NO_DISPLAY, "Failed to get EGL display!");
	estatus = eglInitialize(setup->display, &setup->version_major, &setup->version_minor);
	CHECK_EVAL(estatus, "Failed to initialized EGL!");
    printf("eglGetDisplay\n");
	// Choose config according to attributes
	estatus = eglChooseConfig(setup->display, config_attributes, &config, 1, &num_configs);
	CHECK_EVAL(estatus, "Failed to choose config!");
    printf("eglChooseConfig\n");


	// Bind OpenGL ES API (Needed?)
	estatus = eglBindAPI(EGL_OPENGL_ES_API);
	CHECK_EVAL(estatus, "Failed to bind OpenGL ES API!");
    printf("eglBindAPI\n");

	// Create surface. FIXME!! This command seems to cause an error in 
	setup->surface = eglCreateWindowSurface(setup->display, config, window, NULL);
	CHECK_EVAL(setup->surface != EGL_NO_SURFACE, "Failed to create window surface!");
    printf("eglCreateWindowSurface\n");
    //exit(1);

	// Create context
	setup->context = eglCreateContext(setup->display, config, EGL_NO_CONTEXT, context_attributes);
	CHECK_EVAL(setup->context != EGL_NO_CONTEXT, "Failed to create context!");
	estatus = eglMakeCurrent(setup->display, setup->surface, setup->surface, setup->context);
	CHECK_EVAL(estatus, "Failed to make context current!");
    printf("eglCreateContext, MakeCurrent\n");

    GLCHECK("GL error during init: error 0x%04x\n", e);
 
	return 0;

}

void terminateEGL(EGL_Setup *setup)
{
	eglMakeCurrent(setup->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(setup->display, setup->context);
	eglDestroySurface(setup->display, setup->surface);
	eglTerminate(setup->display);
}



int create_native_window(EGL_DISPMANX_WINDOW_T *window, int x, int y, int width, int height)
{

	// Setup Fullscreen
	VC_RECT_T dst_rect = 
    {
        .x      = x,
        .y      = y,
		.width  = width,
		.height = height,
	};
	VC_RECT_T src_rect = 
    {
        .width  = width << 16,
        .height = height << 16,
    };
	VC_DISPMANX_ALPHA_T alpha = {DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS, 255, 0};

	// Create window
	DISPMANX_DISPLAY_HANDLE_T   dm_display  = vc_dispmanx_display_open(0);
	DISPMANX_UPDATE_HANDLE_T    dm_update   = vc_dispmanx_update_start(0);

	window->element = vc_dispmanx_element_add(dm_update, dm_display, 0, &dst_rect, 0,
		                    &src_rect, DISPMANX_PROTECTION_NONE, 0, 0, DISPMANX_NO_ROTATE);;
	window->width   = width;
	window->height  = height;
	vc_dispmanx_update_submit_sync(dm_update);

	return 0;
}




State::State(std::string name, int x, int y, int width, int height)
:   enableQPU{1,1,1,1, 1,1,1,1, 1,1,1,1}
{
    int ret;
    bcm_host_init();
    vcsm_init();
    //printf("bcm_host_init\n");
    if (create_native_window(&window, x, y, width, height))
        throw std::runtime_error("native window init failed!");
    ret = setupEGL(&setup, (EGLNativeWindowType*)&window);
    if (ret)
    {
        throw std::runtime_error("Failed setupEGL\n");
    }
    printf("setupEGL\n");
    mb = mbox_open();

    //-------------------------------------------------
    // Initialise QPU for user apps
    ret = qpu_initBase(&base, mb);
    if (ret)
	{
		throw std::runtime_error("Failed to init qpu base!");
	}    
    if (qpu_enable(mb, 1)) 
    {
		throw std::runtime_error("QPU enable failed!");
	}
    uint32_t id = base.peripherals[V3D_IDENT0];
    printf("V3D ident %08x\n", id);

    
    qpu_debugHW(&base);
    // VPM memory reservation
	base.peripherals[V3D_VPMBASE] = 16;
    qpu_getHWConfiguration(&hwConfig, &base);
	qpu_getUserProgramInfo(&upInfo, &base);

    // Generate FBO for render to texture
    fbo.gen();

    printf("GL Vendor            : %s\n", glGetString(GL_VENDOR));
    printf("GL Renderer          : %s\n", glGetString(GL_RENDERER));
    printf("GL Version (string)  : %s\n", glGetString(GL_VERSION));
    printf("GLSL Version         : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("EGL Vendor           : %s\n", eglQueryString(setup.display, EGL_VENDOR));
    printf("EGL Version          : %s\n", eglQueryString(setup.display, EGL_VERSION));
    printf("EGL Client APIs      : %s\n", eglQueryString(setup.display, EGL_CLIENT_APIS));
    printf("EGL Extensions       : %s\n", eglQueryString(setup.display, EGL_EXTENSIONS));

    int vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    printf("Viewport %d %d %d %d\n", vp[0],vp[1],vp[2],vp[3]);
    vpwidth     = vp[2];
    vpheight    = vp[3];
    
    //glCullFace(GL_BACK);
    //glEnable(GL_CULL_FACE);

}
void State::bind_fb_screen(int w, int h)
{
    //printf("Setting viewport %d %d %d %d\n", w, h, vpwidth, vpheight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (w == 0)
        glViewport(0, 0, vpwidth, vpheight);
    else
        glViewport(0, 0, w, h);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}


void State::setup_perf_counters()
{
    base.peripherals[V3D_PCTRS(0)] = 13;   // idle
    base.peripherals[V3D_PCTRS(1)] = 14;   // vshader
    base.peripherals[V3D_PCTRS(2)] = 15;   // fshader
    base.peripherals[V3D_PCTRS(3)] = 16;   // inst
    base.peripherals[V3D_PCTRS(4)] = 17;   // tmu_stall
    base.peripherals[V3D_PCTRS(5)] = 20;   // icache_hit
    base.peripherals[V3D_PCTRS(6)] = 21;   // icache_miss
    base.peripherals[V3D_PCTRS(7)] = 22;   // ucache_hit
    base.peripherals[V3D_PCTRS(8)] = 23;   // ucache_miss
    base.peripherals[V3D_PCTRS(9)] = 24;   // tex_quads
    base.peripherals[V3D_PCTRS(10)] = 25;  // tcache_miss
    base.peripherals[V3D_PCTRS(11)] = 26;  // vdw_stall
    base.peripherals[V3D_PCTRS(12)] = 27;  // vcd_stall
    base.peripherals[V3D_PCTRS(13)] = 28;  // l2_hit
    base.peripherals[V3D_PCTRS(14)] = 29;  // l2_miss
    base.peripherals[V3D_PCTRS(15)] = 19;  // vary_stall
    base.peripherals[V3D_PCTRC] = 0xffff;
    base.peripherals[V3D_PCTRE] = 0x8000ffff;
}

void State::clear_perf_counters()
{
    base.peripherals[V3D_PCTRC] = 0xffff;
    base.peripherals[V3D_PCTRC] = 0x0000;
    base.peripherals[V3D_PCTRE] = 0x8000ffff;
}

void State::report_perf_counters(int y, int x)
{
    base.peripherals[V3D_PCTRE] = 0x80000000;

    int32_t idle        = base.peripherals[V3D_PCTR(0)] / 4;
    int32_t vs          = base.peripherals[V3D_PCTR(1)] / 4;
    int32_t fs          = base.peripherals[V3D_PCTR(2)] / 4;
    int32_t inst        = base.peripherals[V3D_PCTR(3)] / 4;
    int32_t tmu_stall   = base.peripherals[V3D_PCTR(4)] / 4;
    int32_t ic_hit      = base.peripherals[V3D_PCTR(5)];
    int32_t ic_miss     = base.peripherals[V3D_PCTR(6)];
    int32_t uc_hit      = base.peripherals[V3D_PCTR(7)];
    int32_t uc_miss     = base.peripherals[V3D_PCTR(8)];
    int32_t quads       = base.peripherals[V3D_PCTR(9)] / 4;
    int32_t tc_miss     = base.peripherals[V3D_PCTR(10)];
    int32_t wdv_stall   = base.peripherals[V3D_PCTR(11)] / 4;
    int32_t vcd_stall   = base.peripherals[V3D_PCTR(12)] / 4;
    int32_t l2_hit      = base.peripherals[V3D_PCTR(13)];
    int32_t l2_miss     = base.peripherals[V3D_PCTR(14)];
    int32_t vary_st     = base.peripherals[V3D_PCTR(15)];

    int32_t total   = idle + fs + vs;
    int32_t active  = fs + vs;
    int32_t stall   = tmu_stall + wdv_stall + vcd_stall;

    float inst_frac = (float)inst / (float)total * 100;

    float load_frac = (float)active / (float)total * 100;
    float all_stall_frac = (float)stall / (float)total * 100;
    float tmu_stall_frac = (float)tmu_stall / (float)total * 100;

    int32_t l2_total    = l2_hit + l2_miss;
    float l2_miss_frac   = (float)l2_miss / (float)l2_total * 100;
    float tc_miss_frac   = (float)(tc_miss) / (float)quads * 100;
    int32_t ic_total = ic_hit + ic_miss;
    float ic_miss_frac = (float)ic_miss / (float)ic_total * 100;

    mvprintw(y, x, "%9d %9d %6.2f %6.2f %6.2f %9d %6.2f %9d %6.2f %9d %6.2f %9d %9d%",
        inst, 
        total, 
        load_frac, 
        all_stall_frac, 
        tmu_stall_frac, 
        quads, 
        tc_miss_frac,
        ic_total,
        ic_miss_frac,
        l2_total, 
        l2_miss_frac, 
        quads,
        tc_miss
    );
}


const char *typestr(GLenum type)
{
    switch (type)
    {
        case GL_FLOAT: return "GL_FLOAT";
        case GL_FLOAT_VEC2: return "GL_FLOAT_VEC2";
        case GL_FLOAT_VEC3: return "GL_FLOAT_VEC3";
        case GL_FLOAT_VEC4: return "GL_FLOAT_VEC4";
        case GL_FLOAT_MAT2: return "GL_FLOAT_MAT2";
        case GL_FLOAT_MAT3: return "GL_FLOAT_MAT3";
        case GL_FLOAT_MAT4: return "GL_FLOAT_MAT4";
        default: return "undefined";
    }
}



Program::Program(const char *vs, const char *fs, const char *label)
{
    int bf[4];
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vs, 0);
    glCompileShader(v);
    int res = GL_TRUE;
    glGetShaderiv(v, GL_COMPILE_STATUS, &res);
    if (res != GL_TRUE)
    {
        char log[1024];
        glGetShaderInfoLog(v, 1024, 0, log);
        printf("VShader compilation failed!\n%s\n", log);
        exit(1);
    }
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fs, 0);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &res);
    if (res != GL_TRUE)
    {
        char log[1024];
        glGetShaderInfoLog(f, 1024, 0, log);
        printf("FShader compilation failed!\n%s\n", log);
        exit(1);
    }
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &res);
    if (res != GL_TRUE)
    {
        char log[1024];
        glGetProgramInfoLog(p, 1024, 0, log);
        printf("Program link failed! %s\n%s\n", label, log);
        exit(1);
    }
    
    char name[1024];
    int size;
    GLenum type;
#ifdef DBGMSG
    printf("Active attributes\n");
#endif
    glGetProgramiv(p, GL_ACTIVE_ATTRIBUTES, &res);
    for(int i = 0; i < res; i++)
    {
        glGetActiveAttrib(p, i, 1024, 0, &size, &type, name);
        glBindAttribLocation(p, i, name);
        attribute_map[std::string(name)] = i;
#ifdef DBGMSG
        printf("%d %s %s bound at %d\n", size, typestr(type), name, attribute_map[std::string(name)]);
#endif
    }
    
#ifdef DBGMSG
    printf("Active uniforms\n");
#endif
    glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &res);
    for(int i = 0; i < res; i++)
    {
        glGetActiveUniform(p, i, 1024, 0, &size, &type, name);
        int loc = glGetUniformLocation(p, name);
        if (loc == -1)
        {
            printf("GetUniformLocation failed %d %s\n", i, name);
            exit(1);
        }
        uniform_map[std::string(name)] = loc;
#ifdef DBGMSG
        printf("%d %s %s location %d\n", size, typestr(type), name, loc);
#endif
    }
}

void Program::set_uniform(std::string name, std::vector<float> v)
{
    uniforms[name] = v;
}


VPUprogram::VPUprogram(State &_state, uint8_t *start, uint32_t size)
:  state(_state)
{
    // The linker script in ~/projects/rpi_experiments/vc4-toolchain/vc4-toolchain/prefix/vc4-elf/lib/vc4-sim/ld
    // has a hardwired RAM size of 0x100000. The size here must match. crt0.S saves the
    // sp from whatever OS is running on VC4 and moves the sp to the top of our region.
    int msize = 0x100000;
    // Get a handle a region of memory controlled by the VPU
    handle = mem_alloc(state.mb, msize, 4096, MEM_FLAG_L1_NONALLOCATING);
    // Lock memory buffer and return a bus address
    bus_address = mem_lock(state.mb, handle);
    // Map the memory into the virtual space 
    virt_address = (uint8_t *)mapmem(BUS_TO_PHYS(bus_address), msize);
#ifdef DBGMSG
    printf( "bus:     %08x\n"
            "phys:    %08x\n"
            "virt:    %08x\n", bus_address, BUS_TO_PHYS(bus_address), virt_address);
#endif
    printf("Loading VPU program %x+%x\n", start, size);
    memcpy(virt_address, start, size);
    //load_elf(elf_file.c_str(), virt_address);
    mem_unlock(state.mb, handle);
}
VPUprogram::~VPUprogram()
{
}
uint32_t VPUprogram::execute(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4, uint32_t r5)
{
    uint32_t ret = execute_code(state.mb, bus_address, r0, r1, r2, r3, r4, r5);
    return ret;
}



QPUprogram::QPUprogram(State &_state, uint8_t *start, uint32_t size, int _exu, int _tc, int _tr, int _w, int _h, int _mw, int _cs)
:   state(_state),
    uniforms(6 + _exu),
    tile_cols(_tc),
    tile_rows(_tr),
    tile_width(_w),
    tile_height(_h),
    min_width(_mw),
    num_instances(_tr * _tc),
    uvalues(_exu),
    constants(_cs)
{
    progmemSetup.codeSize       = size;
    progmemSetup.uniformsSize   = uniforms * num_instances;
    progmemSetup.messageSize    = 0;
    progmemSetup.constantsSize  = constants;

    printf("Loading QPU program %x+%x\n", start, size);
    qpu_initProgram(&program, &state.base, progmemSetup);

    qpu_lockBuffer(&program.progmem_buffer);
    memcpy(program.progmem.code.arm.cptr, start, size);
    qpu_unlockBuffer(&program.progmem_buffer);



    //qpu_loadProgramCode(&program, bin_file.c_str());


	qpu_setupPerformanceCounters(&state.base, &perfState);
	perfState.qpusUsed = 0;
    for(int i = 0; i < 12; i++)
        if (state.enableQPU[i])
            perfState.qpusUsed++;

    last_src_addr = 0;
    last_dst_addr = 0;

    perflog = false;
}
QPUprogram::QPUprogram(State &_state, std::string bin_file, int _exu, int _tc, int _tr, int _w, int _h, int _mw, int _cs)
:   state(_state),
    uniforms(6 + _exu),
    tile_cols(_tc),
    tile_rows(_tr),
    tile_width(_w),
    tile_height(_h),
    min_width(_mw),
    num_instances(_tr * _tc),
    uvalues(_exu),
    constants(_cs)
{
    printf("Loading QPU program %s\n", bin_file.c_str());
    progmemSetup.codeSize       = qpu_getCodeSize(bin_file.c_str());
    progmemSetup.uniformsSize   = uniforms * num_instances;
    progmemSetup.messageSize    = 0;
    progmemSetup.constantsSize  = constants;

    qpu_initProgram(&program, &state.base, progmemSetup);
    qpu_loadProgramCode(&program, bin_file.c_str());


	qpu_setupPerformanceCounters(&state.base, &perfState);
	perfState.qpusUsed = 0;
    for(int i = 0; i < 12; i++)
        if (state.enableQPU[i])
            perfState.qpusUsed++;

    last_src_addr = 0;
    last_dst_addr = 0;

    perflog = false;
}

void QPUprogram::set_uniform(int index, uint32_t value)
{
    uvalues[index] = value;
}

uint32_t QPUprogram::execute(Texture &in, Texture &out, bool force_update, float scale_factor, bool scaling)
{
    //qpu_setupPerformanceCounters(&state.base, &perfState);

    // With lock    4.2     3.3
    // Without lock 4.1     3.1
    // Maybe.. rather noisy, so leave lock in for now
    // uint32_t src_addr = in.lock();  in.unlock();
    // uint32_t dst_addr = out.lock(); out.unlock();
    uint32_t src_addr = in.bus_address;
    uint32_t dst_addr = out.bus_address;

    // Only use enough tiles for the scaled region
    int tr = tile_rows;
    int tc = tile_cols;
    int ni = num_instances;
    if (scale_factor < 1.0)
    {
        tr = ceil(tile_rows * scale_factor);
        tc = ceil(tile_cols * scale_factor);
        ni = tr * tc;
    }
    int tw = tile_width;
    int th = tile_height;
    if (scaling)
    {
        tw = tw / scale_factor;
        th = th / scale_factor - 1;// FIXME!! Should this be here???
        //endwin();printf("tw:%d th:%d\n", tw, th);exit(1);
    }
    if (force_update || (last_src_addr != src_addr) || (last_dst_addr != dst_addr))
    {
        qpu_lockBuffer(&program.progmem_buffer);
        last_src_addr = src_addr;
        last_dst_addr = dst_addr;
        // Build uniform stream
        uint32_t stride = in.vcsm_info.width * 4;
        for(int y = 0; y < tr; y++)
        {
            for(int x = 0; x < tc; x++)
            {
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms]       = src_addr + y * stride * th + x * tw * 4;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 1]   = dst_addr + y * stride * tile_height + x * tile_width * 4;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 2]   = stride;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 3]   = stride;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 4]   = tile_width / min_width;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 5]   = tile_height;
                for(int u = 6; u < uniforms; u++)
                {
                    program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + u] = uvalues[u - 6];
                }
            }

        }
        qpu_unlockBuffer(&program.progmem_buffer);
    }
    qpu_executeProgramDirect(&program, &state.base, ni, uniforms, uniforms, &perfState);


    qpu_updatePerformance(&state.base, &perfState);
    if (perflog) qpu_logPerformance(&perfState);
    return 0;
}
uint32_t QPUprogram::execute_sc2(Texture &in, Texture &out, bool force_update, float scale_factor, bool scaling)
{
    //qpu_setupPerformanceCounters(&state.base, &perfState);

    // With lock    4.2     3.3
    // Without lock 4.1     3.1
    // Maybe.. rather noisy, so leave lock in for now
    // uint32_t src_addr = in.lock();  in.unlock();
    // uint32_t dst_addr = out.lock(); out.unlock();
    uint32_t src_addr = in.bus_address;
    uint32_t dst_addr = out.bus_address;

    // Only use enough tiles for the scaled region
    int tr = tile_rows;
    int tc = tile_cols;
    int ni = num_instances;
    if (scale_factor < 1.0)
    {
        tr = ceil(tile_rows * scale_factor);
        tc = ceil(tile_cols * scale_factor);
        ni = tr * tc;
    }
    int tw = tile_width;
    int th = tile_height;
    if (scaling)
    {
        tw = tw / scale_factor;
        th = th / scale_factor;
        //endwin();printf("tw:%d th:%d\n", tw, th);exit(1);
    }
    if (force_update || (last_src_addr != src_addr) || (last_dst_addr != dst_addr))
    {
        qpu_lockBuffer(&program.progmem_buffer);
        last_src_addr = src_addr;
        last_dst_addr = dst_addr;
        // Build uniform stream
        uint32_t stride = in.vcsm_info.width * 4;

        // Construct the offsets

        //  ...
        uint32_t *p = (uint32_t*)program.progmem.constants.arm.vptr;
        float acc = 0.0;
        float inc = 1.0 / scale_factor;
        float yoff[1024];
        //printf("Constants: %08x %08x\n", (uint32_t)program.progmem.constants.arm.cptr, program.progmem.constants.vc);
        for(int i = 0; i < 1024; i++)
        {
            //int a = ((i & 0xf) << 2) | ((i & 0x30) >> 4) | (i & 0x3c0);
            int a = i;
            p[a] = (uint32_t)acc * 4;
            yoff[a] = acc;
            acc += inc;
        }


        for(int y = 0; y < tr; y++)
        {
            for(int x = 0; x < tc; x++)
            {
                float yo = yoff[y * tile_height];
                int line = (int)yo;
                
                float start = yo - line - inc;
                //program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms]       = src_addr + y * stride * th + x * tw * 4;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms]       = src_addr + line * stride - (int)(inc * 4);
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 1]   = dst_addr + y * stride * tile_height + x * tile_width * 4;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 2]   = stride;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 3]   = float2bits(start);
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 4]   = float2bits(inc);
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 5]   = tile_height;
                program.progmem.uniforms.arm.uptr[(y * tc + x) * uniforms + 6]   = program.progmem.constants.vc + x * 256;

                
            }

        }
        qpu_unlockBuffer(&program.progmem_buffer);
    }
    qpu_executeProgramDirect(&program, &state.base, ni, uniforms, uniforms, &perfState);


    qpu_updatePerformance(&state.base, &perfState);
    if (perflog) qpu_logPerformance(&perfState);
    return 0;
}
uint32_t QPUprogram::execute_scaled(Texture &in, Texture &out, bool force_update, float scale_factor, bool scaling)
{
    //qpu_setupPerformanceCounters(&state.base, &perfState);

    // Use this function for actually scaling.
    //
    // The output tiles represent larger regions of the input, so we unscale in order to get the correct source address
    // for each tile
    
    // uint32_t src_addr = in.lock();  in.unlock();
    // uint32_t dst_addr = out.lock(); out.unlock();
    uint32_t src_addr = in.bus_address;
    uint32_t dst_addr = out.bus_address;

    // Only use enough tiles for the scaled region
    int tw = tile_width;
    int th = tile_height;
    if (scaling)
    {
        tw = tw / scale_factor;
        th = th / scale_factor;
        //endwin();printf("tw:%d th:%d\n", tw, th);exit(1);
    }
    if (force_update || (last_src_addr != src_addr) || (last_dst_addr != dst_addr))
    {
        qpu_lockBuffer(&program.progmem_buffer);
        last_src_addr = src_addr;
        last_dst_addr = dst_addr;
        // Build uniform stream
        uint32_t stride = in.vcsm_info.width * 4;
        for(int y = 0; y < tile_rows; y++)
        {
            for(int x = 0; x < tile_cols; x++)
            {
                program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms]       = src_addr + y * stride * th + x * tw * 4;
                program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 1]   = dst_addr + y * stride * tile_height + x * tile_width * 4;
                program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 2]   = stride;
                program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 3]   = stride;
                program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 4]   = tile_width / min_width;
                program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 5]   = tile_height;
                for(int u = 6; u < uniforms; u++)
                {
                    program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + u] = uvalues[u - 6];
                }
            }

        }
        qpu_unlockBuffer(&program.progmem_buffer);
    }
    qpu_executeProgramDirect(&program, &state.base, num_instances, uniforms, uniforms, &perfState);

    qpu_updatePerformance(&state.base, &perfState);
    if (perflog) qpu_logPerformance(&perfState);
    return 0;
}

uint32_t QPUprogram::execute(Texture &in, Texture &out, int owidth, int oheight, int ostride)
{
    //qpu_setupPerformanceCounters(&state.base, &perfState);


    uint32_t src_addr = in.bus_address; //in.lock();  in.unlock();
    uint32_t dst_addr = out.bus_address; //out.lock(); out.unlock();

    qpu_lockBuffer(&program.progmem_buffer);
    // Build uniform stream
    uint32_t stride = in.vcsm_info.width * 4;
    for(int y = 0; y < tile_rows; y++)
    {
        for(int x = 0; x < tile_cols; x++)
        {
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms]       = src_addr + y * stride * tile_height + x * tile_width * 4;
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 1]   = dst_addr + y * ostride * oheight + x * owidth * 4;
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 2]   = stride;
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 3]   = ostride;
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 4]   = tile_width / min_width;
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 5]   = tile_height;

            // printf("%08x %08x %08x %08x %08x %08x ",
            //     program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms],
            //     program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms+1],
            //     program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms+2],
            //     program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms+3],
            //     program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms+4],
            //     program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms+5]
            // );
            for(int u = 6; u < uniforms; u++)
            {
                program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + u] = uvalues[u - 6];
                // printf("%08x ", uvalues[u - 6]);
            }
            // printf("\n");
        }

    }
    qpu_unlockBuffer(&program.progmem_buffer);
    qpu_executeProgramDirect(&program, &state.base, num_instances, uniforms, uniforms, &perfState);



    qpu_updatePerformance(&state.base, &perfState);
    if (perflog) qpu_logPerformance(&perfState);
    return 0;
}
uint32_t QPUprogram::execute(Texture &in, Texture &out, std::vector<float> &m, int num)
{
    uint32_t src_addr = in.bus_address; //in.lock();  in.unlock();
    uint32_t dst_addr = out.bus_address; //out.lock(); out.unlock();

    qpu_lockBuffer(&program.progmem_buffer);

    // Build uniform stream. This variant is specific to the warp operation.
    // The uniform stream consists of a dest addr in the fid buffer and its stride, and a transformation matrix
    // as 9 floats. After that is two uniforms that define the texture source, which is the 
    // input buffer. Only as many instances as there are fiducials to warp are enqueued.
    uint32_t stride = out.vcsm_info.width * 4;
    int iters = num;
    // endwin();
    for(int y = 0; y < tile_rows; y++)
    {
        for(int x = 0; x < tile_cols; x++)
        {
            int idx = (x + y * tile_cols) * 9;

            // for(int j = 0; j < 9; j++)
            // {
            //     float a = m[idx + j];
            //     printf("%d %8.3f %08x\n", j, a, float2bits(a));
            // }

            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms]       = dst_addr + y * stride * tile_height + x * tile_width * 4;
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 1]   = stride;
            // Transform matrix
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 2]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 3]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 4]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 5]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 6]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 7]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 8]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 9]   = float2bits(m[idx++]);
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 10]  = float2bits(m[idx++]);
            // Texture descriptor
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 11]  = (src_addr & 0xfffff000) | (1 << 8) | 0x00;
            program.progmem.uniforms.arm.uptr[(y * tile_cols + x) * uniforms + 12]  = (1 << 31)                         // RGBA32R
                                                                                    | (in.buffer_height << 20)          // height
                                                                                    | (in.buffer_width << 8)            // width
                                                                                    | (0 << 7)                          // LINEAR magnification
                                                                                    | (0 << 4)                          // LINEAR minification
                                                                                    | 0;                                // Wrap mode repeat
            iters--;
            if (!iters) break;
        }
        if (!iters) break;
    }

    qpu_unlockBuffer(&program.progmem_buffer);
    qpu_executeProgramDirect(&program, &state.base, num, uniforms, uniforms, &perfState);


    // out.dump();
    // exit(1);

    qpu_updatePerformance(&state.base, &perfState);
    if (perflog) qpu_logPerformance(&perfState);
    return 0;
}

Mesh::Mesh(const std::vector<std::string> pack, const std::vector<int> _asize, 
            std::vector<float> vertices)
: asize(_asize)
{
    packing = pack;
    floats_per_vertex = 0;
    for (int i = 0; i < packing.size(); i++)
    {
        floats_per_vertex += asize[i];
    }
    vertex_count = (unsigned)vertices.size() / floats_per_vertex;
    
    // Set up vertex buffer
    glGenBuffers(1, &vbo_id);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), &vertices[0], GL_STATIC_DRAW);
    GLCHECK("mesh:buffer\n");
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    //printf("ID: %d Vertex count: %d  floats per vertex: %d\n", vbo_id, vertex_count, floats_per_vertex);
}
Mesh::~Mesh()
{
    glDeleteBuffers(1, &vbo_id);
}
Mesh::Mesh(const std::vector<std::string> pack, const std::vector<int> _asize)
: asize(_asize)
{
    packing = pack;
    floats_per_vertex = 0;
    for (int i = 0; i < packing.size(); i++)
    {
        floats_per_vertex += asize[i];
    }
    //vertex_count = (unsigned)vertices.size() / floats_per_vertex;
    
    // Set up vertex buffer
    glGenBuffers(1, &vbo_id);
    //glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    //glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), &vertices[0], GL_STATIC_DRAW);
    //GLCHECK("buffer\n");
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void Mesh::set_vertices(std::vector<float> vertices)
{
    vertex_count = (unsigned)vertices.size() / floats_per_vertex;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), &vertices[0], GL_STATIC_DRAW);
    GLCHECK("mesh:set_vertices:buffer\n");
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void Mesh::draw(Program &p, GLuint mode)
{
    //printf("vbo_id: %d\n", vbo_id);
    GLCHECK("draw:prebind\n");
    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    GLCHECK("draw:bind\n");

    unsigned offset = 0;
    for (int i = 0; i < packing.size(); i++)
    {
        GLuint addr = p.attribute_map[packing[i]];
        glVertexAttribPointer(addr,
                              asize[i],
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(float) * floats_per_vertex,
                              (void *)(sizeof(float) * offset));
        GLCHECK("draw:attrptr\n");
        glEnableVertexAttribArray(addr);
        GLCHECK("draw:attrarray\n");
        offset += asize[i];
    }
    p.use_program();
    GLCHECK("draw:program\n");
    for (auto u : p.uniforms)
    {
        auto uname = u.first;
        auto uval = u.second;
        int s = (int)uval.size();
        //printf("%s %f %d\n", uname.c_str(),uval[0],s);
        if (s==1) glUniform1f(p.uniform_map[uname], uval[0]);
        else if (s==2) glUniform2f(p.uniform_map[uname], uval[0], uval[1]);
        else if (s==3) glUniform3f(p.uniform_map[uname], uval[0], uval[1], uval[2]);
        else if (s==4) glUniform4f(p.uniform_map[uname], uval[0], uval[1], uval[2], uval[3]);
    }
    GLCHECK("draw:uniforms\n");
    glDrawArrays(mode, 0, vertex_count);
    GLCHECK("draw:drawarrays %d %d\n", mode, vertex_count);
}


char *get_file(const char *fname)
{
    FILE *fp = fopen(fname, "r");
    if (!fp)
    {
        printf("Failed to open %s\n", fname);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    unsigned length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *data = (char*)malloc(length + 1);
    if (!data)
    {
        printf("Failed to allocate\n");
        exit(1);
    }
    size_t rlen = fread(data, 1, length, fp);
    if (rlen != length)
    {
        printf("Failed to read all data\n");
        exit(1);
    }
    data[length] = 0;
    //printf("bytes_read:%d\n", length);
    return data;
}

int pot(int i)
{
    int j = 1;
    while (j < i) j *= 2;
    return j;
}

Texture::Texture(State &_state, int w, int h, bool interp) : state(_state)
{
    width   = w;
    height  = h;
    buffer_width    = pot(w);
    buffer_height   = pot(h);
#ifdef DBGMSG
    printf("Texture dimensions:%d %d buffer dimensions: %d %d\n", w, h, buffer_width, buffer_height);
#endif

    // FIXME!! https://github.com/raspberrypi/linux/issues/4167
    // https://github.com/raspberrypi/userland/issues/720
    // 
    // The vcsm character device was removed from kernel >= 5.10.y
    // vcsm-cma can only allocate uncached on the arm side, this
    // totally breaks performance. Until dma-buf import is implemented
    // for eglCreateImageKHR (seems unlikely, there doesn't seem to be
    // further work on the legacy GL), we need to use an older kernel. 
    //
    //
    // This image:
    // https://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2021-01-12/
    // is the newest tha does not have 5.10, its 5.4.83
    //



    // vcsm buffers must have power-of-two dimensions
    vcsm_info.width     = buffer_width;
    vcsm_info.height    = buffer_height;

    // From the source, if creating a BRCM_VCSM, context must
    // be EGL_NO_CONTEXT
    egl_buffer = eglCreateImageKHR(state.setup.display, EGL_NO_CONTEXT, 
                                    EGL_IMAGE_BRCM_VCSM, &vcsm_info, NULL);
    if (egl_buffer == EGL_NO_IMAGE_KHR || vcsm_info.vcsm_handle == 0) 
    {
        printf("Failed to create EGL VCSM image\n");
        exit(1);
    }
    // Get the userspace address of the buffer
    user_buffer = (char *) vcsm_lock_cache(vcsm_info.vcsm_handle, VCSM_CACHE_TYPE_HOST, NULL);
    if (!user_buffer) {
        printf("Failed to lock VCSM buffer for handle %d\n", vcsm_info.vcsm_handle);
        exit(1);
    }
    // from this we can get the VC opaque handle, which we can use with mailbox calls
    // to lock the memory and get the bus address, then unlock the memory again
    vc_handle = vcsm_vc_hdl_from_ptr(user_buffer);
    vcsm_unlock_ptr(user_buffer);
    bus_address = lock();
    unlock();


    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (interp)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_buffer);
    glBindTexture(GL_TEXTURE_2D, 0);
#ifdef DBGMSG
    printf("Texture %d created %d %d buffer %d %d\n", texture, w, h, buffer_width, buffer_height);
#endif
}

void Texture::interp(bool mode)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    if (mode)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::load_texture_data(const char *fname)
{
    int w, h, c;
    
    stbi_set_flip_vertically_on_load(true);
    uint8_t *data = stbi_load(fname, &w, &h, &c, 4);
    if (w != width || h != height)
    {
        printf("Load texture mismatch in dimensions: %d %d %d %d\n", w, width, h, height);
        exit(1);
    }

    //set(0);//FIXME!! temp test
    set_texture_data(data);
    stbi_image_free(data);
}

void Texture::set_texture_data(uint8_t *data)
{
    // Set a mmapped texture to the input buffer data. Data is assumed to be RGBA, with 
    // width and height as set in texture creation.

    // Lock the memory for usermode access
    user_lock();
    
    // Copy the image into the buffer, we need to do this line-by-line because the 
    // buffer is next power of two above the input width
    for(int lines = 0; lines < height; lines++)
        memcpy(user_buffer + lines * buffer_width * 4, data + lines * width * 4, width * 4);

    user_unlock();
}

void Texture::dump(int size)
{
    if (!size)
        size = buffer_width * buffer_height * 4;
    user_lock();
    for (int i = 0; i < size; i++)
    {
        if (i%0x40 == 0) printf("%08x  ", i);
        printf("%02x", *(user_buffer + i));
        if (i%4 == 3) printf(" ");
        if (i%0x40 == 0x3f) printf("\n");
    }
    user_unlock();
}
void Texture::dump_file(const char *fname)
{
    glFinish();
    char *p = user_lock();
    FILE *fp = fopen(fname, "w");
    fwrite(p, 1, buffer_width * buffer_height * 4, fp);
    fclose(fp);
    user_unlock();
}
void Texture::dump_float(int size)
{
    if (!size)
        size = buffer_width * buffer_height * 4;
    user_lock();
    for (int i = 0; i < size; i+=4)
    {
        if (i%0x40 == 0) printf("%08x  ", i);
        float x = *(float*)(user_buffer + i);
        if (x == 0.0)
            printf("   -   ");
        else
            printf("%6.2f ", *(float*)(user_buffer + i));
        if (i%0x40 == 0x3c) printf("\n");
    }
    user_unlock();
}
void Texture::set(uint8_t data)
{
    int tsize = buffer_width * buffer_height * 4;
    char *addr = (char*)user_lock();
    memset(user_buffer, data, tsize);
    user_unlock();
}

Texture::Texture(State &_state, Cparams _cp) : state(_state), cp(_cp)
{
    // Set up a camera texture. This acts as a repository for
    // incoming camera frames, which can then be used by a GLES
    // shader. It has methods to get the next frame
	VCOS_STATUS_T vstatus;
	int istatus;
	GLenum glerror;

	// Init VCOS logging
	//vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_INFO);
	//vcos_log_register("CamGL", VCOS_LOG_CATEGORY);

	// Allocate CamGL structure
	camGL = (CamGL*)vcos_calloc(1, sizeof(*camGL), "camGL");
	CHECK_STATUS_V((camGL? VCOS_SUCCESS : VCOS_ENOMEM), "Memory allocation failure");

	// Initialize Semaphores
	vstatus = vcos_mutex_create(&camGL->accessMutex, "camGL-access");
	CHECK_STATUS_V(vstatus, "Error creating semaphore");

	// Init EGL
	//istatus = camGL_initGL(camGL);
    // Generate textures the current frame image is bound to
	glGenTextures(1, &camGL->frame.textureRGB);
	glGenTextures(1, &camGL->frame.textureY);
	glGenTextures(1, &camGL->frame.textureU);
	glGenTextures(1, &camGL->frame.textureV);

	// Init GL for video processing
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_DITHER);

    GLCHECK("tex cam gl init %x\n", e);

	// Init GCS
	//GCS_CameraParams gcsParams;
	gcsParams.mmalEnc       = MMAL_ENCODING_I420;
	//gcsParams.mmalEnc       = 0;
	gcsParams.width         = cp.width;
	gcsParams.height        = cp.height;
	gcsParams.fps           = cp.fps;
	gcsParams.shutterSpeed  = 0;
	gcsParams.iso           = -1;
    gcsParams.disableEXP    = false;
    gcsParams.disableAWB    = false;
    gcsParams.disableISPBlocks = 0;

    width   = cp.width;
    height  = cp.height;
    buffer_width    = pot(width);
    buffer_height   = pot(height);
    // vcsm buffers must have power-of-two dimensions
    vcsm_info.width     = buffer_width;
    vcsm_info.height    = buffer_height;

	camGL->gcs = gcs_create(&gcsParams);
	CHECK_STATUS_V(camGL->gcs? 0 : 1, "Error initialising GCS");

	camGL->quit     = false;
	camGL->error    = false;

	printf("Finished CamGL init\n");

	// Start GPU Camera Stream and process incoming frames
	if (gcs_start(camGL->gcs) == 0)
	{
		printf("Started GCS camera stream\n");
	}
	else
	{
		printf("Failed to start GPU Camera Stream!\n");
		exit(1);
	}


    // vc_handle = vcsm_vc_hdl_from_ptr(vcsm_buffer);

    texture = camGL->frame.textureY;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_buffer);
    glBindTexture(GL_TEXTURE_2D, 0);

    return;

}

static int camGL_processCameraFrame(Texture *t, void *frameBuffer);


int Texture::get_cam_frame()
{
    gcs_returnFrameBuffer(camGL->gcs);
    mmal_cam_buffer_header = gcs_requestFrameBuffer(camGL->gcs);
	if (mmal_cam_buffer_header)
	{

		void *cameraBuffer = gcs_getFrameBufferData(mmal_cam_buffer_header);

        // Get the bus address of the buffer
        printf("try to get bus address of frame %08x\n", cameraBuffer);
        vc_handle = vcsm_vc_hdl_from_ptr(cameraBuffer);
        printf("vc handle %8x\n", vc_handle);
        vcsm_info.vcsm_handle = vcsm_usr_handle(cameraBuffer);
        printf("vcsm user handle %8x\n", vcsm_info.vcsm_handle);
        bus_address = lock();
        printf("bus addr %08x\n", bus_address);
        unlock();

        if (gcsParams.mmalEnc == 0)
        {
            // Opaque format, needed for texture format convert
            if (camGL_processCameraFrame(this, cameraBuffer) == 0)
                return CAMGL_SUCCESS;
            printf("Failed to process frame!\n");
            return CAMGL_ERROR;
        }
        return CAMGL_SUCCESS;

	}
	else 
		printf("No frame received!\n");
	return CAMGL_NO_FRAMES;
}


static int camGL_processCameraFrame(Texture *t, void *frameBuffer)
{
	// Lookup or create EGL image corresponding to supplied buffer handle
	// Frames array is filled in sequentially and frames are bound to one 
    // buffer over their lifetime 
	int i;
	CamGL_FrameInternal *frameInt = NULL;
	for (i = 0; i < MAX_SIMUL_FRAMES; i++)
	{
		frameInt = &t->camGL->frames[i];
		if (frameInt->mmalBufferHandle == frameBuffer)
		{ // Found cached frame and corresponding image
			break;
		}

		if (frameInt->mmalBufferHandle == NULL)
		{ // Found unused frame - buffer has yet to be cached and associated with an image
			//CHECK_GL(camGL);

			EGLint createAttributes[] = {
				EGL_IMAGE_PRESERVED_KHR, GL_TRUE,
				EGL_NONE
			};

			// Create EGL textures from frame buffers according to format
			if (t->cp.format == CAMGL_RGB)
			{
				frameInt->eglImageRGB = eglCreateImageKHR(t->state.setup.display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_MULTIMEDIA, 
                                                            (EGLClientBuffer)frameBuffer, createAttributes);
				CHECK_EVAL(frameInt->eglImageRGB != EGL_NO_IMAGE_KHR, "Failed to convert frame buffer to RGB EGL image!\n");
			}
			else
			{
				frameInt->eglImageY = eglCreateImageKHR(t->state.setup.display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_MULTIMEDIA_Y, 
                                                            (EGLClientBuffer)frameBuffer, createAttributes);
				CHECK_EVAL(frameInt->eglImageY != EGL_NO_IMAGE_KHR, "Failed to convert frame buffer to Y EGL image!");
				if (t->cp.format == CAMGL_YUV)
				{
					frameInt->eglImageU = eglCreateImageKHR(t->state.setup.display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_MULTIMEDIA_U, 
                                                            (EGLClientBuffer)frameBuffer, createAttributes);
					CHECK_EVAL(frameInt->eglImageU != EGL_NO_IMAGE_KHR, "Failed to convert frame buffer to U EGL image!");
					frameInt->eglImageV = eglCreateImageKHR(t->state.setup.display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_MULTIMEDIA_V, 
                                                            (EGLClientBuffer)frameBuffer, createAttributes);
					CHECK_EVAL(frameInt->eglImageV != EGL_NO_IMAGE_KHR, "Failed to convert frame buffer to V EGL image!");
				}
			}

			// Success
			//printf("Created EGL images format %d for buffer index %d\n", t->cp.format, i);
			frameInt->mmalBufferHandle = frameBuffer;
			frameInt->format = t->cp.format;
			//CHECK_GL(camGL);
			break;
		}
	}

	if (i == MAX_SIMUL_FRAMES)
	{
		printf("Exceeded configured max number of EGL images\n");
		return -1;
	}

	// Create frame information for client
	t->camGL->frame.format = frameInt->format;
	t->camGL->frame.width = t->cp.width;
	t->camGL->frame.height = t->cp.height;


	// Bind images to textures
	if (frameInt->format == CAMGL_RGB)
	{
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, t->camGL->frame.textureRGB);
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, frameInt->eglImageRGB);
		//CHECK_GL(camGL);
	}
	else
	{
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, t->camGL->frame.textureY);
		//CHECK_GL(camGL);
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, frameInt->eglImageY);
		//CHECK_GL(camGL);
		if (frameInt->format == CAMGL_YUV)
		{
			glBindTexture(GL_TEXTURE_EXTERNAL_OES, t->camGL->frame.textureU);
			glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, frameInt->eglImageU);
			//CHECK_GL(camGL);
			glBindTexture(GL_TEXTURE_EXTERNAL_OES, t->camGL->frame.textureV);
			glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, frameInt->eglImageV);
			//CHECK_GL(camGL);
		}
	}
	return 0;
}


Framebuffer::Framebuffer()
{
}
void Framebuffer::gen()
{
    glGenFramebuffers(1, &fbo);
    GLCHECK("fb:fb %x\n", e);
    //printf("Created fbo %d\n", fbo);
}

void Framebuffer::bind(Texture &t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLCHECK("fbo bind:1\n");
    //printf("texture %d\n", t.texture);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    GLCHECK("fbo bind:2\n");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    GLCHECK("fbo bind:3\n");
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        //printf("fbo %d complete\n", fbo);
    }
    else
    {
        endwin();
        printf("fbo %d not complete!\n", fbo);
        exit(1);
    }
    glViewport(0, 0, t.buffer_width, t.buffer_height);
    GLCHECK("fbo bind:3\n");
}




void render_pass(Framebuffer &f, Mesh &m, Program &p, Texture &in, Texture &out, 
    GLuint mode)
{
    in.bind(GL_TEXTURE0);
    GLCHECK("in bind\n");
    f.bind(out);
    GLCHECK("fbo bind\n");
    glClearColor(0.0, 1.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    m.draw(p, mode);
    //glFinish();
}


void dumphex(uint32_t addr, char *vaddr, int size)
{
    for(int i = 0; i < size; i++)
    {
        if (i%0x40 == 0)
            printf("%08lx %08lx ", (uint64_t)vaddr, (uint64_t)addr);
        addr++;
        printf("%02x ", *vaddr++);
        if (i%0x40 == 0x3f)
            printf("\n");
    }
}

void dumpvrf(uint32_t addr, uint32_t size)
{
    // addr is address in VC4 bus_address space, we need to map to arm
    // uint32_t paddr = BUS_TO_PHYS(addr);
    // uint8_t *vaddr = (uint8_t *)mapmem(paddr, 4096);
    //printf("mapping to virtual baddr:%08x paddr:%08x, vaddr:%08x\n", addr, paddr, (uint32_t)vaddr);
    printf("         00       01       02       03       04       05       06       07       08       09       0a       0b       0c       0d       0e       0f\n");
    for(int i = 0; i < size; i++)
    {
        if (i%0x10 == 0)
            printf("%02x ", i>>4);
        printf("%08x ", *(uint32_t*)addr);
        addr+= 4;
        if (i%0x10 == 0xf)
            printf("\n");
    }
}

uint32_t vpu_pass(VPUprogram &p, Texture &inbuf, Texture &outbuf)
{
    // Get the bus addresses of the buffers
    uint32_t inbuf_bus_addr = inbuf.lock();
    inbuf.unlock();
    uint32_t outbuf_bus_addr = outbuf.lock();
    outbuf.unlock();
    uint32_t r;
    r = p.execute(inbuf_bus_addr, 4096, outbuf_bus_addr, 640, 480, 0);
    //dumpvrf(r + (uint32_t)p.virt_address);
    //exit(1);
    return r;
}
