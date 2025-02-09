#include "websocket.h"
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

// Replace the SHA1 implementation with EVP
static void sha1(const uint8_t* input, size_t ilen, uint8_t output[SHA_DIGEST_LENGTH]) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, input, ilen);
    unsigned int out_len;
    EVP_DigestFinal_ex(ctx, output, &out_len);
    EVP_MD_CTX_free(ctx);
}

// Rewrite base64 encode function to use EVP
static char* base64_encode(const unsigned char* input, int length) {
    EVP_ENCODE_CTX* ctx = EVP_ENCODE_CTX_new();
    EVP_EncodeInit(ctx);
    
    // Calculate output size (approximately 4/3 of input size + padding)
    size_t outlen = ((length + 2) / 3) * 4 + 1;
    char* output = (char*)malloc(outlen);
    int encoded_len = 0;
    
    EVP_EncodeUpdate(ctx, (unsigned char*)output, &encoded_len, input, length);
    
    int final_len = 0;
    EVP_EncodeFinal(ctx, (unsigned char*)(output + encoded_len), &final_len);
    
    EVP_ENCODE_CTX_free(ctx);
    output[encoded_len + final_len] = '\0';
    
    return output;
}

static char* generate_ws_key(void) {
    unsigned char random[16];
    FILE* urandom = fopen("/dev/urandom", "r");
    if (!urandom) {
        return NULL;
    }
    if (fread(random, 1, 16, urandom) != 16) {
        fclose(urandom);
        return NULL;
    }
    fclose(urandom);
    
    return base64_encode(random, 16);
}

// Fix websocket accept calculation
static void calculate_websocket_accept(const char* key, char* accept) {
    unsigned char sha1_hash[SHA_DIGEST_LENGTH];  // Use SHA_DIGEST_LENGTH
    char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concatenated[256];
    
    snprintf(concatenated, sizeof(concatenated), "%s%s", key, magic);
    sha1((unsigned char*)concatenated, strlen(concatenated), sha1_hash);
    
    // Base64 encode the SHA1 hash
    char* encoded = base64_encode(sha1_hash, SHA_DIGEST_LENGTH);
    strcpy(accept, encoded);
    free(encoded);
}

// Add structure to store response headers
typedef struct {
    char* headers[MAX_HEADERS];
    int count;
} ResponseHeaders;

// Add helper to parse HTTP headers
static bool parse_http_headers(const char* response, ResponseHeaders* headers) {
    char* line = strtok((char*)response, "\r\n");
    
    // Verify HTTP status line
    if (!line || strncmp(line, "HTTP/1.1 101", 12) != 0) {
        fprintf(stderr, "Invalid HTTP status: %s\n", line ? line : "null");
        return false;
    }
    
    // Parse headers
    while ((line = strtok(NULL, "\r\n")) != NULL && headers->count < MAX_HEADERS) {
        if (strlen(line) == 0) break; // Empty line marks end of headers
        headers->headers[headers->count++] = strdup(line);
    }
    
    return true;
}

// Add helper to find header value
static const char* find_header(const ResponseHeaders* headers, const char* name) {
    for (int i = 0; i < headers->count; i++) {
        if (strncasecmp(headers->headers[i], name, strlen(name)) == 0) {
            const char* value = strchr(headers->headers[i], ':');
            if (value) {
                while (*++value == ' '); // Skip whitespace
                return value;
            }
        }
    }
    return NULL;
}

// Modify send_handshake for exact header format
static char* send_handshake(WebSocket* ws, char* accept_key) {
    char* key = generate_ws_key();
    if (!key) {
        fprintf(stderr, "Failed to generate WebSocket key\n");
        return NULL;
    }

    // Exactly match the required handshake format
    char request[1024];
    snprintf(request, sizeof(request), 
        "GET /ws/health HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: health-protocol\r\n"
        "X-Game-Server-ID: %s\r\n"
        "X-Game-Server-Token: %s\r\n"
        "\r\n",
        ws->host, ws->port, key,
        ws->auth_id, ws->auth_token);

    fprintf(stderr, "Sending handshake request:\n%s", request);

    ssize_t sent = send(ws->sock, request, strlen(request), 0);
    if (sent <= 0) {
        perror("Failed to send handshake");
        free(key);
        return NULL;
    }
    fprintf(stderr, "Sent %zd bytes\n", sent);

    // Calculate expected accept key
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", key, WS_GUID);
    unsigned char sha1_hash[SHA_DIGEST_LENGTH];
    sha1((uint8_t*)concat, strlen(concat), sha1_hash);
    char* encoded = base64_encode(sha1_hash, SHA_DIGEST_LENGTH);
    strcpy(accept_key, encoded);
    free(encoded);
    free(key);
    
    return accept_key;
}

// Add verify_handshake function
static bool verify_handshake(WebSocket* ws, const char* expected_accept) {
    char buffer[4096] = {0};
    ssize_t total = 0;
    
    // Read response with timeout
    fd_set fds;
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    
    FD_ZERO(&fds);
    FD_SET(ws->sock, &fds);
    
    if (select(ws->sock + 1, &fds, NULL, NULL, &tv) <= 0) {
        fprintf(stderr, "Handshake timeout\n");
        return false;
    }
    
    total = recv(ws->sock, buffer, sizeof(buffer) - 1, 0);
    if (total <= 0) {
        fprintf(stderr, "Failed to read handshake response\n");
        return false;
    }
    buffer[total] = '\0';

    ResponseHeaders headers = {0};
    if (!parse_http_headers(buffer, &headers)) {
        return false;
    }

    // Verify exact response format
    const char* expected_headers[] = {
        "HTTP/1.1 101 Switching Protocols",
        "Upgrade: websocket",
        "Connection: Upgrade", 
        "Sec-WebSocket-Protocol: health-protocol"
    };

    // Parse and validate each header exactly
    char* line = strtok((char*)buffer, "\r\n");
    int header_idx = 0;
    
    while (line && header_idx < 4) {
        if (strcmp(line, expected_headers[header_idx]) != 0) {
            fprintf(stderr, "Header mismatch:\nExpected: %s\nGot: %s\n", 
                    expected_headers[header_idx], line);
            return false;
        }
        header_idx++;
        line = strtok(NULL, "\r\n");
    }

    // Verify required headers
    const char* upgrade = find_header(&headers, "Upgrade");
    const char* connection = find_header(&headers, "Connection");
    const char* accept = find_header(&headers, "Sec-WebSocket-Accept");
    const char* protocol = find_header(&headers, "Sec-WebSocket-Protocol");

    bool valid = upgrade && strcasecmp(upgrade, "websocket") == 0 &&
                connection && strcasecmp(connection, "Upgrade") == 0 &&
                accept && strcmp(accept, expected_accept) == 0 &&
                protocol && strcmp(protocol, "health-protocol") == 0;

    // Cleanup
    for (int i = 0; i < headers.count; i++) {
        free(headers.headers[i]);
    }

    return valid;
}

// Fix ws_send_frame to handle fread return value
bool ws_send_frame(WebSocket* ws, uint8_t opcode, const uint8_t* payload, size_t len) {
    uint8_t header[14];  // Maximum header size
    size_t header_len = 2;
    
    header[0] = WS_FIN | opcode;
    
    if (len <= 125) {
        header[1] = WS_MASK | (uint8_t)len;
    } else if (len <= 65535) {
        header[1] = WS_MASK | 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        header_len += 2;
    } else {
        header[1] = WS_MASK | 127;
        for (int i = 0; i < 8; i++) {
            header[2+i] = (len >> ((7-i) * 8)) & 0xFF;
        }
        header_len += 8;
    }
    
    // Add mask key with proper error handling
    uint8_t mask[4];
    FILE* urandom = fopen("/dev/urandom", "r");
    if (!urandom) {
        return false;
    }
    if (fread(mask, 1, 4, urandom) != 4) {
        fclose(urandom);
        return false;
    }
    fclose(urandom);
    
    memcpy(header + header_len, mask, 4);
    header_len += 4;
    
    // Send header
    if (send(ws->sock, header, header_len, 0) != header_len) {
        return false;
    }
    
    // Send masked payload
    if (len > 0) {
        uint8_t* masked = malloc(len);
        for (size_t i = 0; i < len; i++) {
            masked[i] = payload[i] ^ mask[i % 4];
        }
        
        bool success = send(ws->sock, masked, len, 0) == len;
        free(masked);
        return success;
    }
    
    return true;
}

bool ws_send_binary(WebSocket* ws, const uint8_t* data, size_t len) {
    return ws_send_frame(ws, WS_FRAME_BIN, data, len);
}

bool ws_send_ping(WebSocket* ws) {
    return ws_send_frame(ws, WS_FRAME_PING, NULL, 0);
}

bool ws_send_pong(WebSocket* ws) {
    return ws_send_frame(ws, WS_FRAME_PONG, NULL, 0);
}

bool ws_connect(WebSocket* ws) {
    fprintf(stderr, "\nAttempting WebSocket connection:\n");
    fprintf(stderr, "Target: %s:%d%s\n", ws->host, ws->port, ws->path);
    fprintf(stderr, "Server ID: %s\n", ws->auth_id);
    
    struct sockaddr_in server;
    struct hostent* host;
    
    // Create socket
    ws->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ws->sock < 0) {
        perror("Socket creation failed");
        return false;
    }
    fprintf(stderr, "Socket created: %d\n", ws->sock);
    
    // For local testing, use direct IP instead of hostname resolution
    server.sin_family = AF_INET;
    server.sin_port = htons(ws->port);
    if (inet_pton(AF_INET, ws->host, &server.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/Address not supported\n");
        close(ws->sock);
        return false;
    }
    
    // Connect with error logging
    fprintf(stderr, "Attempting connection...\n");
    if (connect(ws->sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("Connection failed");
        close(ws->sock);
        return false;
    }
    fprintf(stderr, "TCP connection established\n");
    
    // Send WebSocket handshake
    char accept_key[64] = {0};
    if (!send_handshake(ws, accept_key)) {
        fprintf(stderr, "Failed to send handshake\n");
        close(ws->sock);
        return false;
    }
    fprintf(stderr, "Handshake sent, expecting accept key: %s\n", accept_key);
    
    // Verify handshake response
    if (!verify_handshake(ws, accept_key)) {
        fprintf(stderr, "Handshake verification failed\n");
        close(ws->sock);
        return false;
    }
    
    fprintf(stderr, "WebSocket connection established successfully\n\n");
    ws->connected = true;
    ws->last_ping = time(NULL);
    ws->last_pong = time(NULL);
    
    return true;
}

// Add proper ping/pong handling
void ws_service(WebSocket* ws) {
    uint8_t buffer[4096];
    ssize_t bytes = recv(ws->sock, buffer, sizeof(buffer), MSG_DONTWAIT);
    
    if (bytes > 0) {
        uint8_t opcode = buffer[0] & 0x0F;
        
        switch(opcode) {
            case WS_FRAME_PING:
                ws_send_pong(ws);
                break;
                
            case WS_FRAME_PONG:
                ws->last_pong = time(NULL);
                break;
                
            case WS_FRAME_BIN:
                if (ws->on_message) {
                    // Skip WebSocket frame header
                    ws->on_message(ws->user_data, buffer + 2, bytes - 2);
                }
                break;
        }
    } else if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        ws_disconnect(ws);
    }
}

void ws_disconnect(WebSocket* ws) {
    if (ws->sock >= 0) {
        close(ws->sock);
        ws->sock = -1;
    }
    ws->connected = false;
}

bool ws_send_health_check(WebSocket* ws) {
    uint8_t packet[5] = {0x04, 0, 0, 0, 0};  // [0x04][00 00 00 00]
    return ws_send_binary(ws, packet, sizeof(packet));
}

void ws_set_message_handler(WebSocket* ws, void (*handler)(void*, const uint8_t*, size_t)) {
    ws->on_message = handler;
}

// ... implement remaining functions ...
