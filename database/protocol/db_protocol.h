#ifndef DB_PROTOCOL_H
#define DB_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Protocol Constants
#define MESSAGE_VERSION        0x01
#define MAX_ERROR_LENGTH      256

// Message Flags
#define MSG_FLAG_MULTI_PART  0x80
#define MSG_FLAG_LAST_PART   0x40
#define MSG_FLAG_FIRST_PART  0x20
#define MSG_TYPE_MASK        0x1F

// Auth Server Message Types
#define MSG_AUTH_REQUEST      0x01
#define MSG_AUTH_RESPONSE     0x02
#define MSG_VERIFY_TOKEN      0x03
#define MSG_TOKEN_RESPONSE    0x04
#define MSG_ERROR            0x05
#define MSG_PING             0x06
#define MSG_PONG             0x07
#define MSG_CONN_ACK         0x08
#define MSG_SERVER_INFO      0x09
#define MSG_HEALTH_CHECK     0x0A
#define MSG_HEALTH_RESPONSE  0x0B

// Message header (8 bytes)
typedef struct {
    uint8_t type;          // 1 byte
    uint8_t version;       // 1 byte
    uint16_t sequence;     // 2 bytes
    uint32_t length;       // 4 bytes
} __attribute__((packed)) MessageHeader;

// Multi-part message header
typedef struct {
    MessageHeader header;
    uint16_t part;        // Current part number
    uint16_t parts;       // Total number of parts
} __attribute__((packed)) MultiPartHeader;

// Auth request payload (288 bytes)
typedef struct {
    char server_id[32];      // 32 bytes
    char auth_token[256];    // 256 bytes
} __attribute__((packed)) AuthRequestPayload;

// Auth response structure
typedef struct {
    MessageHeader header;
    uint8_t success;       // 1 = success, 0 = failure
    char error[256];      // Error message if failed
} __attribute__((packed)) AuthResponseMessage;

// Server info payload
typedef struct {
    uint16_t version;       // Protocol version
    uint16_t max_players;   // Maximum players supported
    uint32_t features;      // Feature flags
} __attribute__((packed)) ServerInfoPayload;

// Token verification result
typedef struct {
    bool success;           // Verification success/failure
    union {
        uint32_t player_id; // Valid player ID if success
        char error[256];    // Error message if failure
    } data;
} TokenVerifyResult;

// Connection result
typedef struct {
    uint8_t status;        // 1 = success, 0 = failure
    char error[256];       // Error message if failed
} AuthConnResult;

// Health check structures
typedef struct {
    uint8_t status;        
    uint64_t timestamp;    // Add timestamp field
    uint32_t db_latency;   
    uint64_t memory_used;  
    uint64_t memory_total; 
    uint64_t uptime_ms;    
    char token[256];       // Changed from uint64_t to char array to store session token
} __attribute__((packed)) DatabaseHealth;

// Health check response
typedef struct {
    uint8_t status;        // 1 = healthy, 0 = unhealthy
    uint64_t timestamp;    // Unix timestamp
    uint32_t db_latency;   // Database latency in ms
    uint64_t memory_used;  // Current memory usage
    uint64_t memory_total; // Total available memory
    uint64_t uptime;      // Server uptime in ms
    char token[256];      // Changed from uint64_t to char array to match auth protocol
} __attribute__((packed)) HealthResponsePayload;

// Response structure for CURL operations
typedef struct {
    uint8_t* buffer;
    size_t size;
} CurlResponse;

#endif
