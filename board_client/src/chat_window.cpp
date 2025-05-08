
#include <QDate>
#include <QDebug>
#include <QFont>
#include <QHBoxLayout>
#include <QScrollBar>

#include <unistd.h>
#include <iostream>
#include "chat_record.h"
#include "chat_window.h"
#include "conversation_handler.h"

ChatWindow::ChatWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("ChatGPT Client");
    resize(800, 480);

    chat_record_db_ = new ChatRecordDB("./test.db");
    int ret = chat_record_db_->InitDatabase();
    if (ret != SQLITE_OK) {
        printf("Database initialization failed\n");
        return;
    }

    QSplitter* splitter = new QSplitter(this);

    // 左边布局
    QWidget* left_widget = new QWidget;
    QVBoxLayout* left_layout = new QVBoxLayout(left_widget);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(8);

    // 新建聊天按钮和捕获按钮并排布局
    QHBoxLayout* button_layout = new QHBoxLayout;

    new_chat_button_ = new QPushButton("+ New Chat");
    new_chat_button_->setStyleSheet(
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
    new_chat_button_->setFixedWidth(100);

    capture_button_ = new QPushButton("Capture");
    capture_button_->setStyleSheet(
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
    capture_button_->setFixedWidth(100);

    button_layout->addWidget(new_chat_button_);
    button_layout->addWidget(capture_button_);

    connect(new_chat_button_, &QPushButton::clicked, this, &ChatWindow::NewChat);
    connect(capture_button_, &QPushButton::clicked, this, &ChatWindow::CaptureImage);

    left_layout->addLayout(button_layout);

    // 聊天历史列表
    history_list_ = new QListWidget;
    history_list_->setStyleSheet(
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
        "color: #0d47a1;"
        "}");

    connect(history_list_, &QListWidget::itemClicked, [this](QListWidgetItem* item) {
        if (item->isSelected()) {
            LoadChatHistory(item);
        }
    });

    // 示例数据
    // time_t now = time(nullptr);
    // time_t yesterday = now - 86400;
    // int conv1_id = chat_record_db_->CreateConversation("gpt-4", yesterday);
    // int conv2_id = chat_record_db_->CreateConversation("claude-2", yesterday + 3600);

    // int conv3_id = chat_record_db_->CreateConversation("gpt-4", now);
    // int conv4_id = chat_record_db_->CreateConversation("llama-2", now + 3600);

    // chat_record_db_->AddMessageToConversation(conv1_id, "user", "Hello from day 1, conv 1", yesterday);
    // chat_record_db_->AddMessageToConversation(conv1_id, "assistant", "Response from day 1, conv 1",
    //                                          yesterday + 60);

    // chat_record_db_->AddMessageToConversation(conv2_id, "user", "Hello from day 1, conv 2", yesterday +
    // 3600); chat_record_db_->AddMessageToConversation(conv2_id, "assistant", "Response from day 1, conv 2",
    //                                          yesterday + 3660);

    // chat_record_db_->AddMessageToConversation(conv3_id, "user", "Hello from day 2, conv 1", now);
    // chat_record_db_->AddMessageToConversation(conv3_id, "assistant", "Response from day 2, conv 1", now +
    // 60);

    // chat_record_db_->AddMessageToConversation(conv4_id, "user", "Hello from day 2, conv 2", now + 3600);
    // chat_record_db_->AddMessageToConversation(conv4_id, "assistant", "Response from day 2, conv 2",
    //                                          now + 3660);

    std::vector<std::string> date_strs = chat_record_db_->QueryAllDates();
    for (const auto& date_str : date_strs) {
        QDate date = QDate::fromString(date_str.c_str(), "yyyy-MM-dd");
        AddDateGroup(date);
        int conversation_count = 1;
        for (const auto& conversation : chat_record_db_->QueryConversationsByDate(date_str.c_str())) {
            std::string title = "Chat " + std::to_string(conversation_count++);
            InsertNewConversationItem(QDateTime::fromSecsSinceEpoch(conversation.created_at).date(), title,
                                      conversation.id);
        }
    }
    if (date_strs.empty()) {
        QDate today = QDate::currentDate();
        AddDateGroup(today);
        int conversation_id = chat_record_db_->CreateConversation("Qwen", time(nullptr));
        std::string title = "Chat 1";
        InsertNewConversationItem(today, title, conversation_id);
    }

    // 右边布局
    left_layout->addWidget(history_list_);
    QWidget* rightWidget = new QWidget;
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(16, 0, 0, 0);
    rightLayout->setSpacing(12);

    // 聊天显示区域
    chat_display_ = new QListWidget;
    chat_display_->setStyleSheet(
        "QListWidget {"
        "background: white;"
        "border-radius: 8px;"
        "border: 1px solid #e0e0e0;"
        "padding: 8px;"
        "}"
        "QListWidget::item { border: none; margin: 4px 0; }");  // 移除默认的item间距
    chat_display_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chat_display_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chat_display_->setWordWrap(true);
    rightLayout->addWidget(chat_display_, 1);

    // 状态栏区域
    status_bar_ = new QLabel("Ready");
    status_bar_->setStyleSheet(
        "QLabel {"
        "border: 1px solid #e0e0e0;"
        "border-radius: 10px;"
        "padding: 8px 16px;"
        "font-size: 14px;"
        "background-color: #f5f5f5;"
        "color: #555555;"
        "}");
    status_bar_->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(status_bar_);

    // 主布局
    splitter->addWidget(left_widget);
    splitter->addWidget(rightWidget);
    splitter->setSizes({220, 580});
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(splitter);

    ConversationHandler* conversationHandler = new ConversationHandler("./test.db");
    connect(conversationHandler, SIGNAL(SendConvoStatus(char*, char*)), this,
            SLOT(ReceiveConvoStatus(char*, char*)));
    connect(this, SIGNAL(SendConvoID(int)), conversationHandler, SLOT(ReceiveConvoID(int)));
    connect(this, SIGNAL(HasImage()), conversationHandler, SLOT(HasImage()));
    conversationHandler->start();

    if (history_list_->count() > 0) {
        for (int i = 0; i < history_list_->count(); i++) {
            QListWidgetItem* item = history_list_->item(i);
            if (item->flags() != Qt::NoItemFlags) {
                history_list_->setCurrentItem(item);
                LoadChatHistory(item);
                break;
            }
        }
    }
}

void ChatWindow::AddDateGroup(const QDate& date) {
    QListWidgetItem* date_item = new QListWidgetItem(date.toString("yyyy-MM-dd"));
    date_item->setFlags(Qt::NoItemFlags);
    date_item->setForeground(QColor("#757575"));
    date_item->setFont(QFont("Arial", 10, QFont::Bold));
    history_list_->insertItem(0, date_item);  // 插入到列表顶部
}

void ChatWindow::InsertNewConversationItem(const QDate& date, std::string& title, int conversation_id) {
    // 查找对应日期分组位置
    int date_pos = -1;
    for (int i = 0; i < history_list_->count(); ++i) {
        QListWidgetItem* item = history_list_->item(i);
        if (item->text() == date.toString("yyyy-MM-dd")) {
            date_pos = i;
            break;
        }
    }
    if (date_pos == -1) {
        AddDateGroup(date);
        date_pos = 0;
    }

    // 插入到分组项的下一行
    QListWidgetItem* new_item = new QListWidgetItem(QString((title.c_str())));
    new_item->setData(Qt::UserRole, QString::number(conversation_id));
    new_item->setFont(QFont("Arial", 11));
    history_list_->insertItem(date_pos + 1, new_item);
}

void ChatWindow::AddChatBubble(const QString& text, bool isUser, const QString& imagePath = "") {
    QListWidgetItem* item = new QListWidgetItem();
    item->setFlags(Qt::NoItemFlags);                       // 禁止交互
    item->setSizeHint(QSize(chat_display_->width(), 50));  // 初始高度，后面会调整

    if (imagePath.isEmpty()) {
        QTextBrowser* bubble = new QTextBrowser;
        bubble->setText(text);
        bubble->setReadOnly(true);
        bubble->setFrameShape(QFrame::NoFrame);
        bubble->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        bubble->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        // 动态样式设置
        QString bgColor = isUser ? "#e0e0e0" : "#999999";
        QString text_color = "black";
        bubble->setStyleSheet(QString("QTextBrowser {"
                                      "background: %1;"
                                      "color: %2;"
                                      "border-radius: 12px 12px 12px 12px;"
                                      "padding: 6px;"
                                      "margin-left: 10px;"
                                      "margin-right: 40px;"
                                      "}")
                                  .arg(bgColor, text_color));
        // 自适应高度
        QTextDocument* doc = bubble->document();
        doc->setTextWidth(chat_display_->width() * 0.65);  // 控制文本宽度
        int height = doc->size().height() + 20;            // 加上padding
        item->setSizeHint(QSize(chat_display_->width(), height));
        // 添加item
        chat_display_->addItem(item);
        chat_display_->setItemWidget(item, bubble);
    } else {
        QLabel* imageLabel = new QLabel;
        QPixmap pixmap;
        pixmap.load(imagePath);
        // 调整图片大小为较小尺寸
        int maxWidth = chat_display_->width() * 0.4;  // 图片宽度为显示区域的40%
        int maxHeight = maxWidth * pixmap.height() / pixmap.width();
        imageLabel->setPixmap(pixmap.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio));
        imageLabel->setAlignment(Qt::AlignLeft);  // 图片靠左显示
        int height = maxHeight + 20;              // 加上padding
        item->setSizeHint(QSize(chat_display_->width(), height));
        chat_display_->addItem(item);
        chat_display_->setItemWidget(item, imageLabel);
    }
}

void ChatWindow::LoadChatHistory(QListWidgetItem* item) {
    if (item->flags() == Qt::NoItemFlags)
        return;
    chat_display_->clear();

    int conversation_id = item->data(Qt::UserRole).toInt();

    current_conversation_id_ = conversation_id;
    emit SendConvoID(conversation_id);

    auto messages = chat_record_db_->QueryMessagesOfConversation(conversation_id);

    for (const auto& message : messages) {
        QString role = QString::fromStdString(message.role);
        QString text = QString::fromStdString(message.message);
        if (role == "user") {
            AddChatBubble(QString("%1\n%2")
                              .arg(message.message.c_str())
                              .arg(QDateTime::fromSecsSinceEpoch(message.created_at).toString("hh:mm")),
                          true);
        } else {
            AddChatBubble(QString("%1\n%2")
                              .arg(message.message.c_str())
                              .arg(QDateTime::fromSecsSinceEpoch(message.created_at).toString("hh:mm")),
                          false);
        }
    }
}

void ChatWindow::NewChat() {
    QDate today = QDate::currentDate();
    int date_pos = -1;
    int chatCount = 0;
    // 查找当天分组位置
    for (int i = 0; i < history_list_->count(); ++i) {
        QListWidgetItem* item = history_list_->item(i);
        if (item->text() == today.toString("yyyy-MM-dd")) {
            date_pos = i;
            // 统计当天聊天数量
            int pos = i + 1;
            while (pos < history_list_->count() && history_list_->item(pos)->flags() != Qt::NoItemFlags) {
                chatCount++;
                pos++;
            }
            break;
        }
    }
    // 插入新聊天到分组顶部
    if (date_pos == -1) {
        chat_record_db_->CreateConversation("Qwen", time(nullptr));
        AddDateGroup(today);
        date_pos = 0;
    }

    QListWidgetItem* new_item = new QListWidgetItem(QString("Chat %1").arg(chatCount + 1));
    new_item->setData(Qt::UserRole, today);
    new_item->setFont(QFont("Arial", 11));
    history_list_->insertItem(date_pos + 1, new_item);
    // 滚动到新项目
    history_list_->setCurrentItem(new_item);
    history_list_->scrollToItem(new_item);

    new_chat_button_->setEnabled(true);
}

void ChatWindow::CaptureImage() {
    V4L2Camera camera;
    camera.Capture("./image.jpg", 100);
    camera.CleanUp();

    capture_button_->setEnabled(true);

    AddChatBubble(QString("%1\n%2")
                      .arg("Image captured")
                      .arg(QDateTime::fromSecsSinceEpoch(time(NULL)).toString("hh:mm")),
                  true, "./image.jpg");
                  
    emit HasImage();
    chat_display_->scrollToBottom();
}

void ChatWindow::ReceiveConvoStatus(char* status, char* value) {
    std::cout << "ReceiveConvoStatus: " << status << ", " << value << std::endl;
    status_bar_->setText(QString::fromStdString(status));

    if (strcmp(status, "Audio text received") == 0) {
        time_t now = time(nullptr);
        chat_record_db_->AddMessageToConversation(current_conversation_id_, "user", value, now);
        AddChatBubble(QString("%1\n%2").arg(value).arg(QDateTime::fromSecsSinceEpoch(now).toString("hh:mm")),
                      true);
        chat_display_->scrollToBottom();
    } else if (strcmp(status, "LLM response received") == 0) {
        time_t now = time(nullptr);
        chat_record_db_->AddMessageToConversation(current_conversation_id_, "assistant", value, now);
        AddChatBubble(QString("%1\n%2").arg(value).arg(QDateTime::fromSecsSinceEpoch(now).toString("hh:mm")),
                      false);
        chat_display_->scrollToBottom();
    } else if (strcmp(status, "Response audio received") == 0) {
        sleep(1);
        status_bar_->setText("");
    }
}