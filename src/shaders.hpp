

#ifndef shaders_hpp
#define shaders_hpp

const char *vs = R"(
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
)";

const char *vs_tex = R"(
#version 100
precision mediump float;

attribute vec4 v_pos;
attribute vec2 v_tex;
varying highp vec2 tex_coord;
void main()
{
    gl_Position = v_pos;
    tex_coord   = v_tex;
}
)";
const char *vs_simple = R"(
#version 100
precision mediump float;

attribute vec4 v_pos;
void main()
{
    gl_Position = v_pos;
}
)";

const char *fs_gaussian= R"(
#version 100
precision mediump float;
precision mediump sampler2D;

varying vec2 tex_coord;
uniform sampler2D tex;
uniform float hpix0, hpix1;
uniform float vpix0, vpix1;
uniform vec4 c3;
void main()
{
    // const float a0 = 1.212;
    // const float a1 = 1.2;
    float c = 0.15018 * texture2D(tex, vec2(tex_coord.x, tex_coord.y)).x
                + 0.12088 *     ( texture2D(tex, vec2(tex_coord.x - hpix0, tex_coord.y)).x
                                + texture2D(tex, vec2(tex_coord.x + hpix0, tex_coord.y)).x
                                + texture2D(tex, vec2(tex_coord.x, tex_coord.y - vpix0)).x
                                + texture2D(tex, vec2(tex_coord.x, tex_coord.y + vpix0)).x
                                )
                + 0.091575 *    ( texture2D(tex, vec2(tex_coord.x - hpix1, tex_coord.y - vpix1)).x
                                + texture2D(tex, vec2(tex_coord.x + hpix1, tex_coord.y - vpix1)).x
                                + texture2D(tex, vec2(tex_coord.x - hpix1, tex_coord.y + vpix1)).x
                                + texture2D(tex, vec2(tex_coord.x + hpix1, tex_coord.y + vpix1)).x
                                );

    gl_FragColor = vec4(c);
}
)";


const char *fs_null= R"(
#version 100
precision mediump float;
precision mediump sampler2D;
varying vec2 tex_coord;
uniform sampler2D tex;
void main()
{
    gl_FragColor = vec4(texture2D(tex, tex_coord).xyz, 1.0);
}
)";
const char *fs_overlaycorners = R"(
#version 100
precision mediump float;
precision mediump sampler2D;
varying vec2 tex_coord;
uniform sampler2D tex;
void main()
{
    vec4 col = texture2D(tex, tex_coord);
    if (col.y == 0.0)
        col.a = 0.1;
    else
        col = vec4(0.0, col.y, col.y, 0.1);
    gl_FragColor = col;
}
)";

const char *fs_colour= R"(
#version 100
precision highp float;
uniform vec4 colour;
void main()
{
    gl_FragColor = colour;
}
)";

const char *fs_sobel_shi_tomasi = R"(
#version 100
#define PI 3.14159265
precision highp float;
precision highp sampler2D;

varying vec2 tex_coord;
varying vec2 tc0, tc1, tc2, tc3, tc5, tc6, tc7, tc8;
uniform float sx;
uniform float sy;
uniform float dx;
uniform float dy;
uniform sampler2D tex;
void main()
{

    float p0 = texture2D(tex, tc0).x;
    float p1 = texture2D(tex, tc1).x;
    float p2 = texture2D(tex, tc2).x;
    float p3 = texture2D(tex, tc3).x;
    float p4 = texture2D(tex, tex_coord).x;
    float p5 = texture2D(tex, tc5).x;
    float p6 = texture2D(tex, tc6).x;
    float p7 = texture2D(tex, tc7).x;
    float p8 = texture2D(tex, tc8).x;



    float gx =   (p2 + 2.0 * p5 + p8 - p0 - 2.0 * p3 - p6);
    float gy =   (p0 + 2.0 * p1 + p2 - p6 - 2.0 * p7 - p8);
    float g = 0.5 * length(vec2(gx, gy));


    vec2 gv = normalize(vec2(gx, gy));

    float dth = 0.0;



    // The vector with the highest dot product to the gradient vector
    // is the angle approximation
    const vec2 d0 = vec2(1.0, 0.0);
    const vec2 d1 = vec2(0.707, 0.707);
    const vec2 d2 = vec2(0.0, 1.0);
    const vec2 d3 = vec2(-0.707, 0.707);
    float d0d = abs(dot(gv, d0));
    float d1d = abs(dot(gv, d1));
    float d2d = abs(dot(gv, d2));
    float d3d = abs(dot(gv, d3));


    dth = ((d0d >= d1d) && (d0d >= d2d) && (d0d >= d3d)) ? 0.0
        : ((d1d >= d0d) && (d1d >= d2d) && (d1d >= d3d)) ? 0.25
        : ((d2d >= d0d) && (d2d >= d1d) && (d2d >= d3d)) ? 0.5
        : ((d3d >= d0d) && (d3d >= d1d) && (d3d >= d2d)) ? 0.75 : 0.1;

    // We have the gradients already calculated, so we can do Shi-Tomasi
    // corner detection cheaply. Feed them through to the next stage.

    gl_FragColor = vec4(g, dth, 0.25 * gx + 0.5, 0.25 * gy + 0.5);
}
)";

const char *fs_sobel_shi_tomasi2 = R"(
#version 100
#define PI 3.14159265
precision highp float;
precision highp sampler2D;
varying vec2 tex_coord;
uniform float hpix;
uniform float vpix;
uniform sampler2D tex;
void main()
{

    float p00 = texture2D(tex, vec2(tex_coord.x - 2.0 * hpix, tex_coord.y + 2.0 * vpix)).x;
    float p01 = texture2D(tex, vec2(tex_coord.x - 1.0 * hpix, tex_coord.y + 2.0 * vpix)).x;
    float p02 = texture2D(tex, vec2(tex_coord.x             , tex_coord.y + 2.0 * vpix)).x;
    float p03 = texture2D(tex, vec2(tex_coord.x + 1.0 * hpix, tex_coord.y + 2.0 * vpix)).x;
    float p04 = texture2D(tex, vec2(tex_coord.x + 2.0 * hpix, tex_coord.y + 2.0 * vpix)).x;

    float p10 = texture2D(tex, vec2(tex_coord.x - 2.0 * hpix, tex_coord.y + 1.0 * vpix)).x;
    float p11 = texture2D(tex, vec2(tex_coord.x - 1.0 * hpix, tex_coord.y + 1.0 * vpix)).x;
    float p12 = texture2D(tex, vec2(tex_coord.x             , tex_coord.y + 1.0 * vpix)).x;
    float p13 = texture2D(tex, vec2(tex_coord.x + 1.0 * hpix, tex_coord.y + 1.0 * vpix)).x;
    float p14 = texture2D(tex, vec2(tex_coord.x + 2.0 * hpix, tex_coord.y + 1.0 * vpix)).x;

    float p20 = texture2D(tex, vec2(tex_coord.x - 2.0 * hpix, tex_coord.y             )).x;
    float p21 = texture2D(tex, vec2(tex_coord.x - 1.0 * hpix, tex_coord.y             )).x;
    float p22 = texture2D(tex, vec2(tex_coord.x             , tex_coord.y             )).x;
    float p23 = texture2D(tex, vec2(tex_coord.x + 1.0 * hpix, tex_coord.y             )).x;
    float p24 = texture2D(tex, vec2(tex_coord.x + 2.0 * hpix, tex_coord.y             )).x;

    float p30 = texture2D(tex, vec2(tex_coord.x - 2.0 * hpix, tex_coord.y - 1.0 * vpix)).x;
    float p31 = texture2D(tex, vec2(tex_coord.x - 1.0 * hpix, tex_coord.y - 1.0 * vpix)).x;
    float p32 = texture2D(tex, vec2(tex_coord.x             , tex_coord.y - 1.0 * vpix)).x;
    float p33 = texture2D(tex, vec2(tex_coord.x + 1.0 * hpix, tex_coord.y - 1.0 * vpix)).x;
    float p34 = texture2D(tex, vec2(tex_coord.x + 2.0 * hpix, tex_coord.y - 2.0 * vpix)).x;

    float p40 = texture2D(tex, vec2(tex_coord.x - 2.0 * hpix, tex_coord.y - 1.0 * vpix)).x;
    float p41 = texture2D(tex, vec2(tex_coord.x - 1.0 * hpix, tex_coord.y - 2.0 * vpix)).x;
    float p42 = texture2D(tex, vec2(tex_coord.x             , tex_coord.y - 2.0 * vpix)).x;
    float p43 = texture2D(tex, vec2(tex_coord.x + 1.0 * hpix, tex_coord.y - 2.0 * vpix)).x;
    float p44 = texture2D(tex, vec2(tex_coord.x + 2.0 * hpix, tex_coord.y - 2.0 * vpix)).x;

    // gx coeffs        gy coeffs
    // -1  0  1         1  2  1
    // -2  0  2         0  0  0
    // -1  0  1        -1 -2 -1

    float gx0 = p02 + 2.0 * p12 + p22 - p00 - 2.0 * p10 - p20;
    float gx1 = p03 + 2.0 * p13 + p23 - p01 - 2.0 * p11 - p21;
    float gx2 = p04 + 2.0 * p14 + p24 - p02 - 2.0 * p12 - p22;
    float gx3 = p12 + 2.0 * p22 + p32 - p10 - 2.0 * p20 - p30;
    float gx4 = p13 + 2.0 * p23 + p33 - p11 - 2.0 * p21 - p31;
    float gx5 = p14 + 2.0 * p24 + p34 - p12 - 2.0 * p22 - p32;
    float gx6 = p22 + 2.0 * p32 + p42 - p20 - 2.0 * p30 - p40;
    float gx7 = p23 + 2.0 * p33 + p43 - p21 - 2.0 * p31 - p41;
    float gx8 = p24 + 2.0 * p34 + p44 - p22 - 2.0 * p32 - p42;

    float gy0 = p00 + 2.0 * p01 + p02 - p20 - 2.0 * p21 - p22;
    float gy1 = p01 + 2.0 * p02 + p03 - p21 - 2.0 * p22 - p23;
    float gy2 = p02 + 2.0 * p03 + p04 - p22 - 2.0 * p23 - p24;
    float gy3 = p10 + 2.0 * p11 + p12 - p30 - 2.0 * p31 - p32;
    float gy4 = p11 + 2.0 * p12 + p13 - p31 - 2.0 * p32 - p33;
    float gy5 = p12 + 2.0 * p13 + p14 - p32 - 2.0 * p33 - p34;
    float gy6 = p20 + 2.0 * p21 + p22 - p40 - 2.0 * p41 - p42;
    float gy7 = p21 + 2.0 * p22 + p23 - p41 - 2.0 * p42 - p43;
    float gy8 = p22 + 2.0 * p23 + p24 - p42 - 2.0 * p43 - p44;


    // float g = 0.5 * length(vec2(gx4, gy4));
    // vec2 gv = normalize(vec2(gx4, gy4));

    // float dth = 0.0;


    // // The vector with the highest dot product to the gradient vector
    // // is the angle approximation
    // const vec2 d0 = vec2(1.0, 0.0);
    // const vec2 d1 = vec2(0.707, 0.707);
    // const vec2 d2 = vec2(0.0, 1.0);
    // const vec2 d3 = vec2(-0.707, 0.707);
    // float d0d = abs(dot(gv, d0));
    // float d1d = abs(dot(gv, d1));
    // float d2d = abs(dot(gv, d2));
    // float d3d = abs(dot(gv, d3));


    // dth = ((d0d >= d1d) && (d0d >= d2d) && (d0d >= d3d)) ? 0.0
    //     : ((d1d >= d0d) && (d1d >= d2d) && (d1d >= d3d)) ? 0.25
    //     : ((d2d >= d0d) && (d2d >= d1d) && (d2d >= d3d)) ? 0.5
    //     : ((d3d >= d0d) && (d3d >= d1d) && (d3d >= d2d)) ? 0.75 : 0.1;


    
    ///// et structure matrix Ifor Shi-Tomasi by box filter the on gradients
    // // x^2
    // float a = 0.5 * (gx0 * gx0 + gx1 * gx1 + gx2 * gx2 + gx3 * gx3 + gx4 * gx4 + gx5 * gx5 + gx6 * gx6 + gx7 * gx7 + gx8 * gx8) * 0.11111;
    // // IxIy
    // float b = (gx0 * gy0 + gx1 * gy1 + gx2 * gy2 + gx3 * gy3 + gx4 * gy4 + gx5 * gy5 + gx6 * gy6 + gx7 * gy7 + gx8 * gy8) * 0.11111;
    // // Iy^2
    // float c = 0.5 * (gy0 * gy0 + gy1 * gy1 + gy2 * gy2 + gy3 * gy3 + gy4 * gy4 + gy5 * gy5 + gy6 * gy6 + gy7 * gy7 + gy8 * gy8) * 0.11111;

    // // Extract min eigenvalue. This falls out as solution to quadratic
    // float l1 = a + c - sqrt((a - c) * (a - c) + b * b); 

    float m = 0.1;
    // gl_FragColor = vec4(m, l1, 0.0, 0.0);
    gl_FragColor = vec4(0.0,0.0,0.0,0.0);
}
)";


const char *fs_suppress = R"(
#version 100
#define PI 3.14159265
precision mediump float;
precision mediump sampler2D;

varying vec2 tex_coord;
varying vec2 tc0, tc1, tc2, tc3, tc5, tc6, tc7, tc8;
uniform sampler2D tex;
uniform float corner_thr;
uniform float relax_suppress;
uniform float minval;
void main()
{
    vec4 p0 = texture2D(tex, tc0);
    vec4 p1 = texture2D(tex, tc1);
    vec4 p2 = texture2D(tex, tc2);
    vec4 p3 = texture2D(tex, tc3);
    vec4 p4 = texture2D(tex, tex_coord);
    vec4 p5 = texture2D(tex, tc5);
    vec4 p6 = texture2D(tex, tc6);
    vec4 p7 = texture2D(tex, tc7);
    vec4 p8 = texture2D(tex, tc8);

    // Component x is the gradient magnitude
    // Component y is the descretised angle of the gradient
    // Component z is gx
    // Component a is gy


    // We look at the pixels either side of the centre pixel (p4) in direction
    // of gradient and suppress if either are higher
    //  p0  p1  p2
    //  p3  p4  p5
    //  p6  p7  p8
    float angle = floor(p4.y * 4.0 + 0.5);
    float m = p4.x;
    float ml =      float(angle == 0.0) * p3.x
                +   float(angle == 1.0) * p6.x
                +   float(angle == 2.0) * p7.x
                +   float(angle == 3.0) * p8.x;
    float mr =      float(angle == 0.0) * p5.x
                +   float(angle == 1.0) * p2.x
                +   float(angle == 2.0) * p1.x
                +   float(angle == 3.0) * p0.x;
    // We don't care about having exactly thin edges, but we do want edges to be
    // continuous, adding a little constant here so that suppression is not so
    // aggressive
    m = float(max(ml, mr) <= m + relax_suppress) * m;


    // We get the structure matrix for Shi-Tomasi by doing a box filter on
    // the 3x3 pixels gradients.
    // Convert gradients back to proper values
    float gx0 = (p0.z - 0.5) * 5.0;
    float gx1 = (p1.z - 0.5) * 5.0;
    float gx2 = (p2.z - 0.5) * 5.0;
    float gx3 = (p3.z - 0.5) * 5.0;
    float gx4 = (p4.z - 0.5) * 5.0;
    float gx5 = (p5.z - 0.5) * 5.0;
    float gx6 = (p6.z - 0.5) * 5.0;
    float gx7 = (p7.z - 0.5) * 5.0;
    float gx8 = (p8.z - 0.5) * 5.0;
    float gy0 = (p0.a - 0.5) * 5.0;
    float gy1 = (p1.a - 0.5) * 5.0;
    float gy2 = (p2.a - 0.5) * 5.0;
    float gy3 = (p3.a - 0.5) * 5.0;
    float gy4 = (p4.a - 0.5) * 5.0;
    float gy5 = (p5.a - 0.5) * 5.0;
    float gy6 = (p6.a - 0.5) * 5.0;
    float gy7 = (p7.a - 0.5) * 5.0;
    float gy8 = (p8.a - 0.5) * 5.0;

    // Get structure matrix Ix^2
    float a = 0.5 * (gx0 * gx0 + gx1 * gx1 + gx2 * gx2 + gx3 * gx3 + gx4 * gx4 + gx5 * gx5 + gx6 * gx6 + gx7 * gx7 + gx8 * gx8) * 0.11111;
    // IxIy
    float b = (gx0 * gy0 + gx1 * gy1 + gx2 * gy2 + gx3 * gy3 + gx4 * gy4 + gx5 * gy5 + gx6 * gy6 + gx7 * gy7 + gx8 * gy8) * 0.11111;
    // Iy^2
    float c = 0.5 * (gy0 * gy0 + gy1 * gy1 + gy2 * gy2 + gy3 * gy3 + gy4 * gy4 + gy5 * gy5 + gy6 * gy6 + gy7 * gy7 + gy8 * gy8) * 0.11111;

    // Extract min eigenvalue. This falls out as solution to quadratic
    float l1 = a + c - sqrt((a - c) * (a - c) + b * b);


    bool edge = m > minval;
    float cnr = float(edge && (l1 > corner_thr));


    gl_FragColor = vec4(float(edge), 0.0, cnr, 0.0);


}
)";

const char *fs_threshold = R"(
#version 100
#define PI 3.14159265
precision mediump float;
precision mediump sampler2D;

varying vec2 tex_coord;
varying vec2 tc0, tc1, tc2, tc3, tc5, tc6, tc7, tc8;
uniform sampler2D tex;
uniform float minval;
uniform float maxval;
uniform float corner_thr;
void main()
{

    vec4 p0 = texture2D(tex, tc0);
    vec4 p1 = texture2D(tex, tc1);
    vec4 p2 = texture2D(tex, tc2);
    vec4 p3 = texture2D(tex, tc3);
    vec4 p4 = texture2D(tex, tex_coord);
    vec4 p5 = texture2D(tex, tc5);
    vec4 p6 = texture2D(tex, tc6);
    vec4 p7 = texture2D(tex, tc7);
    vec4 p8 = texture2D(tex, tc8);

    bool edge =     (p4.x > maxval)
                ||  (   (p4.x > minval)
                    &&  (   (p0.x > maxval)
                        ||  (p1.x > maxval)
                        ||  (p2.x > maxval)
                        ||  (p3.x > maxval)
                        ||  (p5.x > maxval)
                        ||  (p6.x > maxval)
                        ||  (p7.x > maxval)
                        ||  (p8.x > maxval)
                        )
                    );
    float col = float(p4.x > minval);



    float cnr = float(edge && (p4.y > corner_thr));
    // Out 
    // x   - edge
    // y,z - corner
    gl_FragColor = vec4(col, 0.0, cnr, 0.0);
}
)";
const char *fs_showcorners= R"(
#version 100
precision mediump float;
precision mediump sampler2D;
varying vec2 tex_coord;
uniform sampler2D tex;
void main()
{
    vec4 c = texture2D(tex, tex_coord);
    //float g = 0.4 * float(c.x > 0.0);
    float g = float((c.z > 0.0) && (c.x == 0.0));
    float r = float((c.z > 0.0) && (c.x > 0.0));
    float b = float((c.z == 0.0) && (c.x > 0.0));
    gl_FragColor = vec4(r, g, b, 1.0);
}
)";
const char *fs_showcontours= R"(
#version 100
precision mediump float;
precision mediump sampler2D;
varying vec2 tex_coord;
uniform sampler2D tex;
void main()
{
    vec4 c = texture2D(tex, tex_coord);

    
    float notcontour = float((c.y == 0.0) && (c.x == 1.0));
    float contour = (1.0 - notcontour) * float(c.x > 0.0);
    //float g = 0.2 * float((c.x > 0.0) || (c.y > 0.0));
    gl_FragColor = vec4(notcontour, contour, contour, 1.0);
}
)";
const char *fs_dilate = R"(
#version 100
#define PI 3.14159265
precision mediump float;
precision mediump sampler2D;

varying vec2 tex_coord;
varying vec2 tc0, tc1, tc2, tc3, tc5, tc6, tc7, tc8;
uniform sampler2D tex;
void main()
{

    vec4 p0 = texture2D(tex, tc0);
    vec4 p1 = texture2D(tex, tc1);
    vec4 p2 = texture2D(tex, tc2);
    vec4 p3 = texture2D(tex, tc3);
    vec4 p4 = texture2D(tex, tex_coord);
    vec4 p5 = texture2D(tex, tc5);
    vec4 p6 = texture2D(tex, tc6);
    vec4 p7 = texture2D(tex, tc7);
    vec4 p8 = texture2D(tex, tc8);

    // float col = max(
    //                 max(    max(max(p0.x, p1.x), max(p2.x, p3.x)),
    //                         max(max(p5.x, p6.x), max(p7.x, p8.x))),
    //                 p4.x
    //             );

    float col = float(  (p0.x > 0.0)
                    ||  (p1.x > 0.0)
                    ||  (p2.x > 0.0)
                    ||  (p3.x > 0.0)
                    ||  (p4.x > 0.0)
                    ||  (p5.x > 0.0)
                    ||  (p6.x > 0.0)
                    ||  (p7.x > 0.0)
                    ||  (p8.x > 0.0));


    gl_FragColor = vec4(col, p4.y, p4.y, 0.0);
}
)";

const char *fs_downscale = R"(
#version 100
precision highp float;
precision mediump sampler2D;
precision highp int;
varying vec2 tex_coord;
varying vec2 tc0, tc1, tc2, tc3, tc5, tc6, tc7, tc8;
uniform sampler2D tex;
uniform float hpix;
uniform float vpix;
void main()
{
    // We are downscaling by factor of two, use a 3x3 gaussian

    vec4 p0 = texture2D(tex, tc0);
    vec4 p1 = texture2D(tex, tc1);
    vec4 p2 = texture2D(tex, tc2);
    vec4 p3 = texture2D(tex, tc3);
    //vec4 p4 = texture2D(tex, tex_coord + vec2(hpix / 2.0, vpix / 2.0));
    vec4 p4 = texture2D(tex, tex_coord);
    vec4 p5 = texture2D(tex, tc5);
    vec4 p6 = texture2D(tex, tc6);
    vec4 p7 = texture2D(tex, tc7);
    vec4 p8 = texture2D(tex, tc8);

    vec4 p =    1.0 * p0 + 2.0 * p1 + 1.0 * p2 
            +   2.0 * p3 + 4.0 * p4 + 2.0 * p5
            +   1.0 * p6 + 2.0 * p7 + 1.0 * p8;
    gl_FragColor = p / 16.0;
    // vec4 p =             + 2.0 * p1  
    //         +   2.0 * p3 + 4.0 * p4 + 2.0 * p5
    //                      + 2.0 * p7 ;
    // gl_FragColor = p / 12.0;
    // gl_FragColor = p4;

}
)";

#endif