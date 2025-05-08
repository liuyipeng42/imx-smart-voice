#include <string>

#include "sqlite3.h"

#ifndef CHAT_RECORD_HPP
#define CHAT_RECORD_HPP

class ChatRecordDB {
    sqlite3* db_;
    std::string db_path_;

   public:
    struct Message {
        uint id;
        uint conversation_id;
        std::string role;
        std::string message;
        time_t created_at;
    };

    struct Conversation {
        int id;
        std::string llm;
        time_t created_at;
        time_t updated_at;
    };

    ChatRecordDB() = default;
    ChatRecordDB(std::string db_path);

    ~ChatRecordDB() {
        if (db_) {
            sqlite3_close(db_);
        }
    }

    int InitDatabase();

    int CreateConversation(const std::string& llm, time_t created_at);

    int AddMessageToConversation(int conversation_id,
                                 const std::string& role,
                                 const std::string& message,
                                 time_t created_at);

    Conversation GetConversation(int conversation_id);

    std::vector<Message> QueryMessagesOfConversation(int conversation_id);

    std::vector<std::string> QueryAllDates();

    std::vector<Conversation> QueryConversationsByDate(const std::string& date);

    int DeleteConversation(int conversation_id);

    int DeleteMessageByID(int id);
};

#endif