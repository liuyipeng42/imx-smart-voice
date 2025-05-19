#include <stdbool.h>
#include <stdio.h>
#include <string>
#include <vector>

#ifndef LLM_HPP
#define LLM_HPP

#define TARGET_PORT 443
#define BUFFER_SIZE 81920

#define PROXY_HOST "10.33.47.116"
#define PROXY_PORT 7897

#define GEMINI_API_KEY "test"
#define DEEPSEEK_API_KEY "test"
#define QWEN_API_KEY "test"

struct ConversationMessage {
    const std::string role;
    const std::string content;
    bool has_image;
};

enum ApiAuthMethod {
    AUTH_METHOD_NONE,
    AUTH_METHOD_URL_PARAM,     // e.g., ?key=API_KEY
    AUTH_METHOD_BEARER_HEADER  // e.g., Authorization: Bearer API_KEY
};

class LLM {
    std::string name_;           // Human-readable name
    std::string host_;           // Target hostname (e.g., "api.example.com")
    std::string path_base_;      // Base API path (e.g., "/v1/chat")
    std::string api_key_;        // The actual API key string
    ApiAuthMethod auth_method_;  // How the API key is used
    std::string model_name_;     // Optional: Model identifier (e.g., "gemini-pro")
    std::string response_search_key_;
    std::string role_;
    bool use_proxy_;

    char* Base64Encode(const unsigned char* input, int length);

    std::string GeneratePayload(std::vector<ConversationMessage>& conversation_data);

    std::string ParseResponse(const std::string& response);

    std::string JsonEscapeString(const std::string& input);

    std::string JsonUnescapeString(const std::string& input);

   public:
    LLM() = default;
    LLM(std::string name,
        std::string host,
        std::string path_base,
        std::string api_key,
        ApiAuthMethod auth_method,
        std::string model_name,
        std::string response_search_key,
        std::string role,
        bool use_proxy);
    ~LLM();

    std::string SendRequest(std::vector<ConversationMessage>& conversation_data);

    std::string role();

    std::string name();
};
#endif
