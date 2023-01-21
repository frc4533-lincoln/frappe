R"glsl(
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
    //m = float(max(ml, mr) <= m -0.1) * m;
    bool edge = m > minval;
    // if (!edge)
    // {
    //     // Early exit if there is no edge
    //     gl_FragColor = vec4(0.0);
    // }
    // else
    // {
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


        // // Get structure matrix Ix^2
        float a = 0.5 * (gx0 * gx0 + gx1 * gx1 + gx2 * gx2 + gx3 * gx3 + gx4 * gx4 + gx5 * gx5 + gx6 * gx6 + gx7 * gx7 + gx8 * gx8) * 0.11111;
        // IxIy
        float b =       (gx0 * gy0 + gx1 * gy1 + gx2 * gy2 + gx3 * gy3 + gx4 * gy4 + gx5 * gy5 + gx6 * gy6 + gx7 * gy7 + gx8 * gy8) * 0.11111;
        // Iy^2
        float c = 0.5 * (gy0 * gy0 + gy1 * gy1 + gy2 * gy2 + gy3 * gy3 + gy4 * gy4 + gy5 * gy5 + gy6 * gy6 + gy7 * gy7 + gy8 * gy8) * 0.11111;

        // vec4 pz0 = vec4(p0.z, p1.z, p2.z, p3.z);
        // vec4 pz4 = vec4(p4.z, p5.z, p6.z, p7.z);
        // vec4 pa0 = vec4(p0.a, p1.a, p2.a, p3.a);
        // vec4 pa4 = vec4(p4.a, p5.a, p6.a, p7.a);

        // vec4 gx0 = (pz0 - 0.5) * 5.0;
        // vec4 gx4 = (pz4 - 0.5) * 5.0;
        // vec4 gy0 = (pa0 - 0.5) * 5.0;
        // vec4 gy4 = (pa4 - 0.5) * 5.0;

        // float a = 0.5 * (dot(gx0, gx0) + dot(gx4, gx4) + gx8 * gx8) * 0.11111;
        // float b =       (dot(gx0, gy0) + dot(gx4, gy4) + gx8 * gy8) * 0.11111;
        // float c = 0.5 * (dot(gy0, gy0) + dot(gy4, gy4) + gy8 * gy8) * 0.11111;



        // Extract min eigenvalue. This falls out as solution to quadratic
        float l1 = a + c - sqrt((a - c) * (a - c) + b * b);


        float cnr = float(edge && (l1 > corner_thr));

        gl_FragColor = vec4(float(edge), 0.0, cnr, 0.0);
    // }

}
)glsl"
