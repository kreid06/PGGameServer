#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// Protocol message types
#define MSG_VERIFY_TOKEN 0x03
#define MSG_HEALTH_CHECK 0x04
#define MSG_HEALTH_RESP 0x05
#define MSG_ERROR 0xFF

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

// Network utility functions
uint32_t readUint32(const uint8_t* buffer);
uint64_t readUint64(const uint8_t* buffer);
void writeUint32(uint8_t* buffer, uint32_t value);
void writeUint64(uint8_t* buffer, uint64_t value);

#endif // NET_PROTOCOL_H
