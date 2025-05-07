
#include <QDate>
#include <QDebug>
#include <QFont>
#include <QHBoxLayout>
#include <QScrollBar>

#include "chat_record.h"
#include "chat_window.h"
#include "conversation_handler.h"

ChatWindow::ChatWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("ChatGPT Client");
    resize(800, 480);
    QSplitter* splitter = new QSplitter(this);

    // 左边布局
    QWidget* leftWidget = new QWidget;
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    // 新建聊天按钮
    newChatButton_ = new QPushButton("+ New Chat");
    newChatButton_->setStyleSheet(
        "QPushButton {"
        "background-color: #4CAF50;"
        "color: white;"
        "border: none;"
        "border-radius: 10px;"
        "min-width: 80px;"
        "min-height: 40px;"
        "font-size: 14px;"
        "}"
        "QPushButton:hover { background-color: #45a049; }");
    newChatButton_->setFixedWidth(100);

    connect(newChatButton_, &QPushButton::clicked, this, &ChatWindow::newChat);
    leftLayout->addWidget(newChatButton_);

    // 聊天历史列表
    historyList_ = new QListWidget;
    historyList_->setStyleSheet(
        "QListWidget {"
        "background-color: #f5f5f5;"
        "border: none;"
        "}"
        "QListWidget::item:disabled {"  // 日期分组样式
        "background: transparent;"
        "border: none;"
        "margin: 4px 0;"
        "padding: 2px 8px;"
        "color: #757575;"
        "}"
        "QListWidget::item:!disabled {"  // 聊天项样式
        "background: white;"
        "border: 1px solid #e0e0e0;"
        "border-radius: 8px;"
        "margin: 4px 8px;"
        "padding: 8px;"
        "}"
        "QListWidget::item:selected {"
        "background-color: #e3f2fd;"
        "border: 1px solid #90caf9;"
        "}");
    connect(historyList_, &QListWidget::itemClicked, this, &ChatWindow::loadChatHistory);

    // 示例数据
    addDateGroup(QDate::currentDate().addDays(-1));
    insertNewChatItem(QDate::currentDate().addDays(-1), 1);
    addDateGroup(QDate::currentDate());
    insertNewChatItem(QDate::currentDate(), 1);
    insertNewChatItem(QDate::currentDate(), 2);

    // 右边布局
    leftLayout->addWidget(historyList_);
    QWidget* rightWidget = new QWidget;
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(16, 0, 0, 0);
    rightLayout->setSpacing(12);

    // 聊天显示区域
    chatDisplay_ = new QListWidget;
    chatDisplay_->setStyleSheet(
        "QListWidget {"
        "background: white;"
        "border-radius: 8px;"
        "border: 1px solid #e0e0e0;"
        "padding: 8px;"
        "}"
        "QListWidget::item { border: none; margin: 4px 0; }");  // 移除默认的item间距
    chatDisplay_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatDisplay_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chatDisplay_->setWordWrap(true);
    rightLayout->addWidget(chatDisplay_, 1);

    // 输入区域
    QHBoxLayout* inputLayout = new QHBoxLayout;
    inputEdit_ = new QLineEdit;
    inputEdit_->setPlaceholderText("Type a message...");
    inputEdit_->setStyleSheet(
        "QLineEdit {"
        "border: 1px solid #e0e0e0;"
        "border-radius: 20px;"
        "padding: 8px 16px;"
        "font-size: 14px;"
        "}");

    sendButton_ = new QPushButton("Send");
    sendButton_->setStyleSheet(
        "QPushButton {"
        "background-color: #4CAF50;"
        "color: white;"
        "border: none;"
        "border-radius: 10px;"  // 减小圆角半径
        "min-width: 80px;"
        "min-height: 40px;"
        "font-size: 14px;"
        "}"
        "QPushButton:hover { background-color: #45a049; }");

    connect(sendButton_, &QPushButton::clicked, this, &ChatWindow::sendMessage);
    inputLayout->addWidget(inputEdit_, 1);
    inputLayout->addWidget(sendButton_);
    rightLayout->addLayout(inputLayout);

    // 主布局
    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setSizes({220, 580});
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(splitter);

    ConversationHandler* conversationHandler = new ConversationHandler("./test.db");
    conversationHandler->start();
}

void ChatWindow::addDateGroup(const QDate& date) {
    QListWidgetItem* dateItem = new QListWidgetItem(date.toString("yyyy-MM-dd"));
    dateItem->setFlags(Qt::NoItemFlags);
    dateItem->setForeground(QColor("#757575"));
    dateItem->setFont(QFont("Arial", 10, QFont::Bold));
    historyList_->insertItem(0, dateItem);  // 插入到列表顶部
}

void ChatWindow::insertNewChatItem(const QDate& date, int count) {
    // 查找对应日期分组位置
    int datePos = -1;
    for (int i = 0; i < historyList_->count(); ++i) {
        QListWidgetItem* item = historyList_->item(i);
        if (item->text() == date.toString("yyyy-MM-dd")) {
            datePos = i;
            break;
        }
    }

    if (datePos == -1) {
        addDateGroup(date);
        datePos = 0;
    }
    // 插入到分组项的下一行
    QListWidgetItem* newItem = new QListWidgetItem(QString("Chat %1").arg(count));
    newItem->setData(Qt::UserRole, date);
    newItem->setFont(QFont("Arial", 11));
    historyList_->insertItem(datePos + 1, newItem);
}

void ChatWindow::addChatBubble(const QString& text, bool isUser) {
    QListWidgetItem* item = new QListWidgetItem();
    item->setFlags(Qt::NoItemFlags);                      // 禁止交互
    item->setSizeHint(QSize(chatDisplay_->width(), 50));  // 初始高度，后面会调整
    QTextBrowser* bubble = new QTextBrowser;
    bubble->setText(text);
    bubble->setReadOnly(true);
    bubble->setFrameShape(QFrame::NoFrame);
    bubble->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bubble->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    // 动态样式设置
    QString bgColor = isUser ? "white" : "#e0e0e0";
    QString textColor = isUser ? "black" : "black";
    bubble->setStyleSheet(QString("QTextBrowser {"
                                  "background: %1;"
                                  "color: %2;"
                                  "border-radius: 12px 12px 12px 12px;"
                                  "padding: 6px;"
                                  "margin-left: 10px;"
                                  "margin-right: 40px;"
                                  "}")
                              .arg(bgColor, textColor));
    // 自适应高度
    QTextDocument* doc = bubble->document();
    doc->setTextWidth(chatDisplay_->width() * 0.65);  // 控制文本宽度
    int height = doc->size().height() + 20;           // 加上padding
    item->setSizeHint(QSize(chatDisplay_->width(), height));
    // 添加item
    chatDisplay_->addItem(item);
    chatDisplay_->setItemWidget(item, bubble);
}

void ChatWindow::sendMessage() {
    QString message = inputEdit_->text().trimmed();
    if (message.isEmpty())
        return;
    // 用户消息（添加时间戳）
    addChatBubble(QString("%1\n%2").arg(message).arg(QDateTime::currentDateTime().toString("hh:mm")), true);

    addChatBubble(
        QString("AI Reply: \n%1\n%2").arg(message).arg(QDateTime::currentDateTime().toString("hh:mm")),
        false);

    inputEdit_->clear();
    chatDisplay_->scrollToBottom();
}

void ChatWindow::loadChatHistory(QListWidgetItem* item) {
    if (item->flags() == Qt::NoItemFlags)
        return;
    chatDisplay_->clear();
}

void ChatWindow::newChat() {
    QDate today = QDate::currentDate();
    int datePos = -1;
    int chatCount = 0;
    // 查找当天分组位置
    for (int i = 0; i < historyList_->count(); ++i) {
        QListWidgetItem* item = historyList_->item(i);
        if (item->text() == today.toString("yyyy-MM-dd")) {
            datePos = i;
            // 统计当天聊天数量
            int pos = i + 1;
            while (pos < historyList_->count() && historyList_->item(pos)->flags() != Qt::NoItemFlags) {
                chatCount++;
                pos++;
            }
            break;
        }
    }
    // 插入新聊天到分组顶部
    if (datePos == -1) {
        addDateGroup(today);
        datePos = 0;
    }

    QListWidgetItem* newItem = new QListWidgetItem(QString("Chat %1").arg(chatCount + 1));
    newItem->setData(Qt::UserRole, today);
    newItem->setFont(QFont("Arial", 11));
    historyList_->insertItem(datePos + 1, newItem);
    // 滚动到新项目
    historyList_->setCurrentItem(newItem);
    historyList_->scrollToItem(newItem);
}
