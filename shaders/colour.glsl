R"glsl(
#version 100
precision highp float;
uniform vec4 colour;
void main()
{
    gl_FragColor = colour;
}
)glsl"
