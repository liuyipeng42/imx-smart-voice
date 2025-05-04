#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "client_api.h"
#include "llm_api.h"
#include "v4l2_camera.h"

const ApiConfig gemini_config = {.name = "Gemini",
                                 .host = "generativelanguage.googleapis.com",
                                 .path_base = "/v1beta/models/gemini-2.5-flash-preview-04-17:generateContent",
                                 .api_key = GEMINI_API_KEY,
                                 .auth_method = AUTH_METHOD_URL_PARAM,
                                 .model_name = "gemini-2.5-flash-preview-04-17",
                                 .response_search_key = "\"text\": \""};

const ApiConfig deepseek_config = {.name = "DeepSeek",
                                   .host = "api.deepseek.com",
                                   .path_base = "/chat/completions",
                                   .api_key = DEEPSEEK_API_KEY,
                                   .auth_method = AUTH_METHOD_BEARER_HEADER,
                                   .model_name = "deepseek-chat",
                                   .response_search_key = "\"content\":\""};

const ApiConfig qwen_config = {.name = "Qwen",
                               .host = "dashscope.aliyuncs.com",
                               .path_base = "/compatible-mode/v1/chat/completions",
                               .api_key = QWEN_API_KEY,
                               .auth_method = AUTH_METHOD_BEARER_HEADER,
                               .model_name = "qwen-vl-plus",
                               .response_search_key = "\"content\":\""};

void camera_test() {
    CameraState* cam = camera_init();
    // 连续捕获10帧
    for (int i = 0; i < 10; ++i) {
        char filename[32];
        snprintf(filename, sizeof(filename), "frame%d.jpg", i);

        if (camera_capture(cam, filename, 100) == 0) {
            printf("Saved %s\n", filename);
        } else {
            fprintf(stderr, "Failed to capture frame %d\n", i);
        }
        usleep(100000);  // 100ms间隔
    }
    camera_cleanup(cam);
}

void api_test() {
    char* server_ip = "10.33.47.116";

    int fd;
    int ret = 0;
    unsigned char key_status = 0;
    FILE* arecord_pipe = NULL;

    fd = open("/dev/key0", O_RDWR);
    if (fd < 0) {
        printf("Can't open file");
    }

    ret = read(fd, &key_status, sizeof(key_status));
    if (ret != -1) { /* 数据读取正确 */
        printf("key_status = %d\n", key_status);
        if (key_status == 1 && arecord_pipe == NULL) {
            printf("start\n");
            arecord_pipe = popen("arecord -f cd ./record.wav", "r");
        }
        ret = read(fd, &key_status, sizeof(key_status));
        printf("key_status = %d\n", key_status);
        if (key_status == 2) {
            printf("finished\n");
            system("pkill arecord");
            pclose(arecord_pipe);
            arecord_pipe = NULL;
            if (audio_send(server_ip, 8000, "test.wav", "/upload_record") != 0) {
                fprintf(stderr, "audio_send failed.\n");
            }
            audio_text_receive(8001, 10000);
        }
    }
    close(fd);
}

int main(int argc, char* argv[]) {
    camera_test();
    return 0;
}