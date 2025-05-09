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

// 宏定义，用于将数值限制在 0 到 255 之间
#define CLAMP(x) (((x) < 0) ? 0 : (((x) > 255) ? 255 : (x)))

V4L2Camera::V4L2Camera()
    // 成员初始化列表
    // 初始化文件描述符为无效值
    : v4l2_fd_(-1),
      // 初始化 RGB 缓冲区指针为空
      rgb_buffer_(nullptr),
      // 初始化初始化状态为 false
      is_initialized_(false),
      // 初始化实际宽度和高度为 0
      actual_width_(0),
      actual_height_(0) {
    // 调用 Init() 方法执行实际的初始化过程，并将结果存储到 is_initialized_ 成员变量
    is_initialized_ = Init();
}

V4L2Camera::~V4L2Camera() {
    // 调用 CleanUp() 方法释放所有资源
    CleanUp();
}

// ioctl 系统调用的包装函数实现
// 循环调用 ioctl，直到成功或遇到非 EINTR 错误
int V4L2Camera::xioctl(int fd, int request, void* arg) {
    int r;
    do {
        // 调用 ioctl
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);  // 如果 ioctl 返回 -1 且错误码是 EINTR (被信号中断)，则重试
    return r;  // 返回 ioctl 的结果
}

// YUYV 到 RGB24 颜色空间转换函数实现
// YUYV 格式是 packed 格式，每 4 个字节代表两个像素 (Y0 U Y1 V)
void V4L2Camera::YUYVToRGB24(const unsigned char* yuyv, unsigned char* rgb, int width, int height) {
    const unsigned char* yuv_ptr = yuyv;  // YUYV 数据指针
    // 遍历每一行
    for (int j = 0; j < height; ++j) {
        // 计算当前行 RGB 数据在输出缓冲区中的起始地址
        unsigned char* rgb_row_ptr = rgb + j * width * 3;
        // 遍历每一行的像素对 (YUYV 格式每 4 字节处理两个像素)
        for (int i = 0; i < width; i += 2) {
            // 提取当前像素对的 Y0, U, Y1, V 值
            int y0 = yuv_ptr[0];
            int u = yuv_ptr[1];
            int y1 = yuv_ptr[2];
            int v = yuv_ptr[3];
            yuv_ptr += 4;  // 移动 YUYV 指针到下一个像素对

            // 计算第一个像素 (Y0) 的 RGB 值
            int c = y0 - 16;  // Y 分量减去偏移量
            int d = u - 128;  // U 分量减去偏移量
            int e = v - 128;  // V 分量减去偏移量

            // 应用 YUV 到 RGB 转换公式 (ITU-R BT.601 标准)
            // 并使用 CLAMP 宏将结果限制在 0-255 范围
            rgb_row_ptr[0] = CLAMP((298 * c + 409 * e + 128) >> 8);            // R
            rgb_row_ptr[1] = CLAMP((298 * c - 100 * d - 208 * e + 128) >> 8);  // G
            rgb_row_ptr[2] = CLAMP((298 * c + 516 * d + 128) >> 8);            // B
            rgb_row_ptr += 3;  // 移动 RGB 指针到下一个像素位置

            // 计算第二个像素 (Y1) 的 RGB 值 (U 和 V 分量与第一个像素共用)
            c = y1 - 16;  // Y1 分量减去偏移量
            // 应用 YUV 到 RGB 转换公式
            rgb_row_ptr[0] = CLAMP((298 * c + 409 * e + 128) >> 8);
            rgb_row_ptr[1] = CLAMP((298 * c - 100 * d - 208 * e + 128) >> 8);
            rgb_row_ptr[2] = CLAMP((298 * c + 516 * d + 128) >> 8);
            rgb_row_ptr += 3;  // 移动 RGB 指针到下一个像素位置
        }
    }
}

int V4L2Camera::SaveAsJpeg(const std::string& filename,
                           const unsigned char* rgb_data,
                           int width,
                           int height,
                           int quality) {
    // 声明 libjpeg 压缩对象和错误处理对象
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE* outfile = nullptr;  // 输出文件指针
    JSAMPROW row_pointer[1];  // 用于存储一行像素数据的指针数组
    int ret = 0;              // 返回值，0 表示成功

    // 设置 libjpeg 的标准错误处理
    cinfo.err = jpeg_std_error(&jerr);
    // 创建压缩对象
    jpeg_create_compress(&cinfo);

    // 打开输出文件，以二进制写入模式
    if (!(outfile = fopen(filename.c_str(), "wb"))) {
        // 文件打开失败，打印错误信息
        std::cerr << "Failed to open " << filename << " for writing: " << strerror(errno) << "\n";
        // 销毁压缩对象
        jpeg_destroy_compress(&cinfo);
        return -1;  // 返回失败
    }

    // 将压缩输出目标设置为文件流
    jpeg_stdio_dest(&cinfo, outfile);

    // 设置图像参数
    cinfo.image_width = width;       // 图像宽度
    cinfo.image_height = height;     // 图像高度
    cinfo.input_components = 3;      // 输入组件数量 (RGB 为 3)
    cinfo.in_color_space = JCS_RGB;  // 输入颜色空间为 RGB

    // 设置默认的压缩参数
    jpeg_set_defaults(&cinfo);
    // 设置压缩质量
    jpeg_set_quality(&cinfo, quality, TRUE);  // TRUE 表示使用标准缩放因子

    // 开始压缩
    jpeg_start_compress(&cinfo, TRUE);  // TRUE 表示写入标准 JPEG 头部

    // 逐行写入像素数据
    while (cinfo.next_scanline < cinfo.image_height) {
        // 设置当前要写入的行指针
        row_pointer[0] = (JSAMPROW)&rgb_data[cinfo.next_scanline * width * 3];
        // 写入一行数据
        if (jpeg_write_scanlines(&cinfo, row_pointer, 1) != 1) {
            // 写入失败，打印错误信息
            std::cerr << "jpeg_write_scanlines failed\n";
            ret = -1;  // 设置返回值为失败
            break;     // 退出循环
        }
    }

    // 根据写入结果完成或中止压缩
    if (ret == 0)
        jpeg_finish_compress(&cinfo);  // 成功完成压缩
    else
        jpeg_abort_compress(&cinfo);  // 中止压缩

    // 销毁压缩对象，释放内存
    jpeg_destroy_compress(&cinfo);
    // 关闭文件
    fclose(outfile);

    return ret;  // 返回结果 (0 或 -1)
}

bool V4L2Camera::Init() {
    // 打开摄像头设备文件，使用阻塞模式 (O_RDWR)
    v4l2_fd_ = open("/dev/video0", O_RDWR);
    if (v4l2_fd_ < 0) {
        // 打开失败，打印错误信息
        perror("open");
        return false;  // 初始化失败
    }

    // 查询并设置视频格式
    memset(&fmt_, 0, sizeof(fmt_));                // 清零格式结构体
    fmt_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;       // 设置缓冲区类型为视频捕获
    fmt_.fmt.pix.width = DEFAULT_WIDTH;            // 请求设置宽度
    fmt_.fmt.pix.height = DEFAULT_HEIGHT;          // 请求设置高度
    fmt_.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;  // 请求设置像素格式为 YUYV
    fmt_.fmt.pix.field = V4L2_FIELD_INTERLACED;  // 请求设置场序为隔行扫描 (通常摄像头支持 PROGRESSIVE 或 ANY)

    // 使用 VIDIOC_S_FMT 设置格式
    if (xioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt_) < 0) {
        // 设置格式失败，打印错误信息
        perror("VIDIOC_S_FMT");
        close(v4l2_fd_);  // 关闭文件描述符
        v4l2_fd_ = -1;    // 设置为无效值
        return false;     // 初始化失败
    }

    // 记录驱动实际设置的宽度和高度（可能与请求的不同）
    actual_width_ = fmt_.fmt.pix.width;
    actual_height_ = fmt_.fmt.pix.height;

    // 申请缓冲区
    v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));            // 清零请求缓冲区结构体
    req.count = FRAMEBUFFER_COUNT;           // 设置希望申请的缓冲区数量
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置缓冲区类型
    req.memory = V4L2_MEMORY_MMAP;           // 设置内存类型为内存映射 (mmap)

    // 使用 VIDIOC_REQBUFS 申请缓冲区
    if (xioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        // 申请缓冲区失败，打印错误信息
        perror("VIDIOC_REQBUFS");
        close(v4l2_fd_);  // 关闭文件描述符
        v4l2_fd_ = -1;
        return false;  // 初始化失败
    }

    // 映射缓冲区到用户空间
    int mapped_count = 0;  // 记录已成功映射的缓冲区数量
    for (int i = 0; i < FRAMEBUFFER_COUNT; ++i) {
        v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));            // 清零缓冲区信息结构体
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置缓冲区类型
        buf.memory = V4L2_MEMORY_MMAP;           // 设置内存类型
        buf.index = i;                           // 设置要查询的缓冲区索引

        // 使用 VIDIOC_QUERYBUF 查询缓冲区信息（如偏移量和长度）
        if (xioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            // 查询失败，打印错误信息
            perror("VIDIOC_QUERYBUF");
            // 清理已成功映射的缓冲区
            for (int j = 0; j < mapped_count; ++j) {
                if (buf_infos_[j].start) {  // 检查指针是否有效
                    munmap(buf_infos_[j].start, buf_infos_[j].length);
                    buf_infos_[j].start = nullptr;
                }
            }
            close(v4l2_fd_);  // 关闭文件描述符
            v4l2_fd_ = -1;
            return false;  // 初始化失败
        }

        // 存储查询到的缓冲区长度
        buf_infos_[i].length = buf.length;
        // 使用 mmap 将内核空间的缓冲区映射到用户空间
        buf_infos_[i].start = static_cast<unsigned char*>(mmap(nullptr,                 // 让系统选择映射地址
                                                               buf.length,              // 映射长度
                                                               PROT_READ | PROT_WRITE,  // 映射区域可读写
                                                               MAP_SHARED,              // 映射区域可共享
                                                               v4l2_fd_,                // 文件描述符
                                                               buf.m.offset));  // 缓冲区在设备内存中的偏移量

        if (buf_infos_[i].start == MAP_FAILED) {
            // 映射失败，打印错误信息
            perror("mmap");
            // 清理已成功映射的缓冲区
            for (int j = 0; j < mapped_count; ++j) {
                if (buf_infos_[j].start) {
                    munmap(buf_infos_[j].start, buf_infos_[j].length);
                    buf_infos_[j].start = nullptr;
                }
            }
            close(v4l2_fd_);  // 关闭文件描述符
            v4l2_fd_ = -1;
            return false;  // 初始化失败
        }
        mapped_count++;  // 增加成功映射的计数

        // 使用 VIDIOC_QBUF 将缓冲区放入队列，使其可用于捕获
        if (xioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
            // 入队失败，打印错误信息
            perror("VIDIOC_QBUF");
            // 清理所有已映射的缓冲区
            for (int j = 0; j < mapped_count; ++j) {
                if (buf_infos_[j].start) {
                    munmap(buf_infos_[j].start, buf_infos_[j].length);
                    buf_infos_[j].start = nullptr;
                }
            }
            close(v4l2_fd_);  // 关闭文件描述符
            v4l2_fd_ = -1;
            return false;  // 初始化失败
        }
    }

    // 开启视频流
    buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置要启动的流类型
    if (xioctl(v4l2_fd_, VIDIOC_STREAMON, &buf_type_) < 0) {
        // 启动流失败，打印错误信息
        perror("VIDIOC_STREAMON");
        // 清理所有已映射的缓冲区
        for (int j = 0; j < FRAMEBUFFER_COUNT; ++j) {
            if (buf_infos_[j].start) {
                munmap(buf_infos_[j].start, buf_infos_[j].length);
                buf_infos_[j].start = nullptr;
            }
        }
        close(v4l2_fd_);  // 关闭文件描述符
        v4l2_fd_ = -1;
        return false;  // 初始化失败
    }

    // 分配用于存储 RGB 图像数据的缓冲区
    rgb_buffer_ = new unsigned char[actual_width_ * actual_height_ * 3];  // RGB24 每个像素占 3 字节

    // 初始化成功
    return true;
}

void V4L2Camera::CleanUp() {
    // 遍历并解除所有已映射的缓冲区
    for (int i = 0; i < FRAMEBUFFER_COUNT; ++i) {
        if (buf_infos_[i].start) {                              // 检查缓冲区是否已被映射
            munmap(buf_infos_[i].start, buf_infos_[i].length);  // 解除映射
            buf_infos_[i].start = nullptr;                      // 将指针置空
        }
    }

    // 如果文件描述符有效，则停止视频流并关闭设备
    if (v4l2_fd_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 要停止的流类型
        // 停止视频流 (即使失败也继续清理，因为可能流未成功启动)
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &type);
        close(v4l2_fd_);  // 关闭文件描述符
        v4l2_fd_ = -1;    // 将文件描述符置为无效值
    }

    // 释放 RGB 缓冲区内存
    delete[] rgb_buffer_;
    rgb_buffer_ = nullptr;  // 将指针置空
}

bool V4L2Camera::Capture(const std::string& filename, int quality) {
    // 检查摄像头是否已成功初始化
    if (!is_initialized_) {
        std::cerr << "Camera not initialized\n";
        return false;  // 如果未初始化，直接返回失败
    }

    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));            // 清零缓冲区信息结构体
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置缓冲区类型
    buf.memory = V4L2_MEMORY_MMAP;           // 设置内存类型

    // 使用 VIDIOC_DQBUF 从队列中取出一个已填充数据的缓冲区
    // 这是一个阻塞调用，直到有帧可用
    if (xioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) {
        // 出队失败，打印错误信息
        perror("VIDIOC_DQBUF");
        return false;  // 捕获失败
    }

    // 将从摄像头缓冲区获取的 YUYV 数据转换为 RGB24 格式
    // buf.index 是当前出队的缓冲区的索引
    YUYVToRGB24(buf_infos_[buf.index].start, rgb_buffer_, actual_width_, actual_height_);

    // 将 RGB 数据保存为 JPEG 文件
    // SaveAsJpeg 返回 0 表示成功
    bool success = (SaveAsJpeg(filename, rgb_buffer_, actual_width_, actual_height_, quality) == 0);

    // 使用 VIDIOC_QBUF 将缓冲区重新放入队列，以便驱动可以填充下一帧数据
    if (xioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
        // 入队失败，打印错误信息
        perror("VIDIOC_QBUF");
        success = false;  // 如果入队失败，则整个捕获过程也视为失败
    }

    return success;  // 返回捕获并保存是否成功
}
