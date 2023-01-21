R"glsl(
#version 100
precision mediump float;
precision mediump sampler2D;
varying vec2 tex_coord;
uniform sampler2D tex;
void main()
{
    gl_FragColor = vec4(texture2D(tex, tex_coord).xyz, 1.0);
}
)glsl"

