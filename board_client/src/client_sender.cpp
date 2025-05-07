#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>

#include <iostream>
#include "client_sender.h"

ClientSender::ClientSender(const std::string& ip_address, int port) : ip_address_(ip_address), port_(port) {}

int ClientSender::AudioSend(const std::string& wav_file_path, const std::string& request_path) {
    int sockfd = -1;
    struct sockaddr_in server_addr;
    FILE* wav_file = nullptr;

    // Get file stats
    struct stat file_stat;
    if (stat(wav_file_path.c_str(), &file_stat) < 0) {
        perror("Error getting file stats");
        return -1;
    }
    const long file_size = file_stat.st_size;

    // Open WAV file
    wav_file = fopen(wav_file_path.c_str(), "rb");
    if (!wav_file) {
        perror("Error opening WAV file");
        return -1;
    }

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error creating socket");
        fclose(wav_file);
        return -1;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, ip_address_.c_str(), &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    // Build HTTP header
    std::stringstream header_stream;
    header_stream << "POST " << (request_path.empty() ? "/" : request_path) << " HTTP/1.1\r\n"
                  << "Host: " << ip_address_ << ":" << port_ << "\r\n"
                  << "Content-Type: audio/wav\r\n"
                  << "Content-Length: " << file_size << "\r\n"
                  << "Connection: close\r\n\r\n";
    const std::string header = header_stream.str();

    // Send HTTP header
    ssize_t bytes_sent = send(sockfd, header.c_str(), header.size(), 0);
    if (bytes_sent < 0 || static_cast<size_t>(bytes_sent) != header.size()) {
        perror("Error sending HTTP header");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    char data_buffer[SEND_BUFFER_SIZE];
    long total_bytes_sent = 0;
    ssize_t bytes_read;
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
        }
    }

    if (ferror(wav_file)) {
        perror("Error reading file");
        fclose(wav_file);
        close(sockfd);
        return -1;
    }

    fclose(wav_file);
    close(sockfd);

    return 0;
}

int ClientSender::LlmReponseSend(const std::string& text_to_send, const std::string& request_path) {
    int sockfd = -1;
    struct sockaddr_in server_addr;

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error creating socket");
        return -1;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, ip_address_.c_str(), &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return -1;
    }

    // Build HTTP header
    std::stringstream header_stream;
    header_stream << "POST " << (request_path.empty() ? "/" : request_path) << " HTTP/1.1\r\n"
                  << "Host: " << ip_address_ << ":" << port_ << "\r\n"
                  << "Content-Type: text/plain; charset=utf-8\r\n"
                  << "Content-Length: " << text_to_send.size() << "\r\n"
                  << "Connection: close\r\n\r\n";
    const std::string header = header_stream.str();

    // Send HTTP header
    ssize_t bytes_sent = send(sockfd, header.c_str(), header.size(), 0);
    if (bytes_sent < 0 || static_cast<size_t>(bytes_sent) != header.size()) {
        perror("Error sending HTTP header");
        close(sockfd);
        return -1;
    }

    // Send text data
    bytes_sent = send(sockfd, text_to_send.c_str(), text_to_send.size(), 0);
    if (bytes_sent < 0 || static_cast<size_t>(bytes_sent) != text_to_send.size()) {
        perror("Error sending text");
        close(sockfd);
        return -1;
    }

    close(sockfd);
    return 0;
}
