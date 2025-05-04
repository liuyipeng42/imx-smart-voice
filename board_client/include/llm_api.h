#include <stdbool.h>
#include <stdio.h>

#define TARGET_PORT 443
#define BUFFER_SIZE 8192

#define PROXY_HOST "10.33.47.116"
#define PROXY_PORT 7897

#define GEMINI_API_KEY "AIzaSyCSeZma5tqzDMdUUy5oRxlSeyIR_0HeXUI"
#define DEEPSEEK_API_KEY "sk-9d419252992248f3bbe6fe260b45900f"
#define QWEN_API_KEY "sk-4043fd4940064916a9fbc11efece7603"

typedef enum {
    AUTH_METHOD_NONE,
    AUTH_METHOD_URL_PARAM,     // e.g., ?key=API_KEY
    AUTH_METHOD_BEARER_HEADER  // e.g., Authorization: Bearer API_KEY
} ApiAuthMethod;

typedef struct ApiConfig {
    const char* name;           // Human-readable name
    const char* host;           // Target hostname (e.g., "api.example.com")
    const char* path_base;      // Base API path (e.g., "/v1/chat")
    const char* api_key;        // The actual API key string
    ApiAuthMethod auth_method;  // How the API key is used
    const char* model_name;     // Optional: Model identifier (e.g., "gemini-pro")
    const char* response_search_key;
} ApiConfig;

char* send_llm_request(const ApiConfig* llm_api, const char* user_prompt, bool USE_PROXY);

int generate_payload(char* buffer, size_t buffer_size, const char* prompt, const struct ApiConfig* config);

int parse_response(const char* response, char* output_buffer, size_t output_size, const char* search_key);