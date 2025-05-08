#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

#include "chat_record.h"
#include "v4l2_camera.h"

class ChatWindow : public QWidget {
    Q_OBJECT

    QListWidget* history_list_;
    QListWidget* chat_display_;

    QLabel* status_bar_;
    QPushButton* new_chat_button_;
    QPushButton* capture_button_;
    ChatRecordDB* chat_record_db_;
    int current_conversation_id_ = -1;

    void AddDateGroup(const QDate& date);
    void InsertNewConversationItem(const QDate& date, std::string& title, int conversation_id);
    void AddChatBubble(const QString& text, bool isUser, const QString& imagePath);

   public:
    ChatWindow(QWidget* parent = nullptr);

   private slots:
    void NewChat();
    void CaptureImage();
    void LoadChatHistory(QListWidgetItem* item);
    void ReceiveConvoStatus(char* status, char* value);

   signals:
    void SendConvoID(int conversation_id);
    void HasImage();
};

#endif  // CHATWINDOW_H
