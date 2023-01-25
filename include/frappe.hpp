#ifndef frappe_hpp
#define frappe_hpp



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

typedef std::vector<Marker> MarkerVec;




#endif