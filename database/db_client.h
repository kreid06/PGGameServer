#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include "./network/db_protocol.h"
#include "../network/websockets/websocket.h"  // Add this for WebSocket implementation
#include <curl/curl.h>
#include <time.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <errno.h>

#define LWS_LATEST_FEATURES

#define MAX_TOKEN_SIZE 2048
#define DB_MAX_RETRIES 3
#define DB_RETRY_DELAY_MS 1000
#define DB_KEEPALIVE_IDLE 60
#define DB_KEEPALIVE_INTERVAL 30
#define DB_CONNECT_TIMEOUT 10
#define DB_REQUEST_TIMEOUT 30

// Add packet type definitions
#define PKT_TYPE_AUTH          0x01
#define PKT_TYPE_AUTH_RESP     0x02
#define PKT_TYPE_HEALTH_CHECK  0x04
#define PKT_TYPE_HEALTH_RESP   0x05
#define PKT_TYPE_AUTH_ACK      0x03
#define PKT_TYPE_SERVER_INFO   0x06

// Add server info structure
typedef struct {
    uint16_t version;
    uint16_t maxPlayers;
    uint32_t features;
} ServerInfo;

// Add connection state enum
typedef enum {
    CONN_STATE_DISCONNECTED,
    CONN_STATE_CONNECTING,
    CONN_STATE_AUTHENTICATING,
    CONN_STATE_CONNECTED
} ConnectionState;

typedef enum {
    CONN_TYPE_TCP,
    CONN_TYPE_WEBSOCKET
} ConnectionType;

typedef struct DatabaseClient {
    char* host;
    int port;
    CURL* curl;  // Keep for token verification
    char error_buffer[CURL_ERROR_SIZE];
    char* server_id;
    char* server_token;
    time_t last_health_check;
    DatabaseHealth last_health;
    int sock;              // Just need a socket fd
    bool connected;        // Simple connection state
    time_t last_ping;     // Keep track of ping timing
    time_t last_pong;     // Keep track of server response
    ConnectionType conn_type;      // Add connection type
    WebSocket ws;                  // Add WebSocket struct
    ServerInfo serverInfo;         // Add server info
    bool auth_success;             // Add auth success flag
    uint16_t sequence;             // Add sequence number for message ordering
    ConnectionState state;         // Add connection state tracking
    time_t last_keepalive;         // Track last keepalive sent
    bool auth_complete;            // Track if auth was successful
} DatabaseClient;

typedef struct CurlResponse {
    uint8_t* buffer;
    size_t size;
} CurlResponse;

// Remove old function declarations
bool initDatabaseClient(DatabaseClient* client, const char* host, int port, 
                       const char* server_id, const char* server_token,
                       ConnectionType type);
void cleanupDatabaseClient(DatabaseClient* client);
bool checkDatabaseHealth(DatabaseClient* client, DatabaseHealth* health);
bool verifyUserToken(DatabaseClient* client, const char* token, TokenVerifyResult* result);

// Add function declaration
bool validateHealthValues(const DatabaseHealth* health);

// Note: Remove or comment out old lws related functions
// bool resetDatabaseConnection(DatabaseClient* client);
// bool isDatabaseConnectionValid(DatabaseClient* client);
// bool setupWebSocketRetry(DatabaseClient* client);

#endif // DB_CLIENT_H
