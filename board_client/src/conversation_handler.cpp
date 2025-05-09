#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <iostream>
#include <vector>

#include "conversation_handler.h"
#include "llm.h"
#include "v4l2_camera.h"

ConversationHandler::ConversationHandler(std::string db_path) {
    // LLM qwen = LLM("Qwen", "dashscope.aliyuncs.com", "/compatible-mode/v1/chat/completions", QWEN_API_KEY,
    //                AUTH_METHOD_BEARER_HEADER, "qwen-vl-plus", "\"content\":\"", "system", false);

    // LLM deepseek = LLM("DeepSeek", "api.deepseek.com", "/chat/completions", DEEPSEEK_API_KEY,
    //                    AUTH_METHOD_BEARER_HEADER, "deepseek-chat", "\"content\":\"", "assistant", false);

    // LLM gemini = LLM("Gemini", "generativelanguage.googleapis.com",
    //                  "/v1beta/models/gemini-2.5-flash-preview-04-17:generateContent", GEMINI_API_KEY,
    //                  AUTH_METHOD_URL_PARAM, "gemini-2.5-flash-preview-04-17", "\"text\": \"", "model",
    //                  true);

    key_fd_ = open("/dev/key", O_RDWR);
    if (key_fd_ < 0)
        printf("Can't open file");

    llm_ = new LLM("Qwen", "dashscope.aliyuncs.com", "/compatible-mode/v1/chat/completions", QWEN_API_KEY,
                   AUTH_METHOD_BEARER_HEADER, "qwen-vl-plus", "\"content\":\"", "system", false);

    sender_ = new ClientSender("10.33.47.116", 8000);
    receiver_ = new ClientReceiver(8001);

    chat_record_db_ = new ChatRecordDB(db_path);
    int ret = chat_record_db_->InitDatabase();
    if (ret != SQLITE_OK) {
        printf("Database initialization failed\n");
        return;
    }
}

ConversationHandler::~ConversationHandler() {
    if (key_fd_ >= 0) {
        close(key_fd_);
    }
}

void ConversationHandler::ReceiveConvoID(int conversation_id) {
    current_conversation_id_ = conversation_id;
    std::cout << "Received conversation ID: " << conversation_id << std::endl;
}

void ConversationHandler::HasImage() {
    has_image_ = true;
}

void ConversationHandler::run() {
    unsigned char key_status;
    FILE* arecord_pipe = nullptr;
    std::string status;
    std::string value;
    while (true) {
        read(key_fd_, &key_status, sizeof(key_status));

        if (key_status == 1) {
            emit SendConvoStatus(const_cast<char*>("Recoding started"), const_cast<char*>(""));
            arecord_pipe = popen("arecord -f cd ./record.wav", "r");
        }

        if (key_status == 2) {
            system("pkill arecord");
            pclose(arecord_pipe);

            std::vector<ConversationMessage> conversation_data = {
                {llm_->role().c_str(),
                 "回复中的数字不要使用阿拉伯数字，使用中文数字，回复不要太长，在200字以内，回复中只回应以"
                 "下内容：\n\n",
                 false}};

            std::vector<ChatRecordDB::Message> messages =
                chat_record_db_->QueryMessagesOfConversation(current_conversation_id_);
            if (!messages.empty()) {
                for (const auto& message : messages) {
                    conversation_data.push_back({message.role.c_str(), message.message.c_str(), false});
                }
            }

            emit SendConvoStatus(const_cast<char*>("Recording finished"), const_cast<char*>(""));

            // std::string filename = "./test_audios/test" + std::to_string((test_audio_id % 4) + 1) + ".wav";
            // sender_->AudioSend(filename, "/upload/audio");
            // test_audio_id++;
            sender_->AudioSend("./record.wav", "/upload/audio");
            emit SendConvoStatus(const_cast<char*>("Audio sent"), const_cast<char*>(""));

            std::string audio_text = receiver_->HandleRequest("");
            emit SendConvoStatus(const_cast<char*>("Audio text received"),
                                 const_cast<char*>(audio_text.c_str()));

            conversation_data.push_back({"user", audio_text.c_str(), has_image_});

            emit SendConvoStatus(const_cast<char*>("LLM requesting"), const_cast<char*>(""));
            std::string response = llm_->SendRequest(conversation_data);
            emit SendConvoStatus(const_cast<char*>("LLM response received"),
                                 const_cast<char*>(response.c_str()));
            has_image_ = false;

            emit SendConvoStatus(const_cast<char*>("Generating audio"), const_cast<char*>(response.c_str()));
            sender_->LlmReponseSend(response, "/send/text");
            std::string response_filename = "./llm_response_audio.wav";
            if (access(response_filename.c_str(), F_OK) == 0)
                remove(response_filename.c_str());
            receiver_->HandleRequest(response_filename);
            emit SendConvoStatus(const_cast<char*>("Response audio received"),
                                 const_cast<char*>(response.c_str()));

            std::string aplay_command = "aplay " + response_filename;
            FILE* aplay_pipe = popen(aplay_command.c_str(), "r");
            if (pclose(aplay_pipe) == 0)
                printf("音频播放完成\n");
        }
    }
}