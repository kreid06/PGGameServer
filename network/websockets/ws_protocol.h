#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include "../common_protocol.h"

// WebSocket-specific message types (0x50-0x5F)
enum WebSocketTypes {
    WS_MSG_CONNECT = 0x50,
    WS_MSG_READY = 0x51,
    WS_MSG_CLOSE = 0x52
};

// WebSocket handshake message
typedef struct {
    MessageHeader header;  // type must be WS_MSG_CONNECT
    char protocol[32];    // Protocol identifier
    uint16_t version;     // Protocol version
} __attribute__((packed)) WsHandshakeMessage;

#endif
