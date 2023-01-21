R"glsl(
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
)glsl"


