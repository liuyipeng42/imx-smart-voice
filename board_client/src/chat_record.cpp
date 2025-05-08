#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#include "chat_record.h"

ChatRecordDB::ChatRecordDB(std::string db_path) : db_path_(db_path) {}

int ChatRecordDB::InitDatabase() {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db_));
        sqlite3_close(db_);
        return rc;
    }

    // Create conversations table
    const char* create_conversations_sql =
        "CREATE TABLE IF NOT EXISTS conversations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "llm TEXT NOT NULL, "
        "created_at INTEGER DEFAULT (strftime('%s', 'now')), "
        "updated_at INTEGER DEFAULT (strftime('%s', 'now')));";

    // Create messages table with reference to conversations
    const char* create_messages_sql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "conversation_id INTEGER NOT NULL, "
        "role TEXT NOT NULL, "  // 'user' or 'assistant'
        "message TEXT NOT NULL, "
        "created_at INTEGER DEFAULT (strftime('%s', 'now')), "
        "FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE);";

    char* err_msg = NULL;
    rc = sqlite3_exec(db_, create_conversations_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Conversations table creation error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db_);
        return rc;
    }

    rc = sqlite3_exec(db_, create_messages_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Messages table creation error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db_);
        return rc;
    }

    return SQLITE_OK;
}

int ChatRecordDB::CreateConversation(const std::string& llm, time_t created_at) {
    const char* sql;
    if (created_at > 0)
        sql = "INSERT INTO conversations (llm, created_at) VALUES (?, ?);";
    else
        sql = "INSERT INTO conversations (llm) VALUES (?);";

    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare create conversation failed: %s\n", sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, llm.c_str(), -1, SQLITE_STATIC);
    if (created_at > 0) {
        sqlite3_bind_int64(stmt, 2, created_at);
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Create conversation failed: %s\n", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return -1;
    }

    int conversation_id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    return conversation_id;
}

int ChatRecordDB::AddMessageToConversation(int conversation_id,
                                           const std::string& role,
                                           const std::string& message,
                                           time_t created_at) {
    const char* sql;
    if (created_at > 0)
        sql = "INSERT INTO messages (conversation_id, role, message, created_at) VALUES (?, ?, ?, ?);";
    else
        sql = "INSERT INTO messages (conversation_id, role, message) VALUES (?, ?, ?);";

    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare add message failed: %s\n", sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, conversation_id);
    sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_STATIC);
    if (created_at > 0) {
        sqlite3_bind_int64(stmt, 4, created_at);
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Add message failed: %s\n", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return -1;
    }

    // Update conversation's updated_at timestamp
    const char* update_sql = "UPDATE conversations SET updated_at = strftime('%s', 'now') WHERE id = ?;";
    sqlite3_stmt* update_stmt;

    rc = sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(update_stmt, 1, conversation_id);
        sqlite3_step(update_stmt);
        sqlite3_finalize(update_stmt);
    }

    int message_id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    return message_id;
}

ChatRecordDB::Conversation ChatRecordDB::GetConversation(int conversation_id) {
    Conversation conv;
    conv.id = -1;

    const char* conv_sql = "SELECT id, llm, created_at, updated_at FROM conversations WHERE id = ?;";
    sqlite3_stmt* conv_stmt;

    int rc = sqlite3_prepare_v2(db_, conv_sql, -1, &conv_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare get conversation failed: %s\n", sqlite3_errmsg(db_));
        return conv;
    }

    sqlite3_bind_int(conv_stmt, 1, conversation_id);

    if (sqlite3_step(conv_stmt) == SQLITE_ROW) {
        conv.id = sqlite3_column_int(conv_stmt, 0);
        conv.llm = reinterpret_cast<const char*>(sqlite3_column_text(conv_stmt, 1));
        conv.created_at = sqlite3_column_int64(conv_stmt, 2);
        conv.updated_at = sqlite3_column_int64(conv_stmt, 3);
    }

    sqlite3_finalize(conv_stmt);

    if (conv.id < 0)
        return conv;  // Conversation not found
    return conv;
}

std::vector<ChatRecordDB::Message> ChatRecordDB::QueryMessagesOfConversation(int conversation_id) {
    const char* sql =
        "SELECT id, conversation_id, role, message, created_at FROM messages WHERE conversation_id = ? "
        "ORDER BY created_at ASC;";
    sqlite3_stmt* stmt;
    std::vector<Message> messages;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare message select failed: %s\n", sqlite3_errmsg(db_));
        return messages;
    }

    sqlite3_bind_int(stmt, 1, conversation_id);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Message message;
        message.id = sqlite3_column_int(stmt, 0);
        message.conversation_id = sqlite3_column_int(stmt, 1);
        message.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        message.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        message.created_at = sqlite3_column_int64(stmt, 4);

        messages.push_back(message);
    }

    sqlite3_finalize(stmt);
    return messages;
}

std::vector<std::string> ChatRecordDB::QueryAllDates() {
    const char* sql =
        "SELECT DISTINCT strftime('%Y-%m-%d', created_at, 'unixepoch') FROM conversations ORDER BY "
        "created_at ASC;";
    sqlite3_stmt* stmt;
    std::vector<std::string> dates;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare date select failed: %s\n", sqlite3_errmsg(db_));
        return dates;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* date = sqlite3_column_text(stmt, 0);
        if (date) {
            dates.push_back(reinterpret_cast<const char*>(date));
        }
    }

    sqlite3_finalize(stmt);
    return dates;
}

std::vector<ChatRecordDB::Conversation> ChatRecordDB::QueryConversationsByDate(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        fprintf(stderr, "Invalid date format. Use YYYY-MM-DD\n");
        return {};
    }

    const char* sql =
        "SELECT id FROM conversations "
        "WHERE strftime('%Y-%m-%d', created_at, 'unixepoch') = ? "
        "ORDER BY created_at DESC;";

    sqlite3_stmt* stmt;
    std::vector<Conversation> conversations;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare conversation by date failed: %s\n", sqlite3_errmsg(db_));
        return conversations;
    }

    sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_STATIC);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int conversation_id = sqlite3_column_int(stmt, 0);

        conversations.push_back(GetConversation(conversation_id));
    }

    sqlite3_finalize(stmt);
    return conversations;
}

int ChatRecordDB::DeleteConversation(int conversation_id) {
    // Enable foreign key constraints to ensure CASCADE works
    if (sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to enable foreign keys: %s\n", sqlite3_errmsg(db_));
        return SQLITE_ERROR;
    }

    // Count messages to be deleted for reporting
    const char* count_sql = "SELECT COUNT(*) FROM messages WHERE conversation_id = ?;";
    sqlite3_stmt* count_stmt;
    int msg_count = 0;

    int rc = sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(count_stmt, 1, conversation_id);
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            msg_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    // Delete the conversation (messages will be deleted automatically via ON DELETE CASCADE)
    const char* sql = "DELETE FROM conversations WHERE id = ?;";
    sqlite3_stmt* stmt;

    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare delete conversation failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, conversation_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Delete conversation failed: %s\n", sqlite3_errmsg(db_));
    } else {
        int changes = sqlite3_changes(db_);
        if (changes > 0) {
            printf("Deleted conversation ID %d with %d associated messages\n", conversation_id, msg_count);
        } else {
            printf("No conversation found with ID %d\n", conversation_id);
        }
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

int ChatRecordDB::DeleteMessageByID(int message_id) {
    const char* sql = "DELETE FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare delete message failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, message_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Delete message failed: %s\n", sqlite3_errmsg(db_));
    } else {
        int changes = sqlite3_changes(db_);
        printf("Deleted message ID %d with %d changes\n", message_id, changes);
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}
