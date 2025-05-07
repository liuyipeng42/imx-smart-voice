#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

class ChatWindow : public QWidget {
    Q_OBJECT

   public:
    ChatWindow(QWidget* parent = nullptr);

   private slots:
    void sendMessage();
    void loadChatHistory(QListWidgetItem* item);
    void newChat();

   private:
    void addDateGroup(const QDate& date);
    void insertNewChatItem(const QDate& date, int position);
    void addChatBubble(const QString& text, bool isUser);

    QListWidget* historyList_;
    QListWidget* chatDisplay_;
    QLineEdit* inputEdit_;
    QPushButton* sendButton_;
    QPushButton* newChatButton_;
};

#endif  // CHATWINDOW_H
