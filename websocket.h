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

// Add URL constants
#define WS_CONNECT_PATH "/game/connect"
#define WS_TOKEN_PARAM "token="
#define WS_URL_MAX_LEN 512

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

// Add WebSocket server functions
bool ws_start_server(const char* host, int port);
bool ws_has_pending_connections(void);
const char* ws_get_connect_token(void);
WebSocket* ws_accept_connection(void);
void ws_stop_server(void);

// Add missing declarations
void ws_set_message_handler(WebSocket* ws, void (*handler)(void*, const uint8_t*, size_t));
bool ws_send_health_check(WebSocket* ws);  // New function to replace sendWebSocketHealthCheck

// Add URL helper functions
bool ws_parse_connect_url(const char* url, char* host, int* port, char* token);
char* ws_build_connect_url(const char* host, int port, const char* token);

#endif
