//
//  main.cpp
//  gles2play
//
//  Created by Simon Jones on 19/03/2022.
//

#include <stdio.h>
#include <assert.h>

#define GL_SILENCE_DEPRECATION


#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "bcm_host.h"
#include "user-vcsm.h"

//----------------------------------------------------------------------------
//
// Minimal example of getting a GL context and being able to use eglCreateImageKHR
//
//----------------------------------------------------------------------------
int main(void)
{
    const int width     = 800;
    const int height    = 600;
    //int success         = 0;
    EGL_DISPMANX_WINDOW_T       window;
    EGLConfig                   config;
    EGLDisplay                  display;
    EGLContext                  context;
    EGLSurface                  surface;
    EGLImageKHR                 egl_buffer;    
    EGLint                      num_config;
    DISPMANX_DISPLAY_HANDLE_T   dispman_display;
    DISPMANX_UPDATE_HANDLE_T    dispman_update;
    VC_RECT_T                   dst_rect = {.x = 0, .y = 0, .width = width, .height = height};
    VC_RECT_T                   src_rect = {.width = width << 16, .height = height << 16};
    static const EGLint         attribute_list[] =
    {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE
    };

    vcsm_init();
    bcm_host_init();

    // Get a native window from dispmanx
    dispman_display = vc_dispmanx_display_open(0);
    dispman_update  = vc_dispmanx_update_start(0);
    window.width    = width;
    window.height   = height;
    window.element  = vc_dispmanx_element_add(dispman_update, dispman_display, 0, &dst_rect, 0,
                        &src_rect, DISPMANX_PROTECTION_NONE, 0, 0, DISPMANX_NO_ROTATE);
    vc_dispmanx_update_submit_sync(dispman_update);

    // Create EGL context and surface using that window
    display         = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, NULL, NULL);
    eglChooseConfig(display, attribute_list, &config, 1, &num_config);

    eglBindAPI(EGL_OPENGL_ES_API);
    context         = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);   
    surface         = eglCreateWindowSurface(display, config, &window, NULL);
    eglMakeCurrent(display, surface, surface, context);

    egl_image_brcm_vcsm_info vcsm_info = {.width = 256, .height = 256};
    egl_buffer      = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_VCSM, &vcsm_info, NULL);
    assert(egl_buffer != EGL_NO_IMAGE_KHR);

    glViewport(0, 0, width, height);
    glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(display, surface);
    while(1);
}


