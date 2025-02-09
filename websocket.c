#include "websocket.h"
#include "net_protocol.h" // Add this to get message type definitions
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>

// OpenSSL headers with proper order
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <time.h>

// WebSocket frame control bits
#define WS_FIN  0x80
#define WS_MASK 0x80

// Add handshake verification constants
#define MAX_HEADERS 32
#define MAX_HEADER_SIZE 1024
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// Add global server state
static struct {
    int listen_fd;
    bool running;
    char current_token[1024];
} ws_server = {0};

bool ws_start_server(const char* host, int port) {
    ws_server.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ws_server.listen_fd < 0) {
        fprintf(stderr, "Failed to create server socket\n");
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ws_server.listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind server socket\n");
        close(ws_server.listen_fd);
        return false;
    }

    if (listen(ws_server.listen_fd, 5) < 0) {
        fprintf(stderr, "Failed to listen on server socket\n");
        close(ws_server.listen_fd);
        return false;
    }

    ws_server.running = true;
    return true;
}

bool ws_has_pending_connections(void) {
    if (!ws_server.running) return false;

    fd_set readfds;
    struct timeval tv = {0, 0}; // Non-blocking
    
    FD_ZERO(&readfds);
    FD_SET(ws_server.listen_fd, &readfds);

    return select(ws_server.listen_fd + 1, &readfds, NULL, NULL, &tv) > 0;
}

const char* ws_get_connect_token(void) {
    if (ws_server.current_token[0] == '\0') {
        fprintf(stderr, "[WS] Warning: No token found in current connection\n");
        return NULL;
    }
    return ws_server.current_token;
}

static bool complete_handshake(WebSocket* ws, const char* sec_ws_key) {
    char accept_key[WS_ACCEPT_LENGTH];
    unsigned char hash[SHA_DIGEST_LENGTH];
    char concat_buf[WS_KEY_LENGTH + sizeof(WS_GUID)];
    
    // Concatenate key with GUID
    snprintf(concat_buf, sizeof(concat_buf), "%s%s", sec_ws_key, WS_GUID);
    
    // Generate SHA1
    SHA1((unsigned char*)concat_buf, strlen(concat_buf), hash);
    
    // Base64 encode
    EVP_EncodeBlock((unsigned char*)accept_key, hash, SHA_DIGEST_LENGTH);
    
    // Send handshake response
    char response[1024];
    snprintf(response, sizeof(response), WS_HANDSHAKE_RESPONSE, accept_key);
    
    return send(ws->sock, response, strlen(response), 0) > 0;
}

WebSocket* ws_accept_connection(void) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(ws_server.listen_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        fprintf(stderr, "[WS] Failed to accept client connection: %s\n", strerror(errno));
        return NULL;
    }

    // Log connection attempt with IP address
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    fprintf(stderr, "[WS] New client connection from %s:%d\n", 
            client_ip, ntohs(client_addr.sin_port));

    // Initialize WebSocket structure with calloc
    WebSocket* ws = calloc(1, sizeof(WebSocket));
    if (!ws) {
        fprintf(stderr, "[WS] Failed to allocate memory for WebSocket\n");
        close(client_fd);
        return NULL;
    }

    // Mark as initialized and valid
    ws->initialized = true;
    ws->valid = true;
    ws->sock = client_fd;
    ws->connected = false;  // Not connected until handshake complete
    ws->handshake_complete = false;

    // Read HTTP request headers
    char request_buffer[4096] = {0};
    size_t total_read = 0;
    
    while (total_read < sizeof(request_buffer) - 1) {
        ssize_t bytes = recv(client_fd, request_buffer + total_read, 
                           sizeof(request_buffer) - total_read - 1, 0);
        if (bytes <= 0) {
            fprintf(stderr, "[WS] Failed to read request headers\n");
            free(ws);
            close(client_fd);
            return NULL;
        }
        total_read += bytes;
        
        if (strstr(request_buffer, "\r\n\r\n")) break;
    }
    request_buffer[total_read] = '\0';

    fprintf(stderr, "[WS] Received request headers:\n%s\n", request_buffer);

    // First extract token before handshake
    char* token_start = strstr(request_buffer, "token=");
    if (token_start) {
        token_start += 6;
        char* token_end = strpbrk(token_start, " \r\n&");
        if (token_end) {
            size_t token_len = token_end - token_start;
            if (token_len < sizeof(ws_server.current_token)) {
                memcpy(ws_server.current_token, token_start, token_len);
                ws_server.current_token[token_len] = '\0';
                fprintf(stderr, "[WS] Extracted token length: %zu\n", token_len);
            }
        }
    } else {
        fprintf(stderr, "[WS] No token found in request\n");
        // Still continue with handshake
    }

    // Now handle WebSocket handshake
    char* key_start = strstr(request_buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        fprintf(stderr, "[WS] No WebSocket key found\n");
        free(ws);
        close(client_fd);
        return NULL;
    }

    key_start += 19;
    char* key_end = strstr(key_start, "\r\n");
    if (!key_end || (key_end - key_start) > WS_KEY_LENGTH) {
        fprintf(stderr, "[WS] Invalid WebSocket key length\n");
        free(ws);
        close(client_fd);
        return NULL;
    }

    // Store key temporarily
    size_t key_len = key_end - key_start;
    char sec_ws_key[WS_KEY_LENGTH + 1] = {0};
    memcpy(sec_ws_key, key_start, key_len);

    // Complete handshake
    if (!complete_handshake(ws, sec_ws_key)) {
        fprintf(stderr, "[WS] Failed to complete WebSocket handshake\n");
        ws->valid = false;
        free(ws);
        close(client_fd);
        return NULL;
    }

    // Only now mark as connected and ready
    ws->handshake_complete = true;
    ws->connected = true;
    fprintf(stderr, "[WS] WebSocket handshake complete, connection ready\n");

    return ws;
}

void ws_stop_server(void) {
    if (ws_server.running) {
        close(ws_server.listen_fd);
        ws_server.running = false;
    }
}

void ws_disconnect(WebSocket* ws) {
    if (!ws) return;
    
    if (ws->connected) {
        // Get peer address for logging
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        if (getpeername(ws->sock, (struct sockaddr*)&peer, &peer_len) == 0) {
            char peer_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
            fprintf(stderr, "[WS] Client disconnecting: %s:%d\n", 
                    peer_ip, ntohs(peer.sin_port));
        }

        // Send close frame and close socket
        uint8_t close_frame[] = {
            WS_FRAME_CLOSE | WS_FIN,
            0x00
        };
        send(ws->sock, close_frame, sizeof(close_frame), MSG_NOSIGNAL);
        close(ws->sock);
        ws->connected = false;
    }

    // Free all allocated resources
    if (ws->host) {
        free(ws->host);
        ws->host = NULL;
    }
    if (ws->path) {
        free(ws->path);
        ws->path = NULL;
    }
    if (ws->auth_id) {
        free(ws->auth_id);
        ws->auth_id = NULL;
    }
    if (ws->auth_token) {
        free(ws->auth_token);
        ws->auth_token = NULL;
    }
}

bool ws_connect(WebSocket* ws) {
    // Create socket
    ws->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ws->sock < 0) {
        fprintf(stderr, "Failed to create WebSocket: %s\n", strerror(errno));
        return false;
    }

    // Connect to server
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ws->port);
    if (inet_pton(AF_INET, ws->host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid WebSocket address: %s\n", ws->host);
        close(ws->sock);
        return false;
    }

    if (connect(ws->sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "WebSocket connect failed: %s\n", strerror(errno));
        close(ws->sock);
        return false;
    }

    // Set socket to non-blocking mode
    int flags = fcntl(ws->sock, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(ws->sock, F_SETFL, flags | O_NONBLOCK);
    }

    ws->connected = true;
    ws->rx_len = 0;
    ws->last_ping = time(NULL);
    ws->last_pong = time(NULL);

    return true;
}

bool ws_send_binary(WebSocket* ws, const uint8_t* data, size_t len) {
    if (!ws || !ws->connected) return false;

    // Construct WebSocket frame
    uint8_t header[10] = {0};  // Max header size
    size_t header_len = 2;     // Minimum header size

    // Set frame type and FIN bit
    header[0] = WS_FRAME_BIN | WS_FIN;

    // Set payload length
    if (len <= 125) {
        header[1] = len;
    } else if (len <= 65535) {
        header[1] = 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        header_len = 4;
    } else {
        header[1] = 127;
        // Write 8-byte length
        for (int i = 0; i < 8; i++) {
            header[2+i] = (len >> ((7-i) * 8)) & 0xFF;
        }
        header_len = 10;
    }

    // Send header
    if (send(ws->sock, header, header_len, MSG_NOSIGNAL) != header_len) {
        return false;
    }

    // Send payload
    if (send(ws->sock, data, len, MSG_NOSIGNAL) != len) {
        return false;
    }

    return true;
}

void ws_set_message_handler(WebSocket* ws, void (*handler)(void*, const uint8_t*, size_t)) {
    if (ws) {
        ws->on_message = handler;
    }
}

bool ws_send_health_check(WebSocket* ws) {
    if (!ws || !ws->connected) {
        fprintf(stderr, "[WS] Cannot send health check - websocket not connected\n");
        return false;
    }

    // Create health check message
    uint8_t health_check[] = {
        MSG_HEALTH_CHECK,  // Message type
        0x01,             // Version 1
        0x00, 0x00,      // Sequence number (not used for health checks)
        0x00, 0x00, 0x00, 0x00  // Length = 0
    };

    // Send using binary frame
    return ws_send_binary(ws, health_check, sizeof(health_check));
}

bool ws_parse_connect_url(const char* url, char* host, int* port, char* token) {
    // Expected format: ws://hostname:port/game/connect?token=<login_token>
    char protocol[8];
    char path[128];
    
    if (sscanf(url, "%[^:]://%[^:]:%d%s", protocol, host, port, path) != 4) {
        fprintf(stderr, "[WS] Invalid URL format: %s\n", url);
        return false;
    }

    // Verify protocol
    if (strcmp(protocol, "ws") != 0) {
        fprintf(stderr, "[WS] Invalid protocol: %s\n", protocol);
        return false;
    }

    // Extract token from query string
    char* token_start = strstr(path, WS_TOKEN_PARAM);
    if (!token_start) {
        fprintf(stderr, "[WS] No token in URL\n");
        return false;
    }
    
    strcpy(token, token_start + strlen(WS_TOKEN_PARAM));
    fprintf(stderr, "[WS] Parsed connection URL - host:%s port:%d\n", host, *port);
    return true;
}

char* ws_build_connect_url(const char* host, int port, const char* token) {
    char* url = malloc(WS_URL_MAX_LEN);
    if (!url) return NULL;

    snprintf(url, WS_URL_MAX_LEN, "ws://%s:%d%s?%s%s",
             host, port, WS_CONNECT_PATH, WS_TOKEN_PARAM, token);
    
    return url;
}

// ... rest of existing code ...

