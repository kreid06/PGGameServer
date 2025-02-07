#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include "net_protocol.h"
#include <curl/curl.h>

#define MAX_TOKEN_SIZE 2048  // Increased from previous size to handle long tokens

typedef struct {
    char* host;
    int port;
    CURL* curl;
    char error_buffer[CURL_ERROR_SIZE];
    char* server_id;     // Add server ID
    char* server_token;  // Add server token
} DatabaseClient;

// Database client functions
bool initDatabaseClient(DatabaseClient* client, const char* host, int port, 
                       const char* server_id, const char* server_token);
void cleanupDatabaseClient(DatabaseClient* client);
bool checkDatabaseHealth(DatabaseClient* client, DatabaseHealth* health);
bool verifyUserToken(DatabaseClient* client, const char* token, TokenVerifyResult* result);

#endif // DB_CLIENT_H
