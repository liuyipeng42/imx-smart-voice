#ifndef CLIENT_SENDER_H
#define CLIENT_SENDER_H

#include <string>

class ClientSender {
   private:
    static const size_t SEND_BUFFER_SIZE = 8192;

    std::string ip_address_;
    int port_;

   public:
    ClientSender(const std::string& ip_address, int port);

    int AudioSend(const std::string& wav_file_path, const std::string& request_path);
    int LlmReponseSend(const std::string& text_to_send, const std::string& request_path);
};

#endif  // CLIENT_SENDER_H
