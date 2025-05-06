#include "llm_api.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

char* send_llm_request(const ApiConfig* llm_api,
                       const ConversationMessage* conversation_data,
                       int conversation_cnt) {
    char payload[BUFFER_SIZE];
    int bytes;

    // --- Generate API-specific Payload using function pointer ---
    if (generate_payload(payload, sizeof(payload), conversation_data, conversation_cnt, llm_api) < 0) {
        fprintf(stderr, "ERROR: Failed to generate JSON payload for %s.\n", llm_api->name);
        exit(EXIT_FAILURE);
    }

    // --- Initialize OpenSSL ---
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);

    // --- Create Socket ---
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(EXIT_FAILURE);
    }

    // --- Connect (Directly or via Proxy) ---
    if (llm_api->use_proxy) {
        struct sockaddr_in proxy_addr;
        memset(&proxy_addr, 0, sizeof(proxy_addr));
        proxy_addr.sin_family = AF_INET;
        proxy_addr.sin_port = htons(PROXY_PORT);
        if (inet_pton(AF_INET, PROXY_HOST, &proxy_addr.sin_addr) <= 0) {
            perror("Invalid proxy address/ Address not supported");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        if (connect(sockfd, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr)) < 0) {
            char error_msg[100];
            snprintf(error_msg, sizeof(error_msg), "ERROR connecting to proxy %s:%d", PROXY_HOST, PROXY_PORT);
            perror(error_msg);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        char connect_req[BUFFER_SIZE];
        // Use llm_api->host for the CONNECT request
        snprintf(connect_req, sizeof(connect_req),
                 "CONNECT %s:%d HTTP/1.1\r\n"
                 "Host: %s:%d\r\n"
                 "Proxy-Connection: Keep-Alive\r\n"
                 "User-Agent: C-Client/1.0\r\n"
                 "\r\n",
                 llm_api->host, TARGET_PORT, llm_api->host, TARGET_PORT);

        if (send(sockfd, connect_req, strlen(connect_req), 0) < 0) {
            perror("ERROR sending CONNECT request to proxy");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        char proxy_response_buffer[BUFFER_SIZE];
        memset(proxy_response_buffer, 0, sizeof(proxy_response_buffer));
        bytes = recv(sockfd, proxy_response_buffer, sizeof(proxy_response_buffer) - 1, 0);
        if (bytes <= 0) {
            perror("ERROR reading response from proxy");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        proxy_response_buffer[bytes] = '\0';

        if (strstr(proxy_response_buffer, "HTTP/1.") == proxy_response_buffer &&
            strstr(proxy_response_buffer, " 200 ") == NULL) {
            fprintf(stderr, "ERROR: Proxy CONNECT request failed:\n%s\n", proxy_response_buffer);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    } else {  // Direct Connection
        struct sockaddr_in target_addr;
        struct hostent* host;
        // Use llm_api->host for direct connection
        host = gethostbyname(llm_api->host);
        if (host == NULL) {
            fprintf(stderr, "ERROR: Could not resolve hostname '%s'\n", llm_api->host);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        memset(&target_addr, 0, sizeof(target_addr));
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(TARGET_PORT);
        memcpy(&target_addr.sin_addr, host->h_addr_list[0], host->h_length);
        if (connect(sockfd, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
            char error_msg[100];
            snprintf(error_msg, sizeof(error_msg), "ERROR connecting to target %s:%d", llm_api->host,
                     TARGET_PORT);
            perror(error_msg);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    // --- SSL/TLS Setup ---
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        fprintf(stderr, "ERROR creating SSL context.\n");
        ERR_print_errors_fp(stderr);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        fprintf(stderr, "ERROR creating SSL structure.\n");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (!SSL_set_fd(ssl, sockfd)) {
        fprintf(stderr, "ERROR attaching SSL to socket descriptor.\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    // Use llm_api->host for SNI
    if (SSL_set_tlsext_host_name(ssl, llm_api->host) != 1) {
        fprintf(stderr, "ERROR setting SNI hostname.\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (SSL_connect(ssl) <= 0) {
        fprintf(stderr, "ERROR in SSL handshake.\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // --- Construct the HTTPS POST Request ---
    char full_path[BUFFER_SIZE];
    char auth_header[BUFFER_SIZE / 4] = "";  // Buffer for Authorization header if needed

    // Build path and potentially Authorization header based on auth_method
    if (llm_api->auth_method == AUTH_METHOD_URL_PARAM) {
        snprintf(full_path, sizeof(full_path), "%s?key=%s", llm_api->path_base, llm_api->api_key);
    } else {
        // Assume path_base is the full path if not using URL param
        strncpy(full_path, llm_api->path_base, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';  // Ensure null termination

        if (llm_api->auth_method == AUTH_METHOD_BEARER_HEADER) {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", llm_api->api_key);
        }
    }

    char request[BUFFER_SIZE];
    // Build the final request string
    int request_len = snprintf(request, sizeof(request),
                               "POST %s HTTP/1.1\r\n"
                               "Host: %s\r\n"  // Use llm_api->host
                               "Content-Type: application/json\r\n"
                               "%s"  // Insert Authorization header (might be empty)
                               "Content-Length: %zu\r\n"
                               "Connection: close\r\n"
                               "User-Agent: C-Client/1.0\r\n"
                               "\r\n"  // End of headers
                               "%s",   // JSON payload
                               full_path,
                               llm_api->host,  // Use llm_api->host
                               auth_header,    // Contains Bearer token or is empty
                               strlen(payload), payload);

    if (request_len < 0 || request_len >= sizeof(request)) {
        fprintf(stderr, "ERROR: Failed to format HTTP request or buffer too small.\n");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // --- Send HTTPS Request over SSL ---
    bytes = SSL_write(ssl, request, request_len);
    if (bytes <= 0) {
        int ssl_error = SSL_get_error(ssl, bytes);
        fprintf(stderr, "ERROR sending HTTPS request via SSL. SSL_ERROR code: %d\n", ssl_error);
        ERR_print_errors_fp(stderr);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // --- Receive HTTPS Response over SSL ---
    char response_buffer[BUFFER_SIZE];
    memset(response_buffer, 0, sizeof(response_buffer));
    char read_chunk[BUFFER_SIZE];
    int total_response_length = 0;
    while ((bytes = SSL_read(ssl, read_chunk, sizeof(read_chunk) - 1)) > 0) {
        if (total_response_length + bytes < sizeof(response_buffer)) {
            memcpy(response_buffer + total_response_length, read_chunk, bytes);
            total_response_length += bytes;
            response_buffer[total_response_length] = '\0';
        } else {
            fprintf(stderr, "WARNING: Response buffer full, truncating response.\n");
            size_t remaining_space = sizeof(response_buffer) - total_response_length - 1;
            if (remaining_space > 0) {
                memcpy(response_buffer + total_response_length, read_chunk, remaining_space);
                total_response_length += remaining_space;
                response_buffer[total_response_length] = '\0';
            }
            break;
        }
    }

    if (bytes < 0) {
        int ssl_error = SSL_get_error(ssl, bytes);
        if (ssl_error != SSL_ERROR_ZERO_RETURN) {  // Ignore clean closure
            fprintf(stderr, "\nERROR receiving HTTPS response via SSL. SSL_ERROR code: %d\n", ssl_error);
            ERR_print_errors_fp(stderr);
        } else {
            printf("\nConnection closed by peer (SSL_ERROR_ZERO_RETURN) - Expected.\n");
        }
    }

    // --- Parse API Response using function pointer ---
    char* final_output = malloc(BUFFER_SIZE / 2);
    memset(final_output, 0, sizeof(BUFFER_SIZE / 2));
    if (parse_response(response_buffer, final_output, BUFFER_SIZE / 2, llm_api->response_search_key) != 0) {
        fprintf(stderr, "\nFailed to parse the relevant content from the %s API response.\n", llm_api->name);
    }

    // --- Cleanup ---
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    if (sockfd >= 0) {
        close(sockfd);
    }
    return final_output;
}

void json_escape_string(const char* input, char* output, size_t output_size) {
    size_t out_idx = 0;
    if (!input || !output || output_size == 0)
        return;
    output[0] = '\0';  // Ensure output is empty if input is NULL or size is 0

    while (*input && out_idx < output_size - 2) {  // Reserve space for escape char + char + null terminator
        switch (*input) {
            case '\"':
                output[out_idx++] = '\\';
                output[out_idx++] = '\"';
                break;
            case '\\':
                output[out_idx++] = '\\';
                output[out_idx++] = '\\';
                break;
            case '\b':
                output[out_idx++] = '\\';
                output[out_idx++] = 'b';
                break;
            case '\f':
                output[out_idx++] = '\\';
                output[out_idx++] = 'f';
                break;
            case '\n':
                output[out_idx++] = '\\';
                output[out_idx++] = 'n';
                break;
            case '\r':
                output[out_idx++] = '\\';
                output[out_idx++] = 'r';
                break;
            case '\t':
                output[out_idx++] = '\\';
                output[out_idx++] = 't';
                break;
            default:
                // Escape control characters (optional but good practice)
                if ((unsigned char)*input < ' ') {
                    if (out_idx < output_size - 7) {  // Need space for \u00xx
                        snprintf(output + out_idx, 7, "\\u%04x", (unsigned char)*input);
                        out_idx += 6;
                    } else {
                        goto end_escape;  // Not enough space
                    }
                } else {
                    output[out_idx++] = *input;
                }
                break;
        }
        input++;
    }

end_escape:
    output[out_idx] = '\0';
    if (*input) {
        fprintf(stderr, "Warning: JSON string escaping truncated due to buffer size limit.\n");
    }
}

void json_unescape_string(char* str) {
    char* read_ptr = str;
    char* write_ptr = str;
    if (!str)
        return;

    while (*read_ptr) {
        if (*read_ptr == '\\' && *(read_ptr + 1) != '\0') {
            read_ptr++;  // Skip backslash
            switch (*read_ptr) {
                case 'n':
                    *write_ptr++ = '\n';
                    break;
                case 't':
                    *write_ptr++ = '\t';
                    break;
                case 'r':
                    *write_ptr++ = '\r';
                    break;
                case 'b':
                    *write_ptr++ = '\b';
                    break;
                case 'f':
                    *write_ptr++ = '\f';
                    break;
                case '"':
                    *write_ptr++ = '"';
                    break;
                case '\\':
                    *write_ptr++ = '\\';
                    break;
                case '/':
                    *write_ptr++ = '/';
                    break;  // Common to unescape '/'
                case 'u':   // Handle basic unicode (assuming \uXXXX format) - simplistic
                    if (strlen(read_ptr + 1) >= 4) {
                        // Very basic: assumes ASCII range for simplicity, ignores actual unicode conversion
                        // A real implementation needs proper unicode handling.
                        *write_ptr++ = '?';  // Placeholder for unhandled unicode
                        read_ptr += 4;       // Skip the 4 hex digits
                    } else {
                        *write_ptr++ = '\\';  // Keep incomplete escape
                        *write_ptr++ = 'u';
                    }
                    break;
                default:  // Keep unrecognized escapes as is
                    *write_ptr++ = '\\';
                    *write_ptr++ = *read_ptr;
                    break;
            }
        } else {
            *write_ptr++ = *read_ptr;
        }
        read_ptr++;
    }
    *write_ptr = '\0';  // Null-terminate the unescaped string
}

int generate_payload(char* buffer,
                     size_t buffer_size,
                     const ConversationMessage* conversation_data,
                     int conversation_cnt,
                     const struct ApiConfig* config) {
    int written = 0;
    const ConversationMessage* messages;

    // Check if we're dealing with a multi-turn conversation or single prompt
    if (conversation_data->role == NULL) {
        // Single prompt case (backward compatibility)
        const char* prompt = (const char*)conversation_data;
        char escaped_prompt[BUFFER_SIZE / 2];
        json_escape_string(prompt, escaped_prompt, sizeof(escaped_prompt));

        ConversationMessage single_message = {"user", escaped_prompt};
        messages = &single_message;
    } else {
        // Multi-turn conversation case
        messages = (const ConversationMessage*)conversation_data;
    }

    // Generate payload based on API type
    if (strcmp(config->name, "Gemini") == 0) {
        written = snprintf(buffer, buffer_size, "{ \"contents\": [");
        if (written < 0 || (size_t)written >= buffer_size)
            return -1;

        size_t current_pos = written;
        for (int i = 0; i < conversation_cnt; i++) {
            char escaped_content[BUFFER_SIZE / 4];
            json_escape_string(messages[i].content, escaped_content, sizeof(escaped_content));

            int msg_written = snprintf(buffer + current_pos, buffer_size - current_pos,
                                       "%s{\"role\": \"%s\", \"parts\": [{\"text\": \"%s\"}]}",
                                       i > 0 ? ", " : "", messages[i].role, escaped_content);

            if (msg_written < 0 || (size_t)(current_pos + msg_written) >= buffer_size)
                return -1;
            current_pos += msg_written;
        }

        int end_written = snprintf(buffer + current_pos, buffer_size - current_pos, "]}");
        if (end_written < 0 || (size_t)(current_pos + end_written) >= buffer_size)
            return -1;
        written = current_pos + end_written;

    } else if (strcmp(config->name, "DeepSeek") == 0) {
        written = snprintf(buffer, buffer_size, "{\"model\": \"%s\", \"messages\": [", config->model_name);
        if (written < 0 || (size_t)written >= buffer_size)
            return -1;

        size_t current_pos = written;
        for (int i = 0; i < conversation_cnt; i++) {
            char escaped_content[BUFFER_SIZE / 4];
            json_escape_string(messages[i].content, escaped_content, sizeof(escaped_content));

            int msg_written = snprintf(buffer + current_pos, buffer_size - current_pos,
                                       "%s{\"role\": \"%s\", \"content\": \"%s\"}", i > 0 ? ", " : "",
                                       messages[i].role, escaped_content);

            if (msg_written < 0 || (size_t)(current_pos + msg_written) >= buffer_size)
                return -1;
            current_pos += msg_written;
        }

        int end_written = snprintf(buffer + current_pos, buffer_size - current_pos, "], \"stream\": false}");
        if (end_written < 0 || (size_t)(current_pos + end_written) >= buffer_size)
            return -1;
        written = current_pos + end_written;

    } else if (strcmp(config->name, "Qwen") == 0) {
        written = snprintf(buffer, buffer_size, "{\"model\": \"%s\", \"messages\": [", config->model_name);
        if (written < 0 || (size_t)written >= buffer_size)
            return -1;

        size_t current_pos = written;
        for (int i = 0; i < conversation_cnt; i++) {
            char escaped_content[BUFFER_SIZE / 4];
            json_escape_string(messages[i].content, escaped_content, sizeof(escaped_content));

            int msg_written =
                snprintf(buffer + current_pos, buffer_size - current_pos,
                         "%s{\"role\": \"%s\", \"content\": [{\"type\": \"text\", \"text\": \"%s\"}]}",
                         i > 0 ? ", " : "", messages[i].role, escaped_content);

            if (msg_written < 0 || (size_t)(current_pos + msg_written) >= buffer_size)
                return -1;
            current_pos += msg_written;
        }

        int end_written = snprintf(buffer + current_pos, buffer_size - current_pos, "], \"stream\": false}");
        if (end_written < 0 || (size_t)(current_pos + end_written) >= buffer_size)
            return -1;
        written = current_pos + end_written;

    } else {
        fprintf(stderr, "ERROR: Unsupported API configuration.\n");
        return -1;
    }

    return written;
}

int parse_response(const char* response, char* output_buffer, size_t output_size, const char* search_key) {
    char* start = strstr(response, search_key);
    if (!start) {
        fprintf(stderr, "ERROR [Parse]: Could not find '%s' in response body.\n", search_key);
        return -1;
    }
    start += strlen(search_key);

    char* end = start;
    while ((end = strchr(end, '"')) != NULL) {
        if (*(end - 1) != '\\')
            break;
        end++;
    }

    if (!end) {
        fprintf(stderr, "ERROR [Parse]: Could not find closing quote for text field.\n");
        return -1;
    }

    size_t len = end - start;
    if (len >= output_size) {
        fprintf(stderr, "Warning [Parse]: Parsed text truncated, buffer too small (needed %zu, have %zu)\n",
                len + 1, output_size);
        len = output_size - 1;
    }
    strncpy(output_buffer, start, len);
    output_buffer[len] = '\0';
    json_unescape_string(output_buffer);  // Unescape the result
    return 0;
}

// int main(int argc, char* argv[]) {
//     if (argc < 3) {
//         fprintf(stderr, "Usage: %s <prompt> <API_NAME> [--proxy]\n", argv[0]);
//         return EXIT_FAILURE;
//     }

//     const char* prompt = argv[1];
//     const char* api_name = argv[2];
//     bool use_proxy = (argc > 3 && strcmp(argv[3], "--proxy") == 0);

//     const ApiConfig* selected_api = NULL;

//     if (strcmp(api_name, "gemini") == 0) {
//         selected_api = &gemini_config;
//     } else if (strcmp(api_name, "deepseek") == 0) {
//         selected_api = &deepseek_config;
//     } else if (strcmp(api_name, "qwen") == 0) {
//         selected_api = &qwen_config;
//     } else {
//         fprintf(stderr, "ERROR: Unsupported API name '%s'. Supported APIs: gemini, deepseek, qwen.\n",
//                 api_name);
//         return EXIT_FAILURE;
//     }

//     send_llm_request(selected_api, prompt, use_proxy);
//     return EXIT_SUCCESS;
// }
