#ifndef CLIENT_SRECEIVER_H
#define CLIENT_SRECEIVER_H

#include <string>
#include <vector>

class ClientSReceiver {
   public:
    static constexpr int MAX_PENDING_CONNECTIONS = 10;
    static constexpr size_t RECEIVE_BUFFER_SIZE = 16384;
    static constexpr size_t MAX_HEADER_SIZE = 8192;
    static constexpr size_t MAX_ERROR_MSG = 512;
    static constexpr long MAX_AUDIO_SIZE = 10 * 1024 * 1024;
    static constexpr long MAX_TEXT_SIZE = 1 * 1024 * 1024;

    ClientSReceiver(int port);
    ~ClientSReceiver();

    std::string HandleRequest(const std::string& file_path);

   private:
    struct RequestInfo {
        std::string method;
        std::string uri;
        std::string version;
        long content_length = 0;
        size_t header_len = 0;
        size_t body_start_offset = 0;
        size_t initial_body = 0;
    };

    int SetupListenSocket(int port);

    int SendAll(int sockfd, const char* buf, size_t len);

    void SendErrorResponse(int sockfd, int code, const std::string& message);

    int ParseHeaders(const std::string& headers, RequestInfo& info);

    int ReceiveHeaders(int sockfd_, RequestInfo& info);

    void HandleAudioUpload(int sockfd,
                           const RequestInfo& info,
                           const std::string& file_path,
                           long long max_size);

    std::string HandleTextRequest(int sockfd, RequestInfo& info, size_t max_size);

    int listen_socket_;
    std::vector<char> receive_buffer_;
};

#endif  // CLIENT_SRECEIVER_H
