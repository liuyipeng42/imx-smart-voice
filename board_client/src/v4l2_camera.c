#include "v4l2_camera.h"
#include <errno.h>
#include <fcntl.h>
#include <jpeglib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int xioctl(int fd, int request, void* arg) {
    int r;

    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}

/*
 * YUYV to RGB24 conversion using integer arithmetic.
 * Includes vertical flip.
 */
void yuyv_to_rgb24_integer(const unsigned char* yuyv, unsigned char* rgb, int width, int height) {
    int y0, u, y1, v;
    int r, g, b;
    int C, D, E;  // Intermediate values for YUV->RGB calculation

    const unsigned char* yuv_ptr = yuyv;
    // Iterate through rows in reverse order for vertical flip
    for (int j = height - 1; j >= 0; --j) {
        // Calculate starting position for the destination RGB row
        unsigned char* rgb_row_ptr = rgb + j * width * 3;

        for (int i = 0; i < width; i += 2) {
            // Extract YUV components for two pixels
            y0 = yuv_ptr[0];
            u = yuv_ptr[1];
            y1 = yuv_ptr[2];
            v = yuv_ptr[3];
            yuv_ptr += 4;  // Move to the next YUYV group

            // --- Process first pixel (Y0, U, V) ---
            C = y0 - 16;
            D = u - 128;
            E = v - 128;

            // Calculate R, G, B using integer approximations
            r = CLAMP((298 * C + 409 * E + 128) >> 8);
            g = CLAMP((298 * C - 100 * D - 208 * E + 128) >> 8);
            b = CLAMP((298 * C + 516 * D + 128) >> 8);

            // Store RGB components for the first pixel
            *rgb_row_ptr++ = (unsigned char)r;
            *rgb_row_ptr++ = (unsigned char)g;
            *rgb_row_ptr++ = (unsigned char)b;

            // --- Process second pixel (Y1, U, V) ---
            C = y1 - 16;
            // D and E are the same for both pixels in YUYV

            // Calculate R, G, B using integer approximations
            r = CLAMP((298 * C + 409 * E + 128) >> 8);
            g = CLAMP((298 * C - 100 * D - 208 * E + 128) >> 8);
            b = CLAMP((298 * C + 516 * D + 128) >> 8);

            // Store RGB components for the second pixel
            *rgb_row_ptr++ = (unsigned char)r;
            *rgb_row_ptr++ = (unsigned char)g;
            *rgb_row_ptr++ = (unsigned char)b;
        }
        // yuv_ptr should now point to the start of the next source row
        // No need to explicitly calculate yuv_index, pointer arithmetic handles it.
    }
}

/* Save RGB data as a JPEG file */
int save_as_jpeg(const char* filename, const unsigned char* rgb_data, int width, int height, int quality) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE* outfile = NULL;  // Initialize to NULL
    JSAMPROW row_pointer[1];
    int ret = 0;  // Success by default

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    if ((outfile = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "ERROR: Can't open %s for writing: %s\n", filename, strerror(errno));
        jpeg_destroy_compress(&cinfo);  // Clean up libjpeg
        return -1;
    }

    jpeg_stdio_dest(&cinfo, outfile);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;      // 3 for RGB
    cinfo.in_color_space = JCS_RGB;  // Use JCS_RGB

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);  // TRUE=limit to baseline-JPEG values
    jpeg_start_compress(&cinfo, TRUE);

    // Process scanlines
    while (cinfo.next_scanline < cinfo.image_height) {
        // Note: libjpeg expects top-down scanlines.
        // If yuyv_to_rgb24_integer didn't do the flip, you'd need:
        // row_pointer[0] = &rgb_data[(height - 1 - cinfo.next_scanline) * width * 3];
        // But since we flipped during conversion, use the direct scanline:
        row_pointer[0] = (JSAMPROW)&rgb_data[cinfo.next_scanline * width * 3];
        if (jpeg_write_scanlines(&cinfo, row_pointer, 1) != 1) {
            fprintf(stderr, "ERROR: jpeg_write_scanlines failed\n");
            ret = -1;  // Mark as failed
            break;     // Exit loop on error
        }
    }

    // Clean up
    if (ret == 0) {  // Only finish compress if no error occured during write
        jpeg_finish_compress(&cinfo);
    } else {  // Abort compress on error
        jpeg_abort_compress(&cinfo);
    }
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);

    return ret;
}

/* 初始化相机设备 */
CameraState* camera_init() {
    CameraState* state = malloc(sizeof(CameraState));
    if (!state)
        return NULL;
    // 1. 打开设备
    state->v4l2_fd = open("/dev/video0", O_RDWR | O_NONBLOCK, 0);
    if (state->v4l2_fd < 0) {
        perror("Failed to open device");
        free(state);
        return NULL;
    }
    // 2. 设置视频格式
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = DEFAULT_WIDTH;
    fmt.fmt.pix.height = DEFAULT_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (xioctl(state->v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Failed to set format");
        close(state->v4l2_fd);
        free(state);
        return NULL;
    }
    state->fmt = fmt;
    // 3. 请求缓冲区
    struct v4l2_requestbuffers req = {0};
    req.count = FRAMEBUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(state->v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to request buffers");
        close(state->v4l2_fd);
        free(state);
        return NULL;
    }
    // 4. 映射缓冲区
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    for (buf.index = 0; buf.index < req.count; ++buf.index) {
        if (xioctl(state->v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("Failed to query buffer");
            for (int i = 0; i < buf.index; ++i)
                munmap(state->buf_infos[i].start, state->buf_infos[i].length);
            close(state->v4l2_fd);
            free(state);
            return NULL;
        }

        state->buf_infos[buf.index].length = buf.length;
        state->buf_infos[buf.index].start =
            mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, state->v4l2_fd, buf.m.offset);

        if (state->buf_infos[buf.index].start == MAP_FAILED) {
            perror("mmap failed");
            for (int i = 0; i < buf.index; ++i)
                munmap(state->buf_infos[i].start, state->buf_infos[i].length);
            close(state->v4l2_fd);
            free(state);
            return NULL;
        }
        if (xioctl(state->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("Failed to queue buffer");
            for (int i = 0; i <= buf.index; ++i)
                munmap(state->buf_infos[i].start, state->buf_infos[i].length);
            close(state->v4l2_fd);
            free(state);
            return NULL;
        }
    }
    // 5. 预分配RGB缓冲区
    size_t rgb_size = DEFAULT_WIDTH * DEFAULT_HEIGHT * 3;
    state->rgb_buffer = malloc(rgb_size);
    if (!state->rgb_buffer) {
        perror("Failed to allocate RGB buffer");
        for (int i = 0; i < req.count; ++i)
            munmap(state->buf_infos[i].start, state->buf_infos[i].length);
        close(state->v4l2_fd);
        free(state);
        return NULL;
    }
    // 6. 启动视频流
    state->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(state->v4l2_fd, VIDIOC_STREAMON, &state->buf_type) < 0) {
        perror("Failed to start streaming");
        for (int i = 0; i < req.count; ++i)
            munmap(state->buf_infos[i].start, state->buf_infos[i].length);
        free(state->rgb_buffer);
        close(state->v4l2_fd);
        free(state);
        return NULL;
    }
    return state;
}

/* 捕获单帧并保存 */
int camera_capture(CameraState* state, const char* filename, int quality) {
    if (!state || !filename)
        return -1;
    // 等待帧数据就绪
    fd_set fds;
    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    FD_ZERO(&fds);
    FD_SET(state->v4l2_fd, &fds);
    int r = select(state->v4l2_fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        fprintf(stderr, r == 0 ? "Timeout\n" : "select error\n");
        return -1;
    }
    // 获取帧数据
    unsigned char* yuyv_data = NULL;
    size_t yuyv_size = 0;
    struct v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
    if (ioctl(state->v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("Dequeue failed");
        return -1;
    }
    yuyv_data = state->buf_infos[buf.index].start;
    yuyv_size = buf.bytesused;
    // 立即重新入队缓冲区
    if (ioctl(state->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("Requeue failed");
        return -1;
    }
    // 转换颜色空间（复用预分配缓冲区）
    yuyv_to_rgb24_integer(yuyv_data, state->rgb_buffer, state->fmt.fmt.pix.width, state->fmt.fmt.pix.height);
    // 保存JPEG
    int ret = save_as_jpeg(filename, state->rgb_buffer, state->fmt.fmt.pix.width, state->fmt.fmt.pix.height,
                           quality);
    return ret;
}

/* 清理资源 */
void camera_cleanup(CameraState* state) {
    if (!state)
        return;
    // 停止视频流
    xioctl(state->v4l2_fd, VIDIOC_STREAMOFF, &state->buf_type);
    // 解除内存映射
    for (int i = 0; i < FRAMEBUFFER_COUNT; ++i) {
        if (state->buf_infos[i].start != MAP_FAILED)
            munmap(state->buf_infos[i].start, state->buf_infos[i].length);
    }
    // 释放资源
    free(state->rgb_buffer);
    close(state->v4l2_fd);
    free(state);
}
