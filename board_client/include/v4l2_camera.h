#include <linux/videodev2.h>
#include <sys/types.h>

#define CLAMP(x) (((x) < 0) ? 0 : (((x) > 255) ? 255 : (x)))

#define FRAMEBUFFER_COUNT 3
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_JPEG_QUALITY 85

// Structure to hold buffer info
typedef struct cam_buf_info {
    unsigned char* start;
    size_t length;  // Use size_t for length/size generally
} cam_buf_info;

typedef struct {
    int v4l2_fd;
    cam_buf_info buf_infos[FRAMEBUFFER_COUNT];
    enum v4l2_buf_type buf_type;
    struct v4l2_format fmt;
    unsigned char* rgb_buffer;  // 预分配的RGB缓冲区
} CameraState;

CameraState* camera_init();

int camera_capture(CameraState* state, const char* filename, int quality);

void camera_cleanup(CameraState* state);