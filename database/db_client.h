#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include "protocol/db_protocol.h"
#include <curl/curl.h>
#include <time.h>

// Connection states
typedef enum {
    CONN_STATE_DISCONNECTED,
    CONN_STATE_CONNECTING,
    CONN_STATE_AUTHENTICATING,
    CONN_STATE_CONNECTED
} ConnectionState;

// Client structure
typedef struct {
    // Connection info
    char* host;
    int port;
    int sock;
    ConnectionState state;
    bool connected;
    
    // Authentication
    char* server_id;
    char* server_token;
    bool auth_complete;
    bool auth_success;
    
    // Message handling
    uint16_t sequence;
    time_t last_ping;
    time_t last_pong;
    time_t last_keepalive;
    time_t last_health_check;
    
    // Health tracking
    DatabaseHealth last_health;
    
    // Connection health tracking
    int missed_pongs;
    time_t last_successful_ping;
    int reconnect_attempts;
    bool is_reconnecting;
    
    // Server info
    ServerInfoPayload server_info;
    
    // Token verification (using CURL)
    CURL* curl;
    char curl_error[CURL_ERROR_SIZE];
} DatabaseClient;

// Client API
bool validateHealthValues(const DatabaseHealth* health);
bool db_client_init(DatabaseClient* client, const char* host, int port,
                   const char* server_id, const char* server_token);
void db_client_cleanup(DatabaseClient* client);
bool db_client_connect(DatabaseClient* client);
bool db_client_verify_token(DatabaseClient* client, const char* token,
                          TokenVerifyResult* result);
bool db_client_ping(DatabaseClient* client);

#endif
