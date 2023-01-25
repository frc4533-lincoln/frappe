
#ifndef detector_hpp
#define detector_hpp

#include "glhelpers.hpp"
#include <vector>



struct Point2f
{
    float x, y;
};
struct Box
{
    Point2f     p[4];
};
typedef std::vector<Box> BoxVec;


class Marker : public std::vector<Point2f>
{
public:
    Marker()
    :   p(4), 
        id(-1)
    {}
    Marker(const std::vector<Point2f>& _p, int _id = -1)
    :   p(_p)
    {

    }
    int                     id;
    int                     dist;
    float                   sad;
    std::vector<Point2f>        p;
    std::vector<cv::Point2f>    cp;
    cv::Mat                 rvec;
    cv::Mat                 tvec;

    // Make it easy to sort into ID order
    bool operator < (const Marker &str) { return (id < str.id); }
};


class Camera
{
public:
    Camera(float c[], float d[])
    :   camera_matrix(3, 3, c),
        dist_coeffs(1, 5, d)
    {

    }
    Camera()
    : camera_matrix(3, 3) {}

    cv::Mat1f camera_matrix;
    cv::Mat1f dist_coeffs;

};

typedef std::vector<Marker> MarkerVec;

void union_initial_init();

uint32_t cpu_connect(Texture &buf, MarkerVec &mv, Texture &t_mask);
uint32_t connected_components(Texture &buf, MarkerVec &mv);
template <class T>
uint32_t find_borders(Texture &buf, MarkerVec &mv, T &t_mask, float scale_factor = 1.0);

void sort_points_clockwise(Marker &p);
void refine_corners(Texture &buf, BoxVec &bv, float alpha);
void find_contours(Texture &buf, MarkerVec &mv);
void make_mask(Texture &buf, char *mask);

int check_candidate(int16_t **cpoints, int corners, MarkerVec &mv, int16_t *org, int step);

namespace cv{
void localcornerSubPix( InputArray _image, InputOutputArray _corners,
                       Size win, Size zeroZone, TermCriteria criteria );
}


class Dictionary;

class Detector
{
public:
    Detector(State &_state, int _width, int _height, int tc=10, int tr=15, int tw=64, int th=32);
    // Just detect the markers
    std::vector<Marker> detect(Texture &image);

    // Detect markers and display diagnostics on output texture
    std::vector<Marker> detect(Texture &image, Texture &output);

    void calculate_extrinsics(float size);
    void render(bool axes);
    cv::Mat render_to_mat(Texture &input);

    struct Params
    {
        float   relax_suppress      = 0.0;
        float   corner_thr          = 0.19;
        bool    corner_refine       = true;
        float   beta                = 28.0;
        bool    scale_adapt         = false;
        float   scale_factor        = 1.0;
        int     max_scaled_frames   = 5;
        int     max_hamming         = 5;
        float   ada_thresh          = 0.0;
        float   edge_minval         = 0.27;
        float   edge_maxval         = 0.2;
        float   xo                  = 0.0;
        float   yo                  = 0.0;
        int     boxidx              = 0;
        float   alpha               = 0.58;
        float   lratio              = 4.0;
        int     xw                  = 140;
        int     yw                  = 100;
    };

    enum VPU_funcs
    {
        VPU_MAKE_MASK       = 0,
        VPU_MAKE_BITS       = 1,
        VPU_DECODE          = 2,
        VPU_EXTRACT_CHANNEL = 3,
        VPU_LOAD_CODE       = 4,
        VPU_SCALE           = 5,
        VPU_DECODE_ALL      = 6,
        VPU_BANDWIDTH       = 7,
        VPU_CAM_Y_COPY      = 8,
    };


    Params      params;
    Camera      camera;
    int         times[16] = {0};
    State       &state;
    Texture     t_buffer[2];
    Texture     t_fid;
    Texture     t_output;
    Texture     t_image;
    Texture     t_mask;
    //Texture     t_atlas;
    Texture     t_codes;
    Texture     t_decode;
    int         width, height;
    Program     *p_shi_tomasi;
    Program     *p_suppress;
    Program     *p_null;
    Program     *p_showcorners;
    Program     *p_showcontours;
    Program     *p_camera;
    Program     *p_colour;
    QPUprogram  *p_qpu_blit;
    QPUprogram  *p_qpu_shi_tomasi;
    QPUprogram  *p_qpu_shi_tomasi_scale;
    QPUprogram  *p_qpu_suppress;
    QPUprogram  *p_qpu_warp;
    VPUprogram  *p_vpu_functions;
    Mesh        m_fid;
    Mesh        *m_quad;
    Mesh        *m_quad_scale;
    MarkerVec   mv;
    MarkerVec   mv_pre_elim;
    bool        tui;
    bool        safe;
    bool        gl;
    bool        grab;
    bool        nextract;
    int         scale_mode;
    Dictionary  *dict;
    int         showpass;
    int         draw_fid;
    int         draw_atlas;
    float       input_width;
    float       input_height;
    float       min_length;
    float       min_length_candidate;
    std::vector<float>    sfactors = {
        0.25f, 
        1.0f/3.0f, 
        0.5f, 
        2.0f/3.0f, 
        0.75f, 
        1.0f
    };
    std::string stages[16] = {
        "uniforms",
        "grads angles",
        "edges corners",
        "make mask",
        "connect",
        "refine corners",
        "extract",
        "decode",
        "",
        "",
        "",
        "",
        "",
        "",
        "render",
        "capture"
    };

private:
    const int maxlimit      = 32;
    int gl_extract_thresh;
    bool y_flip;
    int scaled_frames;
    void extract(Texture &buf);
    void extract2(Texture &buf);
    void decode();
    void decode_hamming();
    void decode_vpu();
    void decode_qpu();
    void refine_corners(Texture &buf);
    void eliminate_unsquare();
    void eliminate_invalid();
    void do_render();

    void textwin(Texture &buf);
    Timer main_timer, aux_timer;
};



class Dictionary
{
public:
    Dictionary(Texture &t_codes)
    {
        // Turn codes into map
        for (int i = 0; i < codes.size(); i++)
        {   
            cmap.insert({codes[i], i});
        }

        uint64_t *cdata = (uint64_t*)t_codes.user_lock();
        for(int c = 0; c < codes.size(); c++)
        {
            cdata[c] = codes[c];
        }
        t_codes.user_unlock();


    }
    int hamming(uint64_t *bits, int maxdist, int *rdist)
    {
        // Return the code with the lowest hamming distance from any of the four orientations
        int dist = 36;
        int idx = 0;
        for (int i = 0; i < codes.size(); i++)
        {
            for(int j = 0; j < 4; j++)
            {
                int d = popcount(codes[i] ^ bits[j]);
                if (d < dist) 
                {
                    dist = d;
                    idx = i;
                }
            }
        }
        *rdist = dist;   
        return dist <= maxdist ? idx : -1;
    }
    std::map<uint64_t, uint8_t> cmap;


private:

    int popcount(uint64_t x)
    {
        const int bitcount[] = {
            0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
            1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
            1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
            2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
            1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
            2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
            2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
            3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
            1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
            2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
            2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
            3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
            2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
            3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
            3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
            4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
        };
        // We only need to count up to the 35th bit
        return bitcount[x & 0xff] 
                + bitcount[(x >> 8) & 0xff]
                + bitcount[(x >> 16) & 0xff]
                + bitcount[(x >> 24) & 0xff]
                + bitcount[(x >> 32) & 0xff];
    }

    // Set of codes for the ArUco dictionary 36h12. Replicate the the last 5 codes to make 256,
    // so we don't get, e.g. zero memory being detected in the VPU decoder as valid
    std::vector<uint64_t> codes = 
    {   
        0xd2b63a09dUL,0x6001134e5UL,0x1206fbe72UL,0xff8ad6cb4UL,0x85da9bc49UL,0xb461afe9cUL,0x6db51fe13UL,0x5248c541fUL,    // 0
        0x8f34503UL,  0x8ea462eceUL,0xeac2be76dUL,0x1af615c44UL,0xb48a49f27UL,0x2e4e1283bUL,0x78b1f2fa8UL,0x27d34f57eUL,    // 8
        0x89222fff1UL,0x4c1669406UL,0xbf49b3511UL,0xdc191cd5dUL,0x11d7c3f85UL,0x16a130e35UL,0xe29f27effUL,0x428d8ae0cUL,    // 16
        0x90d548477UL,0x2319cbc93UL,0xc3b0c3dfcUL,0x424bccc9UL, 0x2a081d630UL,0x762743d96UL,0xd0645bf19UL,0xf38d7fd60UL,    // 24
        0xc6cbf9a10UL,0x3c1be7c65UL,0x276f75e63UL,0x4490a3f63UL,0xda60acd52UL,0x3cc68df59UL,0xab46f9daeUL,0x88d533d78UL,    // 32
        0xb6d62ec21UL,0xb3c02b646UL,0x22e56d408UL,0xac5f5770aUL,0xaaa993f66UL,0x4caa07c8dUL,0x5c9b4f7b0UL,0xaa9ef0e05UL,    // 40
        0x705c5750UL, 0xac81f545eUL,0x735b91e74UL,0x8cc35cee4UL,0xe44694d04UL,0xb5e121de0UL,0x261017d0fUL,0xf1d439eb5UL,    // 48
        0xa1a33ac96UL,0x174c62c02UL,0x1ee27f716UL,0x8b1c5ece9UL,0x6a05b0c6aUL,0xd0568dfcUL, 0x192d25e5fUL,0x1adbeccc8UL,    // 56
        0xcfec87f00UL,0xd0b9dde7aUL,0x88dcef81eUL,0x445681cb9UL,0xdbb2ffc83UL,0xa48d96df1UL,0xb72cc2e7dUL,0xc295b53fUL,     // 64
        0xf49832704UL,0x9968edc29UL,0x9e4e1af85UL,0x8683e2d1bUL,0x810b45c04UL,0x6ac44bfe2UL,0x645346615UL,0x3990bd598UL,    // 72
        0x1c9ed0f6aUL,0xc26729d65UL,0x83993f795UL,0x3ac05ac5dUL,0x357adff3bUL,0xd5c05565UL, 0x2f547ef44UL,0x86c115041UL,    // 80
        0x640fd9e5fUL,0xce08bbcf7UL,0x109bb343eUL,0xc21435c92UL,0x35b4dfce4UL,0x459752cf2UL,0xec915b82cUL,0x51881eed0UL,    // 88
        0x2dda7dc97UL,0x2e0142144UL,0x42e890f99UL,0x9a8856527UL,0x8e80d9d80UL,0x891cbcf34UL,0x25dd82410UL,0x239551d34UL,    // 96
        0x8fe8f0c70UL,0x94106a970UL,0x82609b40cUL,0xfc9caf36UL, 0x688181d11UL,0x718613c08UL,0xf1ab7629UL, 0xa357bfc18UL,    // 104
        0x4c03b7a46UL,0x204dedce6UL,0xad6300d37UL,0x84cc4cd09UL,0x42160e5c4UL,0x87d2adfa8UL,0x7850e7749UL,0x4e750fc7cUL,    // 112
        0xbf2e5dfdaUL,0xd88324da5UL,0x234b52f80UL,0x378204514UL,0xabdf2ad53UL,0x365e78ef9UL,0x49caa6ca2UL,0x3c39ddf3UL,     // 120
        0xc68c5385dUL,0x5bfcbbf67UL,0x623241e21UL,0xabc90d5ccUL,0x388c6fe85UL,0xda0e2d62dUL,0x10855dfe9UL,0x4d46efd6bUL,    // 128
        0x76ea12d61UL,0x9db377d3dUL,0xeed0efa71UL,0xe6ec3ae2fUL,0x441faee83UL,0xba19c8ff5UL,0x313035eabUL,0x6ce8f7625UL,    // 136
        0x880dab58dUL,0x8d3409e0dUL,0x2be92ee21UL,0xd60302c6cUL,0x469ffc724UL,0x87eebeed3UL,0x42587ef7aUL,0x7a8cc4e52UL,    // 144
        0x76a437650UL,0x999e41ef4UL,0x7d0969e42UL,0xc02baf46bUL,0x9259f3e47UL,0x2116a1dc0UL,0x9f2de4d84UL,0xeffac29UL,      // 152
        0x7b371ff8cUL,0x668339da9UL,0xd010aee3fUL,0x1cd00b4c0UL,0x95070fc3bUL,0xf84c9a770UL,0x38f863d76UL,0x3646ff045UL,    // 160
        0xce1b96412UL,0x7a5d45da8UL,0x14e00ef6cUL,0x5e95abfd8UL,0xb2e9cb729UL,0x36c47dd7UL, 0xb8ee97c6bUL,0xe9e8f657UL,     // 168
        0xd4ad2ef1aUL,0x8811c7f32UL,0x47bde7c31UL,0x3adadfb64UL,0x6e5b28574UL,0x33e67cd91UL,0x2ab9fdd2dUL,0x8afa67f2bUL,    // 176
        0xe6a28fc5eUL,0x72049cdbdUL,0xae65dac12UL,0x1251a4526UL,0x1089ab841UL,0xe2f096ee0UL,0xb0caee573UL,0xfd6677e86UL,    // 184
        0x444b3f518UL,0xbe8b3a56aUL,0x680a75cfcUL,0xac02baea8UL,0x97d815e1cUL,0x1d4386e08UL,0x1a14f5b0eUL,0xe658a8d81UL,    // 192
        0xa3868efa7UL,0x3668a9673UL,0xe8fc53d85UL,0x2e2b7edd5UL,0x8b2470f13UL,0xf69795f32UL,0x4589ffc8eUL,0x2e2080c9cUL,    // 200
        0x64265f7dUL, 0x3d714dd10UL,0x1692c6ef1UL,0x3e67f2f49UL,0x5041dad63UL,0x1a1503415UL,0x64c18c742UL,0xa72eec35UL,     // 208
        0x1f0f9dc60UL,0xa9559bc67UL,0xf32911d0dUL,0x21c0d4ffcUL,0xe01cef5b0UL,0x4e23a3520UL,0xaa4f04e49UL,0xe1c4fcc43UL,    // 216
        0x208e8f6e8UL,0x8486774a5UL,0x9e98c7558UL,0x2c59fb7dcUL,0x9446a4613UL,0x8292dcc2eUL,0x4d61631UL,  0xd05527809UL,    // 224
        0xa0163852dUL,0x8f657f639UL,0xcca6c3e37UL,0xcb136bc7aUL,0xfc5a83e53UL,0x9aa44fc30UL,0xbdec1bd3cUL,0xe020b9f7cUL,    // 232
        0x4b8f35fb0UL,0xb8165f637UL,0x33dc88d69UL,0x10a2f7e4dUL,0xc8cb5ff53UL,0xde259ff6bUL,0x46d070dd4UL,0x32d3b9741UL,    // 240
        0x7075f1c04UL,0x4d58dbea0UL,0x4d58dbea0UL,0x4d58dbea0UL,0x4d58dbea0UL,0x4d58dbea0UL,0x4d58dbea0UL,0x4d58dbea0UL     // 248
    }; 

};



#endif