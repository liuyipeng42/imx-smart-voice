#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "jpeglib.h"
#include "v4l2_camera.h"

#define CLAMP(x) (((x) < 0) ? 0 : (((x) > 255) ? 255 : (x)))

V4L2Camera::V4L2Camera() : v4l2_fd_(-1), rgb_buffer_(nullptr) {
    Init();
}

int V4L2Camera::xioctl(int fd, int request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

void V4L2Camera::YUYVToRGB24Integer(const unsigned char* yuyv, unsigned char* rgb, int width, int height) {
    int y0, u, y1, v, r, g, b, C, D, E;
    const unsigned char* yuv_ptr = yuyv;
    for (int j = height - 1; j >= 0; --j) {
        unsigned char* rgb_row_ptr = rgb + j * width * 3;
        for (int i = 0; i < width; i += 2) {
            y0 = yuv_ptr[0];
            u = yuv_ptr[1];
            y1 = yuv_ptr[2];
            v = yuv_ptr[3];
            yuv_ptr += 4;

            C = y0 - 16;
            D = u - 128;
            E = v - 128;
            r = CLAMP((298 * C + 409 * E + 128) >> 8);
            g = CLAMP((298 * C - 100 * D - 208 * E + 128) >> 8);
            b = CLAMP((298 * C + 516 * D + 128) >> 8);
            *rgb_row_ptr++ = r;
            *rgb_row_ptr++ = g;
            *rgb_row_ptr++ = b;

            C = y1 - 16;
            r = CLAMP((298 * C + 409 * E + 128) >> 8);
            g = CLAMP((298 * C - 100 * D - 208 * E + 128) >> 8);
            b = CLAMP((298 * C + 516 * D + 128) >> 8);
            *rgb_row_ptr++ = r;
            *rgb_row_ptr++ = g;
            *rgb_row_ptr++ = b;
        }
    }
}

int V4L2Camera::SaveAsJpeg(const std::string& filename,
                           const unsigned char* rgb_data,
                           int width,
                           int height,
                           int quality) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE* outfile = nullptr;
    JSAMPROW row_pointer[1];
    int ret = 0;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    if (!(outfile = fopen(filename.c_str(), "wb"))) {
        std::cerr << "Failed to open " << filename << " for writing: " << strerror(errno) << "\n";
        jpeg_destroy_compress(&cinfo);
        return -1;
    }

    jpeg_stdio_dest(&cinfo, outfile);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = (JSAMPROW)&rgb_data[cinfo.next_scanline * width * 3];
        if (jpeg_write_scanlines(&cinfo, row_pointer, 1) != 1) {
            std::cerr << "jpeg_write_scanlines failed\n";
            ret = -1;
            break;
        }
    }

    if (ret == 0)
        jpeg_finish_compress(&cinfo);
    else
        jpeg_abort_compress(&cinfo);

    jpeg_destroy_compress(&cinfo);
    fclose(outfile);
    return ret;
}

bool V4L2Camera::Init() {
    v4l2_fd_ = open("/dev/video0", O_RDWR | O_NONBLOCK);
    if (v4l2_fd_ < 0) {
        perror("open");
        return false;
    }

    memset(&fmt_, 0, sizeof(fmt_));
    fmt_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt_.fmt.pix.width = DEFAULT_WIDTH;
    fmt_.fmt.pix.height = DEFAULT_HEIGHT;
    fmt_.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt_.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (xioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt_) < 0) {
        perror("VIDIOC_S_FMT");
        close(v4l2_fd_);
        return false;
    }

    v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = FRAMEBUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close(v4l2_fd_);
        return false;
    }

    for (int i = 0; i < FRAMEBUFFER_COUNT; ++i) {
        v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(v4l2_fd_);
            return false;
        }

        buf_infos_[i].length = buf.length;
        buf_infos_[i].start = static_cast<unsigned char*>(
            mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd_, buf.m.offset));
        if (buf_infos_[i].start == MAP_FAILED) {
            perror("mmap");
            close(v4l2_fd_);
            return false;
        }

        if (xioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            close(v4l2_fd_);
            return false;
        }
    }

    buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(v4l2_fd_, VIDIOC_STREAMON, &buf_type_) < 0) {
        perror("VIDIOC_STREAMON");
        close(v4l2_fd_);
        return false;
    }

    rgb_buffer_ = new unsigned char[DEFAULT_WIDTH * DEFAULT_HEIGHT * 3];
    return true;
}

void V4L2Camera::CleanUp() {
    for (int i = 0; i < FRAMEBUFFER_COUNT; ++i) {
        if (buf_infos_[i].start) {
            munmap(buf_infos_[i].start, buf_infos_[i].length);
        }
    }

    if (v4l2_fd_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &type);
        close(v4l2_fd_);
        v4l2_fd_ = -1;
    }

    delete[] rgb_buffer_;
    rgb_buffer_ = nullptr;
}

bool V4L2Camera::Capture(const std::string& filename, int quality) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(v4l2_fd_, &fds);
    timeval tv = {2, 0};

    if (select(v4l2_fd_ + 1, &fds, NULL, NULL, &tv) == -1) {
        perror("select");
        return false;
    }

    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return false;
    }

    YUYVToRGB24Integer(buf_infos_[buf.index].start, rgb_buffer_, fmt_.fmt.pix.width, fmt_.fmt.pix.height);
    bool success = (SaveAsJpeg(filename, rgb_buffer_, fmt_.fmt.pix.width, fmt_.fmt.pix.height, quality) == 0);

    if (xioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
    }

    return success;
}
