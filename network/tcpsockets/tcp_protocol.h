#ifndef TCP_PROTOCOL_H
#define TCP_PROTOCOL_H

#include "../common_protocol.h"

// TCP-specific message types (0x60-0x6F)
enum TcpMessageTypes {
    TCP_MSG_HANDSHAKE = 0x60,
    TCP_MSG_READY = 0x61,
    TCP_MSG_CLOSE = 0x62
};

// TCP handshake message
typedef struct {
    MessageHeader header;  // type must be TCP_MSG_HANDSHAKE
    uint16_t window_size; // Receive window size
    uint8_t features;     // Supported features
} __attribute__((packed)) TcpHandshakeMessage;

#endif
