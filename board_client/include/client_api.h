#include <stddef.h>

#define BUFFER_SIZE 8192            // 8KB buffer for reading/writing
#define MAX_PENDING_CONNECTIONS 10  // Max pending connections for the server

int audio_send(const char* ip_address, int port, const char* wav_filepath, const char* request_path);

int audio_text_receive(int listen_port, size_t max_content_length);

int llm_reponse_send(const char* ip_address, int port, const char* text_to_send, const char* request_path);

long long audio_receive(int listen_port, const char* save_dir, long long max_file_size);

