#include <stdio.h>

#define MAX_PENDING_CONNECTIONS 10
#define RECEIVE_BUFFER_SIZE 16384
#define MAX_HEADER_SIZE 8192
#define PATH_MAX 4096
#define MAX_ERROR_MSG 512
#define MAX_AUDIO_SIZE (10 * 1024 * 1024)  // 10MB
#define MAX_TEXT_SIZE (1 * 1024 * 1024)    // 1MB

typedef struct {
    char* method;
    char* uri;
    char* version;
    long content_length;
    char* body_start;
    size_t header_len;
    size_t initial_body;
} RequestInfo;

int setup_listen_socket(int port);

char* handle_request(int listen_sock, char* file_path);