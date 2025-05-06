#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "chat_record.h"
#include "client_receive.h"
#include "client_send.h"
#include "llm_api.h"
#include "v4l2_camera.h"

const ApiConfig gemini_config = {.name = "Gemini",
                                 .host = "generativelanguage.googleapis.com",
                                 .path_base = "/v1beta/models/gemini-2.5-flash-preview-04-17:generateContent",
                                 .api_key = GEMINI_API_KEY,
                                 .auth_method = AUTH_METHOD_URL_PARAM,
                                 .model_name = "gemini-2.5-flash-preview-04-17",
                                 .response_search_key = "\"text\": \"",
                                 .role = "model",
                                 .use_proxy = true};

const ApiConfig deepseek_config = {.name = "DeepSeek",
                                   .host = "api.deepseek.com",
                                   .path_base = "/chat/completions",
                                   .api_key = DEEPSEEK_API_KEY,
                                   .auth_method = AUTH_METHOD_BEARER_HEADER,
                                   .model_name = "deepseek-chat",
                                   .response_search_key = "\"content\":\"",
                                   .role = "assistant",
                                   .use_proxy = false};

const ApiConfig qwen_config = {.name = "Qwen",
                               .host = "dashscope.aliyuncs.com",
                               .path_base = "/compatible-mode/v1/chat/completions",
                               .api_key = QWEN_API_KEY,
                               .auth_method = AUTH_METHOD_BEARER_HEADER,
                               .model_name = "qwen-vl-plus",
                               .response_search_key = "\"content\":\"",
                               .role = "system",
                               .use_proxy = false};

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

    fd = open("/dev/key0", O_RDWR);
    if (fd < 0) {
        printf("Can't open file");
    }

    ret = read(fd, &key_status, sizeof(key_status));
    if (ret != -1) { /* 数据读取正确 */
        printf("key_status = %d\n", key_status);
        FILE* arecord_pipe = NULL;
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

            if (audio_send(server_ip, 8000, "test.wav", "/upload/audio") != 0)
                fprintf(stderr, "audio_send failed.\n");
            int listen_sock = setup_listen_socket(8001);
            char* text = handle_request(listen_sock, NULL);
            printf("Received text: %s\n", text);
            close(listen_sock);

            const char* prompt = "回复中的数字不要使用阿拉伯数字，使用中文数字，回复中只回应以下内容：\n\n";

            // char* message = malloc(strlen(prompt) + strlen(text) + 1);  // +1 给终止符
            // strcpy(message, prompt);
            // strcat(message, text);

            // char* response = send_llm_request(&qwen_config, message, 0);
            // printf("LLM Response: %s\n", response);
            // llm_reponse_send(server_ip, 8000, response, "/send/text");

            listen_sock = setup_listen_socket(8001);
            handle_request(listen_sock, "./llm_reponse_audio.wav");
            close(listen_sock);

            FILE* aplay_pipe = popen("aplay ./llm_reponse_audio.wav", "r");
            int status = pclose(aplay_pipe);
            if (status == 0) {
                printf("音频播放完成\n");
            } else {
                printf("音频播放出现问题\n");
            }
        }
    }
    close(fd);
}

int sqlite_test(void) {
    const char* db_path = "./test.db";
    sqlite3* db;
    int rc;

    printf("===== Testing Chat History =====\n");

    // Test database initialization
    rc = init_database(&db, db_path);
    if (rc != SQLITE_OK) {
        printf("✗ Database initialization failed\n");
        return -1;
    }
    printf("✓ Database initialized\n");

    // Test insert message
    rc = insert_message(db, "gpt-3.5", "Hello world", "Greetings, human!", NULL);
    if (rc != SQLITE_OK) {
        printf("✗ Insert message failed\n");
        sqlite3_close(db);
        return -1;
    }
    printf("✓ Message inserted\n");

    // Insert messages with specific dates for testing
    insert_message(db, "gpt-4", "What is AI?", "AI is artificial intelligence...", "2023-01-01 10:00:00");
    insert_message(db, "claude", "Help me code", "Here's how to code...", "2023-01-01 11:00:00");
    insert_message(db, "llama", "Explain machine learning", "Machine learning is a subset of AI that...",
                   "2023-01-02 14:30:00");
    insert_message(db, "gemini", "How to optimize algorithms?",
                   "Algorithm optimization involves several techniques including...", "2023-01-02 16:45:00");

    // Test query functions
    printf("✓ Testing queries...\n");
    printf("=== All Messages ===\n");
    query_all_messages(db);
    query_all_dates(db);
    query_messages_by_date(db, "2023-01-01");

    // Test update
    rc = update_message_by_id(db, 1, "Updated question", "Updated answer");
    if (rc != SQLITE_OK) {
        printf("✗ Update message failed\n");
        sqlite3_close(db);
        return -1;
    }
    query_all_messages(db);
    printf("✓ Message updated\n");

    // Test delete
    rc = delete_message_by_id(db, 2);
    if (rc != SQLITE_OK) {
        printf("✗ Delete by ID failed\n");
        sqlite3_close(db);
        return -1;
    }
    query_all_messages(db);
    printf("✓ Message deleted by ID\n");

    rc = delete_messages_by_date(db, "2023-01-02");
    if (rc != SQLITE_OK) {
        printf("✗ Delete by date failed\n");
        sqlite3_close(db);
        return -1;
    }
    query_all_messages(db);
    printf("✓ Messages deleted by date\n");

    sqlite3_close(db);
    if (remove(db_path) == 0) {
        printf("✓ Database file deleted\n");
    } else {
        printf("✗ Failed to delete database file\n");
    }
    printf("===== All tests completed =====\n");

    return 0;
}

void overall_test() {
    char* server_ip = "10.33.47.116";
    const char* db_path = "./test.db";
    sqlite3* db;
    int ret;
    ret = init_database(&db, db_path);
    if (ret != SQLITE_OK) {
        printf("Database initialization failed\n");
        return;
    }
    printf("Database initialized successfully\n");

    int fd;
    unsigned char key_status;
    fd = open("/dev/key0", O_RDWR);
    if (fd < 0) {
        printf("Can't open file");
    }

    ConversationMessage conversation_data[9];
    conversation_data[0].role = deepseek_config.role;
    conversation_data[0].content =
        "回复中的数字不要使用阿拉伯数字，使用中文数字，回复不要太长，在200字以内，回复中只回应以"
        "下内容：\n\n";
    int test_audio_id = 0;
    int conversation_cnt = 1;
    while (true) {
        ret = read(fd, &key_status, sizeof(key_status));
        if (ret != -1) { /* 数据读取正确 */
            printf("key_status = %d\n", key_status);
            FILE* arecord_pipe = NULL;
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

                char filename[32];
                snprintf(filename, sizeof(filename), "./test_audios/test%d.wav", (test_audio_id % 4) + 1);
                test_audio_id++;
                if (audio_send(server_ip, 8000, filename, "/upload/audio") != 0)
                    fprintf(stderr, "audio_send failed for %s.\n", filename);
                int listen_sock = setup_listen_socket(8001);
                char* audio_text = handle_request(listen_sock, NULL);
                printf("Received text: %s\n", audio_text);
                close(listen_sock);

                conversation_data[conversation_cnt].role = "user";
                conversation_data[conversation_cnt].content = audio_text;
                conversation_cnt++;
                char* response = send_llm_request(&deepseek_config, conversation_data, conversation_cnt);
                printf("LLM Response: %s\n", response);
                conversation_data[conversation_cnt].role = deepseek_config.role;
                conversation_data[conversation_cnt].content = response;
                conversation_cnt++;
                llm_reponse_send(server_ip, 8000, response, "/send/text");

                insert_message(db, "deepseek", audio_text, response, NULL);

                listen_sock = setup_listen_socket(8001);
                time_t timestamp = time(NULL);
                char response_filename[48];
                snprintf(response_filename, sizeof(response_filename), "./llm_response_audios/%ld.wav",
                         timestamp);
                handle_request(listen_sock, response_filename);
                close(listen_sock);

                // 字符串拼接
                char command[48];
                snprintf(command, sizeof(command), "aplay %s", response_filename);
                FILE* aplay_pipe = popen(command, "r");
                int status = pclose(aplay_pipe);
                if (status == 0) {
                    printf("音频播放完成\n");
                } else {
                    printf("音频播放出现问题\n");
                }
            }
        }
    }

    close(fd);
    sqlite3_close(db);
    remove(db_path);
}

int main(int argc, char* argv[]) {
    overall_test();
    return 0;
}