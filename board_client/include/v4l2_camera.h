#ifndef V4L2_CAMERA_HPP  
#define V4L2_CAMERA_HPP

#include <linux/videodev2.h>
#include <cstddef>
#include <string>

class V4L2Camera {
    // 私有结构体，用于存储映射到用户空间的摄像头缓冲区信息
    struct CamBufInfo {
        unsigned char* start = nullptr;  // 缓冲区在用户空间的起始地址
        size_t length = 0;               // 缓冲区长度
    };

    // 静态常量，定义我们希望申请的视频缓冲区数量
    static constexpr int FRAMEBUFFER_COUNT = 3;
    // 静态常量，定义默认的捕获宽度
    static constexpr int DEFAULT_WIDTH = 640;
    // 静态常量，定义默认的捕获高度
    static constexpr int DEFAULT_HEIGHT = 480;

    // 摄像头设备的文件描述符
    int v4l2_fd_;
    // 存储每个映射缓冲区的信息的数组
    CamBufInfo buf_infos_[FRAMEBUFFER_COUNT];
    // 缓冲区类型，通常用于启动和停止视频流
    v4l2_buf_type buf_type_;
    // 存储当前视频格式信息的结构体
    v4l2_format fmt_;
    // 用于存储 YUYV 转换后的 RGB 图像数据的缓冲区
    unsigned char* rgb_buffer_;
    // 标志，指示摄像头是否成功初始化
    bool is_initialized_;
    // 实际从驱动获取的图像宽度（可能与请求的默认值不同）
    int actual_width_;
    // 实际从驱动获取的图像高度（可能与请求的默认值不同）
    int actual_height_;

   public:
    // 构造函数，负责初始化摄像头
    V4L2Camera();
    // 析构函数，负责清理资源
    ~V4L2Camera();

    // 检查摄像头是否成功初始化
    bool IsInitialized() const { return is_initialized_; }
    // 捕获一帧图像，将其转换为 RGB，并保存为 JPEG 文件
    // filename: 保存的文件名
    // quality: JPEG 压缩质量 (0-100)
    bool Capture(const std::string& filename, int quality = 85);
    // 清理所有分配和映射的资源（由析构函数调用，也可手动调用）
    void CleanUp();

   private:
    // ioctl 系统调用的包装函数，处理 EINTR 中断
    // fd: 文件描述符
    // request: ioctl 请求码
    // arg: 请求参数
    int xioctl(int fd, int request, void* arg);
    // 将 YUYV 格式的图像数据转换为 RGB24 格式
    // yuyv: 输入的 YUYV 数据指针
    // rgb: 输出的 RGB24 数据指针
    // width: 图像宽度
    // height: 图像高度
    void YUYVToRGB24(const unsigned char* yuyv, unsigned char* rgb, int width, int height);
    // 使用 libjpeg 库将 RGB24 数据保存为 JPEG 文件
    // filename: 输出文件名
    // rgb_data: 输入的 RGB24 数据指针
    // width: 图像宽度
    // height: 图像高度
    // quality: JPEG 压缩质量
    // 返回值: 0 表示成功，-1 表示失败
    int SaveAsJpeg(const std::string& filename,
                   const unsigned char* rgb_data,
                   int width,
                   int height,
                   int quality);
    // 执行 V4L2 设备的初始化序列：打开设备、设置格式、申请/映射缓冲区、启动流
    // 返回值: true 表示成功，false 表示失败
    bool Init();
};

#endif  // V4L2_CAMERA_HPP
