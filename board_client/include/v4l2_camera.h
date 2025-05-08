#ifndef V4L2_CAMERA_HPP
#define V4L2_CAMERA_HPP

#include <linux/videodev2.h>
#include <cstddef>
#include <string>

class V4L2Camera {
    struct CamBufInfo {
        unsigned char* start = nullptr;
        size_t length = 0;
    };

    static constexpr int FRAMEBUFFER_COUNT = 3;
    static constexpr int DEFAULT_WIDTH = 640;
    static constexpr int DEFAULT_HEIGHT = 480;

    int v4l2_fd_;
    CamBufInfo buf_infos_[FRAMEBUFFER_COUNT];
    v4l2_buf_type buf_type_;
    v4l2_format fmt_;
    unsigned char* rgb_buffer_;

   public:
    V4L2Camera();
    bool Capture(const std::string& filename, int quality = 85);
    void CleanUp();

   private:
    int xioctl(int fd, int request, void* arg);
    void YUYVToRGB24Integer(const unsigned char* yuyv, unsigned char* rgb, int width, int height);
    int SaveAsJpeg(const std::string& filename,
                   const unsigned char* rgb_data,
                   int width,
                   int height,
                   int quality);
    bool Init();
};

#endif  // V4L2_CAMERA_HPP
