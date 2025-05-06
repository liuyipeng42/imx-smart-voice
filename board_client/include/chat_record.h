#include <sqlite3.h>

int init_database(sqlite3** db, const char* db_path);

int insert_message(sqlite3* db,
                   const char* llm,
                   const char* message,
                   const char* response,
                   const char* timestamp);

int query_all_messages(sqlite3* db);

int query_all_dates(sqlite3* db);

int query_messages_by_date(sqlite3* db, const char* date);

int delete_messages_by_date(sqlite3* db, const char* date);

int delete_message_by_id(sqlite3* db, int id);

int update_message_by_id(sqlite3* db, int id, const char* message, const char* response);
