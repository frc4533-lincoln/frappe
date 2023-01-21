R"glsl(
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
)glsl"

