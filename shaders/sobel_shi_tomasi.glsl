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

