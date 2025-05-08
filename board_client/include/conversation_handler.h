#include <QObject>
#include <QThread>
#include <string>

#include "chat_record.h"
#include "client_receiver.h"
#include "client_sender.h"
#include "llm.h"

#ifndef CONVERSATION_HANDLER_H
#define CONVERSATION_HANDLER_H

class ConversationHandler : public QThread {
    Q_OBJECT

    int key_fd_;
    LLM* llm_;
    ClientSender* sender_;
    ClientReceiver* receiver_;
    ChatRecordDB* chat_record_db_;
    int current_conversation_id_ = -1;

    bool has_image_ = false;

   public:
    ConversationHandler(std::string db_path);
    ~ConversationHandler();

   protected:
    void run() override;

   private slots:
    void ReceiveConvoID(int conversation_id);
    void HasImage();

   signals:
    void SendConvoStatus(char* status, char* value);
};

#endif