#include <string>

#include "sqlite3.h"

#ifndef CHAT_RECORD_HPP
#define CHAT_RECORD_HPP

class ChatRecordDB {
    sqlite3* db_;
    std::string db_path_;

   public:
    ChatRecordDB() = default;
    ChatRecordDB(std::string db_path);

    ~ChatRecordDB() {
        if (db_) {
            sqlite3_close(db_);
        }
    }

    int InitDatabase();

    int InsertMessage(const char* llm, const char* message, const char* response, const char* timestamp);

    int QueryAllMessages();

    int QueryAllDates();

    int QueryMessagesByDate(const char* date);

    int DeleteMessagesByDate(const char* date);

    int DeleteMessageByID(int id);

    int UpdateMessageByID(int id, const char* message, const char* response);
};

#endif