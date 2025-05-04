#include "client_send.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief 使用POST请求向指定IP和端口发送WAV音频文件
 *
 * @param ip_address 目标服务器IP地址 (字符串)
 * @param port 目标服务器端口号
 * @param wav_filepath 要发送的WAV文件路径
 * @param request_path POST请求的目标路径 (例如 "/upload_audio")
 * @return int 0 表示成功, -1 表示失败
 */
int audio_send(const char* ip_address, int port, const char* wav_filepath, const char* request_path) {
    int sockfd;
    struct sockaddr_in server_addr;
    FILE* wav_file;
    long file_size;
    struct stat file_stat;
    char header_buffer[1024];
    char data_buffer[SEND_BUFFER_SIZE];
    ssize_t bytes_sent, bytes_read;

    // 1. 获取文件大小
    if (stat(wav_filepath, &file_stat) < 0) {
        perror("Error getting file stats");
        return -1;
    }
    file_size = file_stat.st_size;

    // 2. 打开WAV文件
    wav_file = fopen(wav_filepath, "rb");  // 以二进制读取模式打开
    if (!wav_file) {
        perror("Error opening WAV file");
        return -1;
    }

    // 3. 创建socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        fclose(wav_file);
        return -1;
    }

    // 4. 设置服务器地址信息
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    // 5. 连接到服务器
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    // 6. 构建HTTP POST请求头
    snprintf(header_buffer, sizeof(header_buffer),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: audio/wav\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"  // 使用 close 简化处理
             "\r\n",                  // Header 和 body 之间的空行
             request_path ? request_path : "/", ip_address, port, file_size);

    // 7. 发送HTTP头
    bytes_sent = send(sockfd, header_buffer, strlen(header_buffer), 0);
    if (bytes_sent < 0 || (size_t)bytes_sent != strlen(header_buffer)) {
        perror("Error sending HTTP header");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    // 8. 发送WAV文件内容
    printf("Sending WAV file content (%ld bytes)...\n", file_size);
    long total_bytes_sent = 0;
    while ((bytes_read = fread(data_buffer, 1, SEND_BUFFER_SIZE, wav_file)) > 0) {
        char* p_buffer = data_buffer;
        ssize_t current_bytes_sent;
        while (bytes_read > 0) {
            current_bytes_sent = send(sockfd, p_buffer, bytes_read, 0);
            if (current_bytes_sent < 0) {
                perror("Error sending file data");
                fclose(wav_file);
                close(sockfd);
                return -1;
            }
            p_buffer += current_bytes_sent;
            bytes_read -= current_bytes_sent;
            total_bytes_sent += current_bytes_sent;
            // printf("Sent %ld bytes, total %ld\n", current_bytes_sent, total_bytes_sent); // Debug
        }
    }

    if (ferror(wav_file)) {
        perror("Error reading from WAV file");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    printf("Finished sending %ld bytes.\n", total_bytes_sent);

    // 9. 关闭文件和socket
    fclose(wav_file);
    // 可以选择接收服务器响应，这里简单关闭
    // char response_buffer[1024];
    // recv(sockfd, response_buffer, sizeof(response_buffer) - 1, 0);
    // printf("Server response: %s\n", response_buffer);
    close(sockfd);

    return 0;  // 成功
}

/**
 * @brief 使用POST请求向指定IP和端口发送文本数据
 *
 * @param ip_address 目标服务器IP地址 (字符串)
 * @param port 目标服务器端口号
 * @param text_to_send 要发送的文本字符串
 * @param request_path POST请求的目标路径 (例如 "/process_text")
 * @return int 0 表示成功, -1 表示失败
 */
int llm_reponse_send(const char* ip_address, int port, const char* text_to_send, const char* request_path) {
    int sockfd;
    struct sockaddr_in server_addr;
    char header_buffer[1024];
    ssize_t bytes_sent;
    size_t text_len = strlen(text_to_send);

    // 1. 创建socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        return -1;
    }

    // 2. 设置服务器地址信息
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        return -1;
    }

    // 3. 连接到服务器
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return -1;
    }

    // 4. 构建HTTP POST请求头
    // 注意: Content-Type 可以是 text/plain 或 application/json 等，根据LLM API要求调整
    snprintf(header_buffer, sizeof(header_buffer),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: text/plain; charset=utf-8\r\n"  // 假设是纯文本UTF-8
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             request_path ? request_path : "/", ip_address, port, text_len);

    // 5. 发送HTTP头
    bytes_sent = send(sockfd, header_buffer, strlen(header_buffer), 0);
    if (bytes_sent < 0 || (size_t)bytes_sent != strlen(header_buffer)) {
        perror("Error sending HTTP header");
        close(sockfd);
        return -1;
    }

    // 6. 发送文本数据
    bytes_sent = send(sockfd, text_to_send, text_len, 0);
    if (bytes_sent < 0 || (size_t)bytes_sent != text_len) {
        perror("Error sending text data");
        close(sockfd);
        return -1;
    }
    printf("Finished sending text.\n");
    close(sockfd);

    return 0;  // 成功
}
