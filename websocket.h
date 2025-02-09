#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

// Frame types
#define WS_FRAME_CONT  0x0
#define WS_FRAME_TEXT  0x1
#define WS_FRAME_BIN   0x2
#define WS_FRAME_CLOSE 0x8
#define WS_FRAME_PING  0x9
#define WS_FRAME_PONG  0xA

typedef struct {
    int sock;  // Changed from sock_fd to sock
    char* host;
    int port;
    char* path;
    bool connected;
    char* auth_id;
    char* auth_token;
    void (*on_message)(void* user, const uint8_t* data, size_t len);
    void* user_data;
    uint8_t rx_buffer[4096];
    size_t rx_len;
    time_t last_ping;
    time_t last_pong;
} WebSocket;

// Core WebSocket functions
bool ws_connect(WebSocket* ws);
void ws_disconnect(WebSocket* ws);
bool ws_send_binary(WebSocket* ws, const uint8_t* data, size_t len);
bool ws_send_ping(WebSocket* ws);
bool ws_send_pong(WebSocket* ws);
void ws_handle_ping(WebSocket* ws);
void ws_service(WebSocket* ws);  // Call this regularly to handle incoming data

// Add missing declarations
void ws_set_message_handler(WebSocket* ws, void (*handler)(void*, const uint8_t*, size_t));
bool ws_send_health_check(WebSocket* ws);  // New function to replace sendWebSocketHealthCheck

#endif
