#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Add debug flags
#define NET_DEBUG_PROTOCOL 1  // Enable protocol debug logging

// Protocol message types - Unified definitions
// Core protocol messages (0x01-0x0F)
#define MSG_AUTH_REQUEST        0x01  // Auth can be multi-part
#define MSG_AUTH_RESPONSE       0x02  
#define MSG_VERIFY_TOKEN        0x03
#define MSG_SERVER_INFO         0x04
#define MSG_HEALTH_CHECK        0x05  // Single part message
#define MSG_HEALTH_RESPONSE     0x06
#define MSG_VERIFY_SESSION      0x07

// Player auth messages
#define MSG_LOGIN              0x11
#define MSG_REGISTER           0x12

// System messages
#define MSG_ERROR             0xFF

// Connection & Authentication (0x10-0x1F) 
#define MSG_CONNECT_REQUEST     0x10
#define MSG_CONNECT_SUCCESS     0x11
#define MSG_TOKEN_VERIFY        0x12
#define MSG_AUTH_FAILURE        0x13
#define MSG_KEEP_ALIVE          0x14   // Client -> Server keepalive 
#define MSG_KEEP_ALIVE_RESP     0x15   // Server -> Client keepalive response
#define MSG_SERVER_ERROR       0x15

// Player Actions (0x20-0x2F)
#define MSG_PLAYER_MOVEMENT    0x20
#define MSG_OTHER_MOVEMENT     0x21
#define MSG_PLAYER_ACTION      0x22
#define MSG_SHIP_INPUT        0x23
#define MSG_PLAYER_STATE      0x24
#define MSG_MOUNT_REQUEST     0x25
#define MSG_PLAYER_ERROR      0x2F

// Ship Related (0x30-0x3F) 
#define MSG_SHIP_STATE        0x30
#define MSG_SHIP_MOVEMENT     0x31
#define MSG_SHIP_ACTION       0x32
#define MSG_MODULE_STATE      0x33
#define MSG_SHIP_ERROR        0x3F

// Projectiles (0x40-0x4F)
#define MSG_PROJECTILE_SPAWN  0x40
#define MSG_PROJECTILE_UPDATE 0x41
#define MSG_PROJECTILE_HIT    0x42
#define MSG_PROJECTILE_ERROR  0x4F

// Entity Messages (0x50-0x5F)
#define MSG_ENTITY_SPAWN      0x50
#define MSG_ENTITY_UPDATE     0x51
#define MSG_ENTITY_ACTION     0x52
#define MSG_ENTITY_DESPAWN    0x53

// World State (0x60-0x6F)
#define MSG_WORLD_STATE       0x60
#define MSG_WIND_UPDATE       0x61
#define MSG_ISLAND_STATE      0x62
#define MSG_WORLD_ERROR       0x6F

// Protocol constants
#define MAX_TOKEN_LENGTH 1024
#define HEADER_SIZE 5  // 1 byte type + 4 bytes length

// Health check response structure
typedef struct {
    uint8_t status;        // 1 = healthy, 0 = unhealthy
    uint64_t timestamp;    // Unix timestamp
    uint32_t db_latency;   // Database latency in ms
    uint64_t memory_used;  // Memory used in bytes
    uint64_t memory_total; // Total memory in bytes
    uint64_t uptime_ms;    // Uptime in milliseconds
} DatabaseHealth;

// Token verification response
typedef struct {
    bool success;
    union {
        uint32_t player_id;  // On success
        char error[256];     // On error
    } data;
} TokenVerifyResult;

// Message Structures
typedef struct {
    uint8_t type;
    uint32_t length;
    uint8_t* data;
} NetworkMessage;

// Connection Messages
typedef struct {
    char token[1024];
} ConnectRequest;

typedef struct {
    uint32_t player_id;
    uint32_t session_id;
} ConnectSuccess;

typedef struct {
    char message[256];
} AuthFailure;

// Auth response message (2 bytes)
typedef struct {
    uint8_t type;    // Always MSG_AUTH_RESPONSE
    uint8_t success; // 1 = success, 0 = failure
} __attribute__((packed)) AuthResponseMessage;

// Message header structure (8 bytes total) - matches Auth Server requirements
typedef struct {
    uint8_t type;          // Message type and flags
    uint8_t version;       // Protocol version (1)
    uint16_t sequence;     // Sequence number
    uint32_t length;       // Payload length in bytes
} __attribute__((packed)) MessageHeader;  

// Fixed-size auth payload (136 bytes)
typedef struct {
    char server_id[8];     // Fixed 8 bytes, null padded
    char token[128];       // Fixed 128 bytes, null padded
} __attribute__((packed)) AuthPayload;

// Add a MultiPartHeader for messages that need to be split:
typedef struct {
    MessageHeader header;  // Standard header
    uint16_t part;        // Current part number (0-based)
    uint16_t parts;       // Total number of parts
} __attribute__((packed)) MultiPartHeader;

// Add keepalive message format
typedef struct {
    MessageHeader header;  // type=MSG_KEEP_ALIVE, length=0
} __attribute__((packed)) KeepaliveMessage;

// Add keepalive response format  
typedef struct {
    MessageHeader header;  // type=MSG_KEEP_ALIVE_RESP, length=1
    uint8_t status;       // 1=OK, 0=Error
} __attribute__((packed)) KeepaliveResponse;

// Message building helper
typedef struct {
    uint8_t* buffer;         // Complete message buffer
    size_t capacity;         // Total buffer capacity
    size_t length;          // Current message length
    uint16_t max_part_size;  // Maximum size per part
    uint16_t part_count;     // Number of parts needed
    uint16_t seq;           // Sequence number for this message
} MessageBuilder;

// Message assembly helper
typedef struct {
    uint8_t* parts[256];     // Buffer for each part
    size_t part_sizes[256];  // Size of each received part
    uint16_t expected_parts; // Total parts expected
    uint16_t received_parts; // Number of parts received
    uint16_t seq;           // Expected sequence number
    uint8_t type;           // Message type
} MessageAssembler;

// Message type flags (top 3 bits of type byte)
#define MSG_FLAG_MULTI_PART  0x80  // Bit 7: Multi-part message
#define MSG_FLAG_LAST_PART   0x40  // Bit 6: Last part of multi-part message
#define MSG_FLAG_FIRST_PART  0x20  // Bit 5: First part of multi-part message
#define MSG_TYPE_MASK        0x1F  // Bits 0-4: Actual message type

// Network utility functions
uint32_t readUint32(const uint8_t* buffer);
uint64_t readUint64(const uint8_t* buffer);
void writeUint32(uint8_t* buffer, uint32_t value);
void writeUint64(uint8_t* buffer, uint64_t value);

// Add function declarations
bool processClientMessage(NetworkMessage* msg);
bool sendServerMessage(NetworkMessage* msg);

// Update function declarations to use MultiPartHeader
MessageBuilder* createMessageBuilder(size_t initial_size, uint16_t max_part_size);
void freeMessageBuilder(MessageBuilder* builder);
bool addMessageData(MessageBuilder* builder, const void* data, size_t length);
size_t getNextMessagePart(MessageBuilder* builder, uint8_t** part_buffer, MultiPartHeader* header);

MessageAssembler* createMessageAssembler(void);
void freeMessageAssembler(MessageAssembler* assembler);
bool addMessagePart(MessageAssembler* assembler, const MultiPartHeader* header,
                   const uint8_t* data, size_t length);
bool isMessageComplete(const MessageAssembler* assembler);
uint8_t* getCompleteMessage(MessageAssembler* assembler, size_t* total_length);

#endif // NET_PROTOCOL_H
