
#define SEND_BUFFER_SIZE 8192            // 8KB buffer for reading/writing
#define MAX_PENDING_CONNECTIONS 10  // Max pending connections for the server

int audio_send(const char* ip_address, int port, const char* wav_filepath, const char* request_path);

int llm_reponse_send(const char* ip_address, int port, const char* text_to_send, const char* request_path);

