#include "client_receive.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

int setup_listen_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket() failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt() failed");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(port)};

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind() failed");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("listen() failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int send_all(int sockfd, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n;
        do {
            n = send(sockfd, buf + sent, len - sent, MSG_NOSIGNAL);
        } while (n == -1 && errno == EINTR);

        if (n <= 0)
            return -1;
        sent += n;
    }
    return 0;
}

void send_error_response(int sockfd, int code, const char* message) {
    const char* status_lines[] = {
        [400] = "HTTP/1.1 400 Bad Request",
        [411] = "HTTP/1.1 411 Length Required",
        [413] = "HTTP/1.1 413 Payload Too Large",
        [500] = "HTTP/1.1 500 Internal Server Error",
    };

    const char* status_line = status_lines[code];
    if (!status_line)
        status_line = "HTTP/1.1 500 Internal Server Error";

    char headers[MAX_ERROR_MSG];
    int len = snprintf(headers, sizeof(headers),
                       "%s\r\n"
                       "Connection: close\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: %zu\r\n\r\n%s",
                       status_line, strlen(message), message);

    if (len < 0 || (size_t)len >= sizeof(headers)) {
        const char* fallback =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Connection: close\r\nContent-Length: 22\r\n\r\n"
            "Internal server error";
        send_all(sockfd, fallback, strlen(fallback));
    } else {
        send_all(sockfd, headers, len);
    }
}

int parse_headers(const char* headers, RequestInfo* info) {
    char* headers_copy = strdup(headers);
    char* request_line = strdup(strtok(headers_copy, "\r\n"));
    info->method = strdup(strtok(request_line, " "));
    info->uri = strdup(strtok(NULL, " "));
    info->version = strdup(strtok(NULL, " \r\n"));

    if (strcmp(info->method, "POST") != 0) {
        free(info->method);
        free(info->uri);
        free(info->version);
        return -1;
    }
    free(headers_copy);

    headers_copy = strdup(headers);
    char* line = strtok(headers_copy, "\r\n");
    int found = 0;
    while (line) {
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            if (found)
                return -1;

            char* value = line + 15;
            while (*value && isspace(*value))
                value++;

            char* end;
            long val = strtol(value, &end, 10);
            if (*end || val < 0)
                return -1;

            info->content_length = val;
            found = 1;
        }
        line = strtok(NULL, "\r\n");
    }
    return found ? 0 : -1;
}

int receive_headers(int sockfd, char* buf, size_t buf_size, RequestInfo* info) {
    size_t received = 0;
    char* headers_end = NULL;

    while (received < buf_size && !headers_end) {
        ssize_t n;
        do {
            n = recv(sockfd, buf + received, buf_size - received, 0);
        } while (n == -1 && errno == EINTR);

        if (n <= 0)
            return -1;

        received += n;
        buf[received] = '\0';
        headers_end = strstr(buf, "\r\n\r\n");
    }

    if (!headers_end)
        return -1;

    info->header_len = headers_end - buf;
    info->body_start = headers_end + 4;
    info->initial_body = received - (info->body_start - buf);

    return 0;
}

void handle_audio_upload(int sockfd, RequestInfo* info, const char* file_path, long long max_size) {
    if (info->content_length > max_size) {
        send_error_response(sockfd, 413, "Payload too large");
        return;
    }
    FILE* fp = fopen(file_path, "wb");
    if (!fp) {
        send_error_response(sockfd, 500, "Failed to create file");
        return;
    }
    size_t written = 0;
    if (info->initial_body > 0) {
        size_t to_write = info->initial_body > (size_t)info->content_length ? (size_t)info->content_length
                                                                            : info->initial_body;
        if (fwrite(info->body_start, 1, to_write, fp) != to_write) {
            fclose(fp);
            remove(file_path);
            send_error_response(sockfd, 500, "File write error");
            return;
        }
        written += to_write;
    }
    char buffer[RECEIVE_BUFFER_SIZE];
    while (written < (size_t)info->content_length) {
        size_t remain = info->content_length - written;
        size_t to_read = remain > sizeof(buffer) ? sizeof(buffer) : remain;
        ssize_t n;
        do {
            n = recv(sockfd, buffer, to_read, 0);
        } while (n == -1 && errno == EINTR);
        if (n <= 0) {
            fclose(fp);
            remove(file_path);
            send_error_response(sockfd, 400, "Incomplete body");
            return;
        }
        if (fwrite(buffer, 1, n, fp) != (size_t)n) {
            fclose(fp);
            remove(file_path);
            send_error_response(sockfd, 500, "File write error");
            return;
        }
        written += n;
    }
    fclose(fp);
    char response[256];
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 200 OK\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n\r\n"
                       "File received: %s",
                       strlen(file_path) + 15, file_path);
    if (len > 0 && (size_t)len < sizeof(response)) {
        send_all(sockfd, response, len);
    }
}

char* handle_text_request(int sockfd, RequestInfo* info, size_t max_size) {
    if ((size_t)info->content_length > max_size) {
        send_error_response(sockfd, 413, "Payload too large");
        return NULL;
    }
    char* body = malloc(info->content_length + 1);
    if (!body) {
        send_error_response(sockfd, 500, "Out of memory");
        return NULL;
    }
    size_t received = 0;
    if (info->initial_body > 0) {
        size_t to_copy = info->initial_body > (size_t)info->content_length ? (size_t)info->content_length
                                                                           : info->initial_body;
        memcpy(body, info->body_start, to_copy);
        received = to_copy;
    }
    while (received < (size_t)info->content_length) {
        size_t remain = info->content_length - received;
        ssize_t n;
        do {
            n = recv(sockfd, body + received, remain, 0);
        } while (n == -1 && errno == EINTR);
        if (n <= 0) {
            free(body);
            send_error_response(sockfd, 400, "Incomplete body");
            return NULL;
        }
        received += n;
    }
    body[info->content_length] = '\0';
    const char* response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 15\r\n"
        "Connection: close\r\n\r\n"
        "Text received.\n";
    send_all(sockfd, response, strlen(response));
    return body;
}

char* handle_request(int listen_sock, char* file_path) {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &addrlen);
    if (client_sock < 0) {
        perror("accept() failed");
        return NULL;
    }
    char headers[MAX_HEADER_SIZE];
    RequestInfo info = {0};
    // 接收并解析请求头
    if (receive_headers(client_sock, headers, sizeof(headers), &info) < 0) {
        send_error_response(client_sock, 413, "Header too large or incomplete");
        close(client_sock);
        return NULL;
    }

    // 解析Content-Length
    if (parse_headers(headers, &info) < 0) {
        send_error_response(client_sock, 411, "Content-Length required");
        close(client_sock);
        return NULL;
    }
    char* result = NULL;
    if (strcmp(info.uri, "/upload/audio") == 0) {
        printf("Received audio upload request\n");
        handle_audio_upload(client_sock, &info, file_path, MAX_AUDIO_SIZE);
    } else if (strcmp(info.uri, "/upload/text") == 0) {
        printf("Received text upload request\n");
        result = handle_text_request(client_sock, &info, MAX_TEXT_SIZE);
    } else {
        send_error_response(client_sock, 404, "Not Found");
    }
    close(client_sock);
    return result;
}