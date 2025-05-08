#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <iostream>
#include "client_receiver.h"

ClientReceiver::ClientReceiver(int port) : listen_socket_(-1) {
    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ < 0) {
        perror("socket() failed");
    }

    int opt = 1;
    if (setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt() failed");
        close(listen_socket_);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_socket_, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("bind() failed");
        close(listen_socket_);
    }

    if (listen(listen_socket_, MAX_PENDING_CONNECTIONS) < 0) {
        perror("listen() failed");
        close(listen_socket_);
    }
}

ClientReceiver::~ClientReceiver() {
    if (listen_socket_ != -1) {
        close(listen_socket_);
    }
}

int ClientReceiver::SendAll(int sockfd, const char* buf, size_t len) {
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

void ClientReceiver::SendErrorResponse(int sockfd, int code, const std::string& message) {
    const std::string status_lines[] = {
        "HTTP/1.1 400 Bad Request",
        "HTTP/1.1 411 Length Required",
        "HTTP/1.1 413 Payload Too Large",
        "HTTP/1.1 500 Internal Server Error",
    };

    std::string status_line;
    switch (code) {
        case 400:
            status_line = status_lines[0];
            break;
        case 411:
            status_line = status_lines[1];
            break;
        case 413:
            status_line = status_lines[2];
            break;
        default:
            status_line = status_lines[3];
            break;
    }

    std::string headers = status_line +
                          "\r\n"
                          "Connection: close\r\n"
                          "Content-Type: text/plain\r\n"
                          "Content-Length: " +
                          std::to_string(message.size()) + "\r\n\r\n" + message;

    if (headers.size() > MAX_ERROR_MSG) {
        const char* fallback =
            "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 22\r\n\r\nInternal "
            "server error";
        SendAll(sockfd, fallback, strlen(fallback));
    } else {
        SendAll(sockfd, headers.c_str(), headers.size());
    }
}

int ClientReceiver::ParseHeaders(const std::string& headers, RequestInfo& info) {
    std::istringstream stream(headers);
    std::string line;

    // Parse request line
    if (!std::getline(stream, line))
        return -1;
    if (line.back() == '\r')
        line.pop_back();

    size_t pos1 = line.find(' ');
    if (pos1 == std::string::npos)
        return -1;
    info.method = line.substr(0, pos1);

    size_t pos2 = line.find(' ', pos1 + 1);
    if (pos2 == std::string::npos)
        return -1;
    info.uri = line.substr(pos1 + 1, pos2 - pos1 - 1);
    info.version = line.substr(pos2 + 1);

    if (info.method != "POST")
        return -1;

    // Parse headers
    bool found_cl = false;
    while (std::getline(stream, line)) {
        if (line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;

        if (line.find("Content-Length:") == 0) {
            size_t colon = line.find(':');
            size_t value_start = line.find_first_not_of(" \t", colon + 1);
            if (value_start == std::string::npos)
                return -1;

            try {
                info.content_length = std::stol(line.substr(value_start));
                found_cl = true;
            } catch (...) {
                return -1;
            }
        }
    }
    return found_cl ? 0 : -1;
}

int ClientReceiver::ReceiveHeaders(int sockfd, RequestInfo& info) {
    receive_buffer_.resize(MAX_HEADER_SIZE);
    size_t received = 0;
    char* headers_end = nullptr;

    while (received < MAX_HEADER_SIZE && !headers_end) {
        ssize_t n = recv(sockfd, &receive_buffer_[received], MAX_HEADER_SIZE - received, 0);
        if (n <= 0)
            return -1;
        received += n;

        receive_buffer_[received] = '\0';
        headers_end = strstr(&receive_buffer_[0], "\r\n\r\n");
    }

    if (!headers_end)
        return -1;

    info.header_len = headers_end - &receive_buffer_[0];
    info.body_start_offset = headers_end + 4 - &receive_buffer_[0];
    info.initial_body = received - info.body_start_offset;

    return 0;
}

void ClientReceiver::HandleAudioUpload(int sockfd,
                                        const RequestInfo& info,
                                        const std::string& file_path,
                                        long long max_size) {
    if (info.content_length > max_size) {
        SendErrorResponse(sockfd, 413, "Payload too large");
        return;
    }

    FILE* fp = fopen(file_path.c_str(), "wb");
    if (!fp) {
        SendErrorResponse(sockfd, 500, "Failed to create file");
        return;
    }

    size_t written = 0;
    if (info.initial_body > 0) {
        size_t to_write = std::min(info.initial_body, static_cast<size_t>(info.content_length));
        if (fwrite(&receive_buffer_[info.body_start_offset], 1, to_write, fp) != to_write) {
            fclose(fp);
            remove(file_path.c_str());
            SendErrorResponse(sockfd, 500, "File write error");
            return;
        }
        written += to_write;
    }

    char buffer[RECEIVE_BUFFER_SIZE];
    while (written < static_cast<size_t>(info.content_length)) {
        size_t remain = info.content_length - written;
        size_t to_read = std::min(remain, sizeof(buffer));

        ssize_t n;
        do {
            n = recv(sockfd, buffer, to_read, 0);
        } while (n == -1 && errno == EINTR);

        if (n <= 0) {
            fclose(fp);
            remove(file_path.c_str());
            SendErrorResponse(sockfd, 400, "Incomplete body");
            return;
        }

        if (fwrite(buffer, 1, n, fp) != static_cast<size_t>(n)) {
            fclose(fp);
            remove(file_path.c_str());
            SendErrorResponse(sockfd, 500, "File write error");
            return;
        }
        written += n;
    }

    fclose(fp);
    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                           std::to_string(file_path.length() + 15) +
                           "\r\nConnection: close\r\n\r\nFile received: " + file_path;
    SendAll(sockfd, response.c_str(), response.size());
}

std::string ClientReceiver::HandleTextRequest(int sockfd, RequestInfo& info, size_t max_size) {
    if (static_cast<size_t>(info.content_length) > max_size) {
        SendErrorResponse(sockfd, 413, "Payload too large");
        return "";
    }

    std::string body;
    body.reserve(info.content_length);

    if (info.initial_body > 0) {
        size_t to_copy = std::min(info.initial_body, static_cast<size_t>(info.content_length));
        body.append(&receive_buffer_[info.body_start_offset], to_copy);
    }

    char buffer[RECEIVE_BUFFER_SIZE];
    while (body.size() < static_cast<size_t>(info.content_length)) {
        size_t remain = info.content_length - body.size();
        size_t to_read = std::min(remain, sizeof(buffer));

        ssize_t n;
        do {
            n = recv(sockfd, buffer, to_read, 0);
        } while (n == -1 && errno == EINTR);

        if (n <= 0) {
            SendErrorResponse(sockfd, 400, "Incomplete body");
            return "";
        }
        body.append(buffer, n);
    }

    const char* response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 15\r\nConnection: close\r\n\r\nText "
        "received.\n";
    SendAll(sockfd, response, strlen(response));
    return body;
}

std::string ClientReceiver::HandleRequest(const std::string& file_path) {

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int client_sock = accept(listen_socket_, (struct sockaddr*)&client_addr, &addrlen);
    if (client_sock < 0) {
        perror("accept() failed");
        return "";
    }


    RequestInfo info;
    if (ReceiveHeaders(client_sock, info) < 0) {
        SendErrorResponse(client_sock, 413, "Header too large or incomplete");
        close(client_sock);
        return "";
    }

    std::string headers_str(receive_buffer_.begin(), receive_buffer_.begin() + info.header_len);
    if (ParseHeaders(headers_str, info) < 0) {
        SendErrorResponse(client_sock, 411, "Content-Length required");
        close(client_sock);
        return "";
    }

    std::string result;
    if (info.uri == "/upload/audio") {
        HandleAudioUpload(client_sock, info, file_path, MAX_AUDIO_SIZE);
    } else if (info.uri == "/upload/text") {
        result = HandleTextRequest(client_sock, info, MAX_TEXT_SIZE);
    } else {
        SendErrorResponse(client_sock, 404, "Not Found");
    }

    close(client_sock);
    return result;
}
