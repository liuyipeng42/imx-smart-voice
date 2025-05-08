#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <memory>

#include "llm.h"

LLM::LLM(std::string name,
         std::string host,
         std::string path_base,
         std::string api_key,
         ApiAuthMethod auth_method,
         std::string model_name,
         std::string response_search_key,
         std::string role,
         bool use_proxy)
    : name_(name),
      host_(host),
      path_base_(path_base),
      api_key_(api_key),
      auth_method_(auth_method),
      model_name_(model_name),
      response_search_key_(response_search_key),
      role_(role),
      use_proxy_(use_proxy) {};

LLM::~LLM() {};

std::string LLM::SendRequest(std::vector<ConversationMessage>& conversation_data) {
    try {
        // --- Initialize OpenSSL ---
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
        OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);

        // --- Create Socket ---
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Failed to open socket: " + std::string(strerror(errno)));
        }

        // --- Connect (Directly or via Proxy) ---
        if (use_proxy_) {
            struct sockaddr_in proxy_addr;
            memset(&proxy_addr, 0, sizeof(proxy_addr));
            proxy_addr.sin_family = AF_INET;
            proxy_addr.sin_port = htons(PROXY_PORT);
            if (inet_pton(AF_INET, PROXY_HOST, &proxy_addr.sin_addr) <= 0) {
                close(sockfd);
                throw std::runtime_error("Invalid proxy address or address not supported");
            }

            if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&proxy_addr), sizeof(proxy_addr)) < 0) {
                std::string error_msg = "Error connecting to proxy " + std::string(PROXY_HOST) + ":" +
                                        std::to_string(PROXY_PORT) + " - " + strerror(errno);
                close(sockfd);
                throw std::runtime_error(error_msg);
            }

            std::string connect_req = "CONNECT " + host_ + ":" + std::to_string(TARGET_PORT) +
                                      " HTTP/1.1\r\n" + "Host: " + host_ + ":" + std::to_string(TARGET_PORT) +
                                      "\r\n" + "Proxy-Connection: Keep-Alive\r\n" +
                                      "User-Agent: C++-Client/1.0\r\n\r\n";

            if (send(sockfd, connect_req.c_str(), connect_req.length(), 0) < 0) {
                close(sockfd);
                throw std::runtime_error("Error sending CONNECT request to proxy: " +
                                         std::string(strerror(errno)));
            }

            std::vector<char> proxy_response(BUFFER_SIZE);
            int bytes = recv(sockfd, proxy_response.data(), proxy_response.size() - 1, 0);
            if (bytes <= 0) {
                close(sockfd);
                throw std::runtime_error("Error reading response from proxy: " +
                                         std::string(strerror(errno)));
            }

            proxy_response[bytes] = '\0';
            std::string response_str(proxy_response.data());

            if (response_str.find("HTTP/1.") == 0 && response_str.find(" 200 ") == std::string::npos) {
                close(sockfd);
                throw std::runtime_error("Proxy CONNECT request failed: " + response_str);
            }
        } else {  // Direct Connection
            struct hostent* hostent = gethostbyname(host_.c_str());
            if (hostent == nullptr) {
                close(sockfd);
                throw std::runtime_error("Could not resolve hostname: " + host_);
            }

            struct sockaddr_in target_addr;
            memset(&target_addr, 0, sizeof(target_addr));
            target_addr.sin_family = AF_INET;
            target_addr.sin_port = htons(TARGET_PORT);
            memcpy(&target_addr.sin_addr, hostent->h_addr_list[0], hostent->h_length);

            if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&target_addr), sizeof(target_addr)) < 0) {
                std::string error_msg = "Error connecting to target " + host_ + ":" +
                                        std::to_string(TARGET_PORT) + " - " + strerror(errno);
                close(sockfd);
                throw std::runtime_error(error_msg);
            }
        }

        // --- SSL/TLS Setup ---
        std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
        if (!ctx) {
            close(sockfd);
            throw std::runtime_error("Error creating SSL context");
        }

        SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);

        std::unique_ptr<SSL, decltype(&SSL_free)> ssl(SSL_new(ctx.get()), SSL_free);
        if (!ssl) {
            close(sockfd);
            throw std::runtime_error("Error creating SSL structure");
        }

        if (!SSL_set_fd(ssl.get(), sockfd)) {
            close(sockfd);
            throw std::runtime_error("Error attaching SSL to socket descriptor");
        }

        if (SSL_set_tlsext_host_name(ssl.get(), host_.c_str()) != 1) {
            close(sockfd);
            throw std::runtime_error("Error setting SNI hostname");
        }

        if (SSL_connect(ssl.get()) <= 0) {
            close(sockfd);
            throw std::runtime_error("Error in SSL handshake");
        }

        // --- Construct the HTTPS POST Request ---
        std::string full_path;
        std::string auth_header;

        // Build path and potentially Authorization header based on auth_method
        if (auth_method_ == AUTH_METHOD_URL_PARAM) {
            full_path = path_base_ + "?key=" + api_key_;
        } else {
            full_path = path_base_;

            if (auth_method_ == AUTH_METHOD_BEARER_HEADER) {
                auth_header = "Authorization: Bearer " + api_key_ + "\r\n";
            }
        }

        std::string payload = GeneratePayload(conversation_data);

        std::string request = "POST " + full_path + " HTTP/1.1\r\n" + "Host: " + host_ + "\r\n" +
                              "Content-Type: application/json\r\n" + auth_header +
                              "Content-Length: " + std::to_string(payload.size()) + "\r\n" +
                              "Connection: close\r\n" + "User-Agent: C++-Client/1.0\r\n" + "\r\n" + payload;

        // --- Send HTTPS Request over SSL ---
        int bytes = SSL_write(ssl.get(), request.c_str(), request.length());
        if (bytes <= 0) {
            int ssl_error = SSL_get_error(ssl.get(), bytes);
            close(sockfd);
            throw std::runtime_error("Error sending HTTPS request via SSL. SSL_ERROR code: " +
                                     std::to_string(ssl_error));
        }

        // --- Receive HTTPS Response over SSL ---
        std::string response_buffer;
        response_buffer.reserve(BUFFER_SIZE);
        std::vector<char> read_chunk(BUFFER_SIZE);

        while ((bytes = SSL_read(ssl.get(), read_chunk.data(), read_chunk.size() - 1)) > 0) {
            read_chunk[bytes] = '\0';
            response_buffer.append(read_chunk.data(), bytes);
        }

        if (bytes < 0) {
            int ssl_error = SSL_get_error(ssl.get(), bytes);
            if (ssl_error != SSL_ERROR_ZERO_RETURN) {  // Ignore clean closure
                close(sockfd);
                throw std::runtime_error("Error receiving HTTPS response via SSL. SSL_ERROR code: " +
                                         std::to_string(ssl_error));
            }
        }

        // Close socket manually since we're not using a RAII wrapper for it
        close(sockfd);

        return ParseResponse(response_buffer);

    } catch (const std::exception& e) {
        std::cerr << "Error in LLM::SendRequest: " << e.what() << std::endl;
        return "";
    }
}

char* LLM::Base64Encode(const unsigned char* input, int length) {
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

std::string LLM::GeneratePayload(std::vector<ConversationMessage>& conversation_data) {
    std::string payload;

    if (name_ == "Gemini") {
        payload = "{ \"contents\": [";

        for (size_t i = 0; i < conversation_data.size(); i++) {
            std::string escaped_content = JsonEscapeString(conversation_data[i].content);

            if (i > 0)
                payload += ", ";
            payload += "{\"role\": \"" + std::string(conversation_data[i].role) +
                       "\", \"parts\": [{\"text\": \"" + escaped_content + "\"}]}";
        }

        payload += "]}";
    } else if (name_ == "DeepSeek") {
        payload = "{\"model\": \"" + model_name_ + "\", \"messages\": [";

        for (size_t i = 0; i < conversation_data.size(); i++) {
            std::string escaped_content = JsonEscapeString(conversation_data[i].content);

            if (i > 0)
                payload += ", ";
            payload += "{\"role\": \"" + std::string(conversation_data[i].role) + "\", \"content\": \"" +
                       escaped_content + "\"}";
        }

        payload += "], \"stream\": false}";

    } else if (name_ == "Qwen") {
        payload = "{\"model\": \"" + model_name_ + "\", \"messages\": [";

        for (size_t i = 0; i < conversation_data.size(); i++) {
            std::string escaped_content = JsonEscapeString(conversation_data[i].content);

            if (i > 0)
                payload += ", ";

            if (i == conversation_data.size() - 1 && conversation_data[i].has_image) {
                const char* image_path = "./image.jpg";
                std::ifstream file(image_path, std::ios::binary | std::ios::ate);
                if (file.is_open()) {
                    std::streamsize fsize = file.tellg();
                    file.seekg(0, std::ios::beg);

                    std::vector<unsigned char> data(fsize);
                    if (file.read(reinterpret_cast<char*>(data.data()), fsize)) {
                        std::unique_ptr<char, decltype(&free)> b64(Base64Encode(data.data(), fsize), free);

                        payload +=
                            "{\"role\":\"user\", \"content\":["
                            "{\"type\":\"text\",\"text\":\"" +
                            escaped_content +
                            "\"},"
                            "{\"type\":\"image_url\", \"image_url\":{\"url\":\"data:image/jpeg;base64," +
                            std::string(b64.get()) +
                            "\"}}"
                            "]}";
                        continue;
                    }
                }
            }

            payload += "{\"role\": \"" + std::string(conversation_data[i].role) +
                       "\", \"content\": [{\"type\": \"text\", \"text\": \"" + escaped_content + "\"}]}";
        }

        payload += "]}";

        // std::cout << "Payload: " << payload << std::endl;
    }

    return payload;
}

std::string LLM::ParseResponse(const std::string& response) {
    size_t start_pos = response.find(response_search_key_);
    if (start_pos == std::string::npos) {
        std::cerr << "ERROR [Parse]: Could not find '" << response_search_key_ << "' in response body."
                  << std::endl;
        return "";
    }

    start_pos += response_search_key_.length();

    // Find the closing quote that's not escaped
    size_t end_pos = start_pos;
    while (true) {
        end_pos = response.find('"', end_pos);
        if (end_pos == std::string::npos) {
            std::cerr << "ERROR [Parse]: Could not find closing quote for text field." << std::endl;
            return "";
        }

        // Check if the quote is escaped
        if (end_pos > 0 && response[end_pos - 1] != '\\') {
            break;
        }
        end_pos++;  // Move past this quote to find the next one
    }

    std::string result = response.substr(start_pos, end_pos - start_pos);
    return JsonUnescapeString(result);
}

std::string LLM::JsonEscapeString(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 2);  // 预分配空间优化性能

    for (unsigned char c : input) {
        switch (c) {
            case '\"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (c < ' ') {  // 处理其他控制字符 (0x00-0x1F)
                    char buffer[7];
                    snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    output += buffer;
                } else {
                    output += static_cast<char>(c);
                }
                break;
        }
    }

    return output;
}

std::string LLM::JsonUnescapeString(const std::string& input) {
    std::string output;
    size_t i = 0;
    const size_t length = input.length();

    while (i < length) {
        if (input[i] == '\\' && (i + 1 < length)) {
            const char esc_char = input[++i];  // 跳过反斜杠并获取转义字符
            switch (esc_char) {
                case 'n':
                    output += '\n';
                    break;
                case 't':
                    output += '\t';
                    break;
                case 'r':
                    output += '\r';
                    break;
                case 'b':
                    output += '\b';
                    break;
                case 'f':
                    output += '\f';
                    break;
                case '"':
                    output += '"';
                    break;
                case '\\':
                    output += '\\';
                    break;
                case '/':
                    output += '/';
                    break;
                case 'u':                  // 处理Unicode转义（简化版）
                    if (i + 4 < length) {  // 检查后续4个字符是否存在
                        output += '?';     // 使用问号代替实际解码
                        i += 4;            // 跳过4个十六进制字符
                    } else {
                        output += "\\u";  // 保留不完整的转义序列
                    }
                    break;
                default:  // 保留无法识别的转义序列
                    output += '\\';
                    output += esc_char;
                    break;
            }
            i++;  // 移动到下一个字符
        } else {
            output += input[i++];  // 直接复制普通字符
        }
    }
    return output;
}

std::string LLM::role() {
    return role_;
}

std::string LLM::name() {
    return name_;
}