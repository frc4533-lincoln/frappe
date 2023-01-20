#version 100
precision mediump float;

attribute vec4 v_pos;
void main()
{
    gl_Position = v_pos;
}
