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
