#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chat_record.h"

ChatRecordDB::ChatRecordDB(std::string db_path) : db_path_(db_path) {}

int ChatRecordDB::InitDatabase() {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db_));
        sqlite3_close(db_);
        return rc;
    }

    const char* create_table_sql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "llm TEXT NOT NULL, "
        "message TEXT NOT NULL, "
        "response TEXT NOT NULL, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char* err_msg = NULL;
    rc = sqlite3_exec(db_, create_table_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Table creation error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db_);
        return rc;
    }

    return SQLITE_OK;
}

int ChatRecordDB::InsertMessage(const char* llm,
                                const char* message,
                                const char* response,
                                const char* timestamp) {
    const char* insert_sql;
    sqlite3_stmt* stmt;

    if (timestamp && *timestamp) {
        insert_sql = "INSERT INTO messages(llm, message, response, timestamp) VALUES (?, ?, ?, ?);";
    } else {
        insert_sql = "INSERT INTO messages(llm, message, response) VALUES (?, ?, ?);";
    }

    int rc = sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare insert failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, llm, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, message, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, response, -1, SQLITE_STATIC);

    if (timestamp && *timestamp) {
        sqlite3_bind_text(stmt, 4, timestamp, -1, SQLITE_STATIC);
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Insert failed: %s\n", sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

int ChatRecordDB::QueryAllMessages() {
    const char* sql = "SELECT id, llm, message, response, timestamp FROM messages ORDER BY timestamp ASC;";
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare select failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    printf("%-20s | %-5s | %-10s | %-30s | %s\n", "Timestamp", "ID", "LLM", "Message", "Response");
    printf(
        "----------------------------------------------------------------------------------------------------"
        "---------\n");

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* llm = (const char*)sqlite3_column_text(stmt, 1);
        const char* message = (const char*)sqlite3_column_text(stmt, 2);
        const char* response = (const char*)sqlite3_column_text(stmt, 3);
        const char* time = (const char*)sqlite3_column_text(stmt, 4);

        printf("%-20s | #%-4d | %-10s | %-30.30s | %.30s...\n", time, id, llm, message, response);
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

int ChatRecordDB::QueryAllDates() {
    const char* sql = "SELECT DISTINCT DATE(timestamp) as date FROM messages ORDER BY date ASC;";
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare date select failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    printf("=== Available Dates ===\n");
    int count = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* date = (const char*)sqlite3_column_text(stmt, 0);
        printf("%s\n", date);
        count++;
    }

    if (count == 0) {
        printf("No chat history found\n");
    } else {
        printf("Total: %d date(s)\n", count);
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

int ChatRecordDB::QueryMessagesByDate(const char* date) {
    if (!date || strlen(date) < 8) {
        fprintf(stderr, "Invalid date format. Use YYYY-MM-DD\n");
        return SQLITE_ERROR;
    }

    const char* sql =
        "SELECT id, llm, message, response, timestamp FROM messages "
        "WHERE DATE(timestamp) = ? ORDER BY timestamp ASC;";
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare date select failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, date, -1, SQLITE_STATIC);
    printf("=== Messages on %s ===\n", date);
    printf("%-20s | %-5s | %-10s | %-30s | %s\n", "Timestamp", "ID", "LLM", "Message", "Response");
    printf(
        "----------------------------------------------------------------------------------------------------"
        "---------\n");

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* llm = (const char*)sqlite3_column_text(stmt, 1);
        const char* message = (const char*)sqlite3_column_text(stmt, 2);
        const char* response = (const char*)sqlite3_column_text(stmt, 3);
        const char* time = (const char*)sqlite3_column_text(stmt, 4);

        printf("%-20s | #%-4d | %-10s | %-30.30s | %.30s...\n", time, id, llm, message, response);
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

int ChatRecordDB::DeleteMessagesByDate(const char* date) {
    if (!date || strlen(date) < 8) {
        fprintf(stderr, "Invalid date format. Use YYYY-MM-DD\n");
        return SQLITE_ERROR;
    }

    const char* sql = "DELETE FROM messages WHERE DATE(timestamp) = ?;";
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare delete by date failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, date, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Delete failed: %s\n", sqlite3_errmsg(db_));
    } else {
        int changes = sqlite3_changes(db_);
        printf("Deleted %d message(s) on %s\n", changes, date);
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

int ChatRecordDB::DeleteMessageByID(int id) {
    if (id <= 0) {
        fprintf(stderr, "Invalid ID: must be positive\n");
        return SQLITE_ERROR;
    }

    const char* sql = "DELETE FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare delete by ID failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        int changes = sqlite3_changes(db_);
        if (changes > 0) {
            printf("Deleted message with ID %d\n", id);
        } else {
            printf("No message found with ID %d\n", id);
        }
    } else {
        fprintf(stderr, "Delete by ID failed: %s\n", sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

int ChatRecordDB::UpdateMessageByID(int id, const char* message, const char* response) {
    if (id <= 0 || !message || !response) {
        fprintf(stderr, "Invalid parameters for update\n");
        return SQLITE_ERROR;
    }

    const char* sql = "UPDATE messages SET message = ?, response = ? WHERE id = ?;";
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare update failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, message, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, response, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        int changes = sqlite3_changes(db_);
        if (changes > 0) {
            printf("Updated message ID %d\n", id);
        } else {
            printf("No message found with ID %d\n", id);
        }
    } else {
        fprintf(stderr, "Update failed: %s\n", sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}
