#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Base64编码函数
char* base64_encode(const unsigned char* input, int length) {
    BIO *bio, *b64;
    BUF_MEM* bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    char* output = (char*)malloc(bufferPtr->length + 1);
    memcpy(output, bufferPtr->data, bufferPtr->length);
    output[bufferPtr->length] = '\0';

    BIO_free_all(bio);
    return output;
}

int qwen_img() {
    const char* api_key = "sk-4043fd4940064916a9fbc11efece7603";

    const char* image_path = "./IMG_0545.JPG";
    std::ifstream file(image_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        perror("Can't open image");
        return 1;
    }

    std::streamsize fsize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> data(fsize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fsize)) {
        perror("Failed to read image");
        return 1;
    }

    std::unique_ptr<char, decltype(&free)> b64(base64_encode(data.data(), fsize), free);

    std::string payload =
        "{"
        "\"model\":\"qwen-vl-plus\","
        "\"messages\":[{"
        "\"role\":\"user\","
        "\"content\":["
        "{\"type\":\"image_url\","
        "\"image_url\":{\"url\":\"data:image/jpeg;base64," +
        std::string(b64.get()) +
        "\"}},"
        "{\"type\":\"text\",\"text\":\"描述这张图片，分别用中文，英文2种方式\"}"
        "]}]}";

    // --- Initialize OpenSSL ---
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
    OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);

    // --- Create Socket ---
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    std::string host = "dashscope.aliyuncs.com";
    struct hostent* hostent = gethostbyname(host.c_str());

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(443);
    memcpy(&target_addr.sin_addr, hostent->h_addr_list[0], hostent->h_length);

    connect(sockfd, reinterpret_cast<struct sockaddr*>(&target_addr), sizeof(target_addr));

    // --- SSL/TLS Setup ---
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl(SSL_new(ctx.get()), SSL_free);
    SSL_set_fd(ssl.get(), sockfd);
    SSL_set_tlsext_host_name(ssl.get(), host.c_str());
    SSL_connect(ssl.get());

    std::string full_path = "/compatible-mode/v1/chat/completions";
    std::string auth_header = "Authorization: Bearer " + std::string(api_key) + "\r\n";

    std::string request = "POST " + full_path + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" +
                          "Content-Type: application/json\r\n" + auth_header +
                          "Content-Length: " + std::to_string(payload.size()) + "\r\n" +
                          "Connection: close\r\n" + "User-Agent: C++-Client/1.0\r\n" + "\r\n" + payload;

    // --- Send HTTPS Request over SSL ---
    int bytes = SSL_write(ssl.get(), request.c_str(), request.length());

    // --- Receive HTTPS Response over SSL ---
    std::string response_buffer;
    response_buffer.reserve(81920);
    std::vector<char> read_chunk(81920);

    while ((bytes = SSL_read(ssl.get(), read_chunk.data(), read_chunk.size() - 1)) > 0) {
        read_chunk[bytes] = '\0';
        response_buffer.append(read_chunk.data(), bytes);
    }

    std::cout << response_buffer << std::endl;

    // Close socket manually since we're not using a RAII wrapper for it
    close(sockfd);

    return 0;
}
