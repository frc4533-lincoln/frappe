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
