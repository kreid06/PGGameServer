#ifndef DB_CLIENT_H
#define DB_CLIENT_H

//-----------------------------------------------------------------------------
// Dependencies
//-----------------------------------------------------------------------------
#include "protocol/db_protocol.h"
#include <curl/curl.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>

//-----------------------------------------------------------------------------
// Constants & Configuration
//-----------------------------------------------------------------------------

// Retry configuration
#define DB_MAX_RETRIES 5           // Increase max retries
#define DB_RETRY_DELAY_MS 5000     // Increase base delay to 5 seconds
#define DB_RETRY_BACKOFF_MULTIPLIER 2

// Size limits
#define MAX_SERVER_ID_LENGTH 32
#define MAX_SERVER_TOKEN_LENGTH 512  // Increase from 256 to 512 to handle longer tokens

// Error codes
#define DB_ERROR_INVALID_ID_LENGTH -1
#define DB_ERROR_INVALID_TOKEN_LENGTH -2

// Ping timeout constants
#define PING_TIMEOUT_MS 3000        // Increase to 5 seconds
#define PONG_CHECK_INTERVAL_MS 500  // Check every 500ms
#define PING_RETRY_INTERVAL_MS 6000 // Wait 5 seconds between retries
#define MAX_MISSED_PINGS 3         // Allow 3 missed pings before considering connection dead

// Update reconnection constants
#define MAX_RECONNECT_ATTEMPTS 10           // Reduce from 100 to 10
#define RECONNECT_INITIAL_DELAY_MS 1000     // Start with 1 second
#define RECONNECT_MAX_DELAY_MS 30000        // Cap at 30 seconds
#define RECONNECT_BACKOFF_MULTIPLIER 2.0    // Double delay each attempt
#define CONNECTION_STABILITY_THRESHOLD 60    // Seconds before considering connection stable

// Add missing constants and time functions
#define MAX_PING_FAILURES 3         // Maximum failed pings before reconnect
#define GetTime() ((double)time(NULL))  // Simple time function for now

//-----------------------------------------------------------------------------
// Network Types
//-----------------------------------------------------------------------------

// Add connection quality tracking
typedef struct {
    time_t last_successful_connect;     // Last time we had a good connection
    int failed_attempts_count;          // How many times we've failed recently
    double connection_uptime;           // How long we stay connected on average
    bool connection_stable;             // Is the connection considered stable
} ConnectionQualityMetrics;

// Network connection information
typedef struct {
    char* host;               // Server hostname
    int port;                // Server port
    int sock;               // Socket file descriptor
    struct sockaddr_in addr;  // Server address info
    bool connected;          // Connection status
} NetworkConnection;

// Connection states
typedef enum {
    CONN_STATE_DISCONNECTED,    // Initial state or after disconnect
    CONN_STATE_CONNECTING,      // Connection in progress
    CONN_STATE_AUTHENTICATING,  // Authenticating with server
    CONN_STATE_CONNECTED       // Successfully connected and authenticated
} ConnectionState;

//-----------------------------------------------------------------------------
// Health Monitoring Types
//-----------------------------------------------------------------------------

// Ping state tracking
typedef struct {
    time_t timestamp;          // Last ping timestamp
    time_t last_pong;         // Last pong received
    time_t last_successful;    // Last successful ping time
    bool expecting_pong;       // Whether we're waiting for a pong
    int missed_pongs;         // Count of missed pongs
    uint16_t last_sequence;   // Last ping sequence number
} PingState;

//-----------------------------------------------------------------------------
// Main Client Type
//-----------------------------------------------------------------------------

typedef struct {
    // Core networking
    NetworkConnection net;     // Network connection info
    ConnectionState state;    // Current connection state
    
    // Authentication & Identity
    char* server_id;         // Server identifier
    char* server_token;      // Authentication token
    bool auth_complete;      // Authentication process completed
    bool auth_success;       // Authentication succeeded
    
    // Connection management
    int reconnect_attempts;  // Number of reconnection attempts
    bool is_reconnecting;    // Currently attempting reconnection
    
    // Protocol state
    uint16_t sequence;       // Message sequence number
    time_t last_keepalive;   // Last keepalive sent
    time_t last_health_check; // Last health check
    
    // Monitoring & Health
    DatabaseHealth last_health;  // Last health check result
    PingState ping_state;      // Ping/Pong state tracking
    
    // Server metadata
    ServerInfoPayload server_info;  // Server information
    
    // External services
    CURL* curl;                    // CURL handle for HTTP requests
    char curl_error[CURL_ERROR_SIZE]; // CURL error buffer

    // Add connection quality tracking
    ConnectionQualityMetrics metrics;   // Add connection quality tracking
} DatabaseClient;

//-----------------------------------------------------------------------------
// Network Operations
//-----------------------------------------------------------------------------

// Connection management
bool network_connect(NetworkConnection* net, const char* host, int port);
void network_disconnect(NetworkConnection* net);
bool init_socket(DatabaseClient* client);

// Add send wrapper function declaration
ssize_t db_client_send(DatabaseClient* client, const void* data, size_t len);

// Add thread function declaration
void* db_client_reconnect_thread(void* arg);

// Add environment function declarations
const char* getEnvOrDefault(const char* key, const char* default_value);

//-----------------------------------------------------------------------------
// Client API
//-----------------------------------------------------------------------------

// Lifecycle management
bool db_client_init(DatabaseClient* client, const char* host, int port,
                   const char* server_id, const char* server_token);
void db_client_cleanup(DatabaseClient* client);

// Connection operations
bool db_client_connect(DatabaseClient* client);
bool db_client_ensure_connected(DatabaseClient* client);
bool db_client_authenticate(DatabaseClient* client);
bool db_client_wait_for_auth(DatabaseClient* client, int timeout_seconds);

// Health & monitoring
bool db_client_ping(DatabaseClient* client);
bool validateHealthValues(const DatabaseHealth* health);
ConnectionState db_client_get_state(const DatabaseClient* client);

// Authentication
bool db_client_verify_token(DatabaseClient* client, const char* token,
                          TokenVerifyResult* result);

// Retry Mechanisms
bool db_client_retry_operation(DatabaseClient* client, 
                             bool (*operation)(DatabaseClient*),
                             const char* op_name);
void db_client_sleep_with_backoff(int attempt);

// Pong message handling
bool db_client_process_pong(DatabaseClient* client, const MessageHeader* response);
bool db_client_wait_for_pong(DatabaseClient* client, uint16_t expected_sequence);

// Reconnection handling
bool db_client_handle_disconnect(DatabaseClient* client);
bool db_client_reconnect(DatabaseClient* client);

// Add message processing function declaration in Client API section
bool db_client_process_messages(DatabaseClient* client);

#endif // DB_CLIENT_H
