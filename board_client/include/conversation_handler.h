#include <QObject>
#include <QThread>
#include <string>

#include "chat_record.h"

#ifndef CONVERSATION_HANDLER_H
#define CONVERSATION_HANDLER_H

class ConversationHandler : public QThread {
    Q_OBJECT

    ChatRecordDB chat_record_db_;

   public:
    ConversationHandler(std::string db_path);

   protected:
    void run() override;

   signals:
    void send_response(char* response);
    void send_audio(char* file_path);
};

#endif