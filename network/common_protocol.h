#ifndef COMMON_PROTOCOL_H
#define COMMON_PROTOCOL_H

#include <stdint.h>

// Basic message header used by all protocols
typedef struct {
    uint8_t type;           // Message type
    uint8_t flags;          // Message flags
    uint16_t sequence;      // Sequence number
    uint32_t length;        // Payload length
} __attribute__((packed)) CommonMessageHeader;

// Common flags for all protocols
#define PROTO_FLAG_NONE        0x00
#define PROTO_FLAG_COMPRESSED  0x01
#define PROTO_FLAG_ENCRYPTED   0x02
#define PROTO_FLAG_NEEDS_ACK   0x04

#endif
