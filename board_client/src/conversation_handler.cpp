#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <iostream>
#include <vector>

#include "client_receiver.h"
#include "client_sender.h"
#include "conversation_handler.h"
#include "llm.h"
#include "v4l2_camera.h"

ConversationHandler::ConversationHandler(std::string db_path) {
    chat_record_db_ = ChatRecordDB(db_path);
}

void ConversationHandler::run() {
    LLM qwen = LLM("Qwen", "dashscope.aliyuncs.com", "/compatible-mode/v1/chat/completions", QWEN_API_KEY,
                   AUTH_METHOD_BEARER_HEADER, "qwen-vl-plus", "\"content\":\"", "system", false);

    LLM deepseek = LLM("DeepSeek", "api.deepseek.com", "/chat/completions", DEEPSEEK_API_KEY,
                       AUTH_METHOD_BEARER_HEADER, "deepseek-chat", "\"content\":\"", "assistant", false);

    LLM gemini = LLM("Gemini", "generativelanguage.googleapis.com",
                     "/v1beta/models/gemini-2.5-flash-preview-04-17:generateContent", GEMINI_API_KEY,
                     AUTH_METHOD_URL_PARAM, "gemini-2.5-flash-preview-04-17", "\"text\": \"", "model", true);

    int ret;
    ret = chat_record_db_.InitDatabase();
    if (ret != SQLITE_OK) {
        printf("Database initialization failed\n");
        return;
    }

    int fd = open("/dev/key0", O_RDWR);
    if (fd < 0)
        printf("Can't open file");

    LLM& llm = qwen;
    ClientSender sender("10.33.47.116", 8000);
    ClientSReceiver receiver(8001);

    std::vector<ConversationMessage> conversation_data = {
        {llm.role().c_str(),
         "回复中的数字不要使用阿拉伯数字，使用中文数字，回复不要太长，在200字以内，回复中只回应以"
         "下内容：\n\n"}};

    unsigned char key_status;
    FILE* arecord_pipe = nullptr;
    int test_audio_id = 0;
    while (true) {
        ret = read(fd, &key_status, sizeof(key_status));
        printf("key_status = %d\n", key_status);

        if (key_status == 1) {
            arecord_pipe = popen("arecord -f cd ./record.wav", "r");
        }

        if (key_status == 2) {
            system("pkill arecord");
            pclose(arecord_pipe);

            std::string filename = "./test_audios/test" + std::to_string((test_audio_id % 4) + 1) + ".wav";
            sender.AudioSend(filename, "/upload/audio");
            test_audio_id++;

            std::string audio_text = receiver.HandleRequest("");
            std::cout << "Received text: " << audio_text << std::endl;

            conversation_data.push_back({"user", audio_text});
            std::string response = llm.SendRequest(conversation_data);
            conversation_data.push_back({llm.role().c_str(), response});
            chat_record_db_.InsertMessage(llm.name().c_str(), audio_text.c_str(), response.c_str(), nullptr);
            std::cout << "LLM Response: " << response << std::endl;

            sender.LlmReponseSend(response, "/send/text");
            std::string response_filename = "./llm_response_audios/" + std::to_string(time(nullptr)) + ".wav";
            receiver.HandleRequest(response_filename);

            std::string aplay_command = "aplay " + response_filename;
            FILE* aplay_pipe = popen(aplay_command.c_str(), "r");
            if (pclose(aplay_pipe) == 0)
                printf("音频播放完成\n");
        }
    }

    ::close(fd);
}