#include "client_api.h"
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
    char data_buffer[BUFFER_SIZE];
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
    printf("Sent HTTP header:\n%s", header_buffer);

    // 8. 发送WAV文件内容
    printf("Sending WAV file content (%ld bytes)...\n", file_size);
    long total_bytes_sent = 0;
    while ((bytes_read = fread(data_buffer, 1, BUFFER_SIZE, wav_file)) > 0) {
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
 * @brief 接收Python requests发送的POST请求中的文本数据
 *        (作为一次性服务器运行，处理完一个请求即退出)
 *
 * @param listen_port 要监听的端口号
 * @param received_text_buffer 用于存储接收到的文本的缓冲区
 * @param buffer_size 缓冲区的大小
 * @param max_content_length 允许接收的最大文本长度 (防止过大请求)
 * @return int 接收到的文本字节数, -1 表示失败, -2 表示内容过长
 */
int audio_text_receive(int listen_port, size_t max_content_length) {
    int listen_sockfd, conn_sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    // --- 服务器设置 (只执行一次) ---
    listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sockfd < 0) {
        perror("Error creating listening socket");
        return -1;  // 致命错误，无法启动
    }
    int opt = 1;
    if (setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error setting SO_REUSEADDR");
        close(listen_sockfd);
        return -1;  // 致命错误
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(listen_port);
    if (bind(listen_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(listen_sockfd);
        return -1;  // 致命错误
    }
    if (listen(listen_sockfd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("Error listening on socket");
        close(listen_sockfd);
        return -1;  // 致命错误
    }
    printf("Text server listening continuously on port %d...\n", listen_port);
    // --- 主循环，持续接受和处理连接 ---
    while (1) {
        printf("\nWaiting for a new text connection...\n");
        conn_sockfd = accept(listen_sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (conn_sockfd < 0) {
            perror("Error accepting connection - continuing");  // 非致命错误，记录并继续等待
            continue;                                           // 继续循环，尝试接受下一个连接
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Connection accepted from %s:%d\n", client_ip, ntohs(client_addr.sin_port));
        // --- 处理单个连接 ---
        char request_buffer[BUFFER_SIZE];
        ssize_t bytes_received;
        long content_length = -1;
        char* body_start = NULL;
        size_t header_length = 0;
        size_t total_body_received = 0;
        char* received_text_buffer = NULL;  // 为本次请求分配内存
        int request_status = 0;             // 0: success, -1: error, -2: too large
        // 接收请求头 (和可能的部分body)
        memset(request_buffer, 0, BUFFER_SIZE);
        bytes_received = recv(conn_sockfd, request_buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            perror("Error receiving data or connection closed prematurely");
            close(conn_sockfd);
            continue;  // 处理下一个连接
        }
        request_buffer[bytes_received] = '\0';
        // printf("Received Raw Request (%ld bytes):\n%s\n---\n", bytes_received, request_buffer); // Debug
        // 解析 Content-Length
        char* cl_ptr = strstr(request_buffer, "Content-Length:");
        if (cl_ptr) {
            sscanf(cl_ptr, "Content-Length: %ld", &content_length);
        }
        if (content_length < 0) {
            fprintf(stderr, "Error: Content-Length header not found or invalid.\n");
            const char* resp = "HTTP/1.1 411 Length Required\r\nConnection: close\r\n\r\n";
            send(conn_sockfd, resp, strlen(resp), 0);
            request_status = -1;
        }
        // 检查内容长度是否过大
        if (request_status == 0 && (size_t)content_length > max_content_length) {
            fprintf(stderr, "Error: Content-Length (%ld) exceeds maximum allowed (%zu).\n", content_length,
                    max_content_length);
            const char* resp = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n";
            send(conn_sockfd, resp, strlen(resp), 0);
            request_status = -2;
        }
        // 分配足够大的内存 (+1 for null terminator)
        if (request_status == 0) {
            received_text_buffer = (char*)malloc(content_length + 1);
            if (!received_text_buffer) {
                perror("Error allocating memory for text buffer");
                const char* resp = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
                send(conn_sockfd, resp, strlen(resp), 0);
                request_status = -1;
            } else {
                memset(received_text_buffer, 0, content_length + 1);
            }
        }
        // 查找 body 开始位置
        if (request_status == 0) {
            body_start = strstr(request_buffer, "\r\n\r\n");
            if (!body_start) {
                fprintf(stderr, "Error: Malformed HTTP request (no header/body separator).\n");
                const char* resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(conn_sockfd, resp, strlen(resp), 0);
                request_status = -1;
            } else {
                body_start += 4;
                header_length = body_start - request_buffer;
            }
        }
        // 处理已接收的 body 部分
        if (request_status == 0) {
            size_t initial_body_bytes = bytes_received > header_length ? bytes_received - header_length : 0;
            if (initial_body_bytes > 0) {
                size_t bytes_to_copy = (initial_body_bytes > (size_t)content_length) ? (size_t)content_length
                                                                                     : initial_body_bytes;
                memcpy(received_text_buffer, body_start, bytes_to_copy);
                total_body_received += bytes_to_copy;
            }
        }
        // 接收剩余的 body 数据
        if (request_status == 0) {
            while (total_body_received < (size_t)content_length) {
                // 注意：这里直接读入动态分配的缓冲区
                bytes_received = recv(conn_sockfd, received_text_buffer + total_body_received,
                                      content_length - total_body_received, 0);
                if (bytes_received <= 0) {
                    perror("Error receiving remaining body data or connection closed");
                    fprintf(
                        stderr,
                        "Warning: Connection closed before receiving full content. Expected %ld, got %zu\n",
                        content_length, total_body_received);
                    request_status = -1;
                    break;  // 退出接收循环
                }
                total_body_received += bytes_received;
            }
        }
        // 如果成功接收
        if (request_status == 0) {
            received_text_buffer[total_body_received] = '\0';  // Null terminate
            printf("Successfully Received Text (%zu bytes):\n---\n%s\n---\n", total_body_received,
                   received_text_buffer);
            // 在这里可以添加处理接收到的文本的逻辑
            // process_received_text(received_text_buffer);
            // 发送成功响应
            const char* http_response =
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 15\r\nConnection: "
                "close\r\n\r\nText received.\n";
            send(conn_sockfd, http_response, strlen(http_response), 0);
            printf("Sent OK response.\n");
        }
        // 清理本次连接的资源
        if (received_text_buffer) {
            free(received_text_buffer);  // 释放为本次请求分配的内存
            received_text_buffer = NULL;
        }
        close(conn_sockfd);  // 关闭当前客户端连接
        printf("Connection closed.\n");
        // 循环继续，等待下一个连接...
    }  // end while(1)
    // 这部分代码理论上不会执行，除非循环被break（例如通过信号处理）
    printf("Text server shutting down.\n");
    close(listen_sockfd);  // 关闭监听套接字
    return 0;              // 表示正常关闭（虽然可能无法到达）
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
    printf("Sent HTTP header:\n%s", header_buffer);

    // 6. 发送文本数据
    printf("Sending text data (%zu bytes): %s\n", text_len, text_to_send);
    bytes_sent = send(sockfd, text_to_send, text_len, 0);
    if (bytes_sent < 0 || (size_t)bytes_sent != text_len) {
        perror("Error sending text data");
        close(sockfd);
        return -1;
    }
    printf("Finished sending text.\n");

    // 7. 关闭socket
    // 可以选择接收服务器响应
    // char response_buffer[1024];
    // recv(sockfd, response_buffer, sizeof(response_buffer) - 1, 0);
    // printf("Server response: %s\n", response_buffer);
    close(sockfd);

    return 0;  // 成功
}

/**
 * @brief 接收Python发送的WAV音频文件并保存
 *        (作为一次性服务器运行，处理完一个请求即退出)
 *
 * @param listen_port 要监听的端口号
 * @param save_filepath 用于保存接收到的WAV文件的路径
 * @param max_file_size 允许接收的最大文件大小 (字节)
 * @return long long 接收到的文件字节数, -1 表示失败, -2 表示文件过大
 */
long long audio_receive(int listen_port, const char* save_dir, long long max_file_size) {
    int listen_sockfd, conn_sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    // --- 服务器设置 (只执行一次) ---
    listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sockfd < 0) {
        perror("Error creating listening socket");
        return -1;
    }
    int opt = 1;
    if (setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error setting SO_REUSEADDR");
        close(listen_sockfd);
        return -1;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(listen_port);
    if (bind(listen_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(listen_sockfd);
        return -1;
    }
    if (listen(listen_sockfd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("Error listening on socket");
        close(listen_sockfd);
        return -1;
    }
    // 确保保存目录存在 (简单实现，生产代码应检查权限等)
    mkdir(save_dir, 0755);  // POSIX function, may need <sys/stat.h> and <sys/types.h>
    printf("Audio server listening continuously on port %d, saving to '%s'...\n", listen_port, save_dir);
    // --- 主循环，持续接受和处理连接 ---
    int file_counter = 0;  // 用于生成唯一文件名
    while (1) {
        printf("\nWaiting for a new audio connection...\n");
        conn_sockfd = accept(listen_sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (conn_sockfd < 0) {
            perror("Error accepting connection - continuing");
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Connection accepted from %s:%d\n", client_ip, ntohs(client_addr.sin_port));
        // --- 处理单个连接 ---
        char request_buffer[BUFFER_SIZE];
        ssize_t bytes_received;
        long long content_length = -1;
        char* body_start = NULL;
        size_t header_length = 0;
        FILE* output_file = NULL;
        long long total_bytes_written = 0;
        char save_filepath[FILENAME_MAX];  // Buffer for the full save path
        int request_status = 0;            // 0: success, -1: error, -2: too large
        // 接收请求头 (和可能的部分body)
        memset(request_buffer, 0, BUFFER_SIZE);
        bytes_received = recv(conn_sockfd, request_buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            perror("Error receiving data or connection closed prematurely");
            close(conn_sockfd);
            continue;  // 处理下一个连接
        }
        request_buffer[bytes_received] = '\0';
        // printf("Received Raw Request Header Part (%ld bytes)\n", bytes_received);
        // 解析 Content-Length
        char* cl_ptr = strstr(request_buffer, "Content-Length:");
        if (cl_ptr) {
            if (sscanf(cl_ptr, "Content-Length: %lld", &content_length) != 1) {
                content_length = -1;
            }
        }
        if (content_length < 0) {
            fprintf(stderr, "Error: Content-Length header not found or invalid.\n");
            const char* resp = "HTTP/1.1 411 Length Required\r\nConnection: close\r\n\r\n";
            send(conn_sockfd, resp, strlen(resp), 0);
            request_status = -1;
        }
        // 检查文件大小是否超过限制
        if (request_status == 0 && content_length > max_file_size) {
            fprintf(stderr, "Error: Content-Length (%lld) exceeds maximum allowed (%lld).\n", content_length,
                    max_file_size);
            const char* resp = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n";
            send(conn_sockfd, resp, strlen(resp), 0);
            request_status = -2;
        }
        // 查找 body 开始位置
        if (request_status == 0) {
            body_start = strstr(request_buffer, "\r\n\r\n");
            if (!body_start) {
                fprintf(stderr, "Error: Malformed HTTP request (no header/body separator).\n");
                const char* resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                send(conn_sockfd, resp, strlen(resp), 0);
                request_status = -1;
            } else {
                body_start += 4;
                header_length = body_start - request_buffer;
            }
        }
        // 打开输出文件
        if (request_status == 0) {
            // 生成一个简单的唯一文件名
            snprintf(save_filepath, FILENAME_MAX, "%s/received_audio_%d.wav", save_dir, file_counter++);
            output_file = fopen(save_filepath, "wb");
            if (!output_file) {
                perror("Error opening output file for writing");
                fprintf(stderr, "Failed path: %s\n", save_filepath);
                const char* resp = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
                send(conn_sockfd, resp, strlen(resp), 0);
                request_status = -1;
            } else {
                printf("Opened '%s' for writing.\n", save_filepath);
            }
        }
        // 处理已接收的 body 部分
        if (request_status == 0) {
            size_t initial_body_bytes = bytes_received > header_length ? bytes_received - header_length : 0;
            if (initial_body_bytes > 0) {
                size_t bytes_to_write = (initial_body_bytes > (size_t)content_length) ? (size_t)content_length
                                                                                      : initial_body_bytes;
                size_t written = fwrite(body_start, 1, bytes_to_write, output_file);
                if (written != bytes_to_write) {
                    perror("Error writing initial data to file");
                    request_status = -1;  // 标记错误，下面会清理
                } else {
                    total_bytes_written += written;
                    // printf("Wrote initial %zu bytes.\n", written); // Debug
                }
            }
        }
        // 接收剩余的文件数据并写入
        if (request_status == 0) {
            char file_buffer[BUFFER_SIZE];
            while (total_bytes_written < content_length) {
                long long remaining_bytes = content_length - total_bytes_written;
                size_t bytes_to_read =
                    (remaining_bytes > BUFFER_SIZE) ? BUFFER_SIZE : (size_t)remaining_bytes;
                bytes_received = recv(conn_sockfd, file_buffer, bytes_to_read, 0);
                if (bytes_received <= 0) {
                    if (bytes_received == 0) {
                        fprintf(stderr,
                                "Warning: Connection closed by peer before receiving full file. Expected "
                                "%lld, got %lld\n",
                                content_length, total_bytes_written);
                    } else {
                        perror("Error receiving file data");
                    }
                    request_status = -1;  // 标记错误
                    break;                // 退出接收循环
                }
                size_t written = fwrite(file_buffer, 1, bytes_received, output_file);
                if (written != (size_t)bytes_received) {
                    perror("Error writing received data to file");
                    request_status = -1;  // 标记错误
                    break;                // 退出接收循环
                }
                total_bytes_written += written;
            }
        }
        // 根据处理结果进行清理和响应
        if (request_status == 0) {
            // 文件接收写入成功
            fclose(output_file);  // 关闭文件
            output_file = NULL;   // 标记为已关闭
            printf("Finished receiving and writing %lld bytes to %s.\n", total_bytes_written, save_filepath);
            // 发送成功响应
            char response_buffer[200];
            // Careful calculation for Content-Length of the response body
            char response_body[100];
            int body_len = snprintf(response_body, sizeof(response_body), "File received (%lld bytes).",
                                    total_bytes_written);
            snprintf(response_buffer, sizeof(response_buffer),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",  // Body goes here
                     body_len, response_body);
            send(conn_sockfd, response_buffer, strlen(response_buffer), 0);
            printf("Sent OK response.\n");
        } else {
            // 处理过程中发生错误
            if (output_file) {  // 如果文件已打开
                fclose(output_file);
                output_file = NULL;
                if (request_status != -2) {  // 如果不是因为文件太大（已发送413），则尝试删除不完整文件
                    if (remove(save_filepath) == 0) {
                        printf("Removed partially written file: %s\n", save_filepath);
                    } else {
                        perror("Error removing partial file");
                    }
                }
            }
            // 错误响应已在各自的错误点发送，这里不再重复发送
            fprintf(stderr, "Failed to process request. Status: %d\n", request_status);
        }
        // 关闭当前客户端连接
        close(conn_sockfd);
        printf("Connection closed.\n");
        // 循环继续，等待下一个连接...
    }  // end while(1)
    // 清理监听套接字（理论上不可达）
    printf("Audio server shutting down.\n");
    close(listen_sockfd);
    return 0;
}
