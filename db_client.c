#include "db_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>    
#include <unistd.h>  
#include <errno.h>    
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>    // Add this for F_GETFL, F_SETFL, O_NONBLOCK

// Add fallback definitions for systems that might not have them
#ifndef O_NONBLOCK
#define O_NONBLOCK  04000
#endif

#ifndef F_GETFL
#define F_GETFL     3
#endif

#ifndef F_SETFL
#define F_SETFL     4
#endif

// Add fallback definitions if not defined by system headers
#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT    6    /* Number of keepalives before death */
#endif

#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE   4    /* Start keeplives after this period */
#endif

#ifndef TCP_KEEPINTVL
#define TCP_KEEPINTVL  2    /* Interval between keepalives */
#endif

// Forward declare static functions
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata);
static bool parseHealthResponse(const uint8_t* buffer, size_t size, DatabaseHealth* health);
static bool initializeHealthCheck(DatabaseClient* client);
static void sendWebSocketHealthCheck(DatabaseClient* client);
static void onWebSocketMessage(void* user, const uint8_t* data, size_t len);
static bool sendAuthRequest(DatabaseClient* client);
static bool readMultiPartMessage(DatabaseClient* client, uint8_t* buffer, size_t* total_size);
static bool sendMessage(DatabaseClient* client, uint8_t type, const void* payload, uint32_t length);
static bool readMessage(DatabaseClient* client, uint8_t* type, void* payload, uint32_t* length);
static bool skipMessagePayload(int sock, size_t length);

// Health check packet: [0x04][00 00 00 00]
static uint8_t health_check_packet[] = {
    MSG_HEALTH_CHECK,  // Now using 0x05
    0x00, 0x00, 0x00, 0x00
};

// Curl callback function
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    CurlResponse* resp = (CurlResponse*)userp;
    
    if (resp->size + total_size > 1024) {  // Prevent buffer overflow
        return 0;  // Return 0 to indicate error
    }
    
    memcpy(resp->buffer + resp->size, contents, total_size);
    resp->size += total_size;
    return total_size;
}

// Add header debug callback
static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t totalSize = size * nitems;
    fprintf(stderr, "Received header: %.*s", (int)totalSize, buffer);
    return totalSize;
}

static volatile bool ws_connected = false;
static volatile bool ws_connecting = false;  // Add this flag

// Add ping/pong tracking
static struct {
    bool ping_pending;
    time_t last_ping;
    time_t last_pong;
} ws_state = {0};

static const uint32_t retry_ms_table[] = {
    2000,    // First retry after 2s
    4000,    // Second retry after 4s
    8000,    // Then 8s
    16000    // Then 16s
};


// Add ping packet definition
static uint8_t ping_packet[] = {
    0x89,  // WebSocket ping frame
    0x00   // Zero payload length
};

// Add helper function for hex dump logging
static void log_hex_dump(const char* prefix, const unsigned char* data, size_t len) {
    fprintf(stderr, "%s [%zu bytes]:\n", prefix, len);
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, "%02x ", data[i]);
        if ((i + 1) % 16 == 0) fprintf(stderr, "\n");
        else if ((i + 1) % 8 == 0) fprintf(stderr, " ");
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "ASCII:\n");
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, "%c", isprint(data[i]) ? data[i] : '.');
        if ((i + 1) % 64 == 0) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n\n");
}

static bool readAuthResponse(DatabaseClient* client) {
    // First log the read attempt
    fprintf(stderr, "[DB] Reading auth response...\n");
    
    MessageHeader header;
    size_t totalRead = 0;
    ssize_t bytes;  // Add this declaration
    
    // Keep reading until we get full header
    while (totalRead < sizeof(header)) {
        bytes = recv(client->sock, 
                   ((uint8_t*)&header) + totalRead,
                   sizeof(header) - totalRead, 
                   0);
        
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket would block, wait a bit and retry
                usleep(100000);  // 100ms delay
                continue;
            }
            fprintf(stderr, "[DB] Read error: %s\n", strerror(errno));
            return false;
        } else if (bytes == 0) {
            fprintf(stderr, "[DB] Server closed connection\n");
            return false;
        }
        
        totalRead += bytes;
        fprintf(stderr, "[DB] Received %zd bytes, total %zu of %zu\n",
                bytes, totalRead, sizeof(header));
    }

    // Now we have full header, validate it
    fprintf(stderr, "[DB] Full header received: type=0x%02x, version=%d, seq=%d, len=%d\n",
            header.type, header.version, header.sequence, header.length);

    // Read success flag
    uint8_t success;
    bytes = recv(client->sock, &success, 1, 0);
    if (bytes != 1) {
        fprintf(stderr, "[DB] Failed to read success flag: %s\n", strerror(errno));
        return false;
    }

    fprintf(stderr, "[DB] Auth response received: success=%d\n", success);
    client->auth_complete = (success != 0);
    client->auth_success = (success != 0);

    // Ensure we properly set all state flags on success
    if (client->auth_success) {
        // Set socket back to non-blocking
        int flags = fcntl(client->sock, F_GETFL, 0);
        if (flags != -1) {
            fcntl(client->sock, F_SETFL, flags | O_NONBLOCK);
        }
        
        client->state = CONN_STATE_CONNECTED;
        client->connected = true;
        client->last_keepalive = time(NULL);
        client->last_ping = 0;  // Reset ping/pong tracking
        client->last_pong = time(NULL);
        fprintf(stderr, "[DB] Connection authenticated and ready\n");
    }

    return client->auth_success;
}

static bool initializeHealthCheck(DatabaseClient* client) {
    // Prevent multiple connection attempts
    if (client->state != CONN_STATE_DISCONNECTED) {
        return false;
    }

    fprintf(stderr, "[DB] Initializing health check connection to %s:3001\n", client->host);
    
    // Create socket with error handling
    client->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->sock < 0) {
        fprintf(stderr, "[DB] Socket creation failed: %s (errno=%d)\n", 
                strerror(errno), errno);
        return false;
    }

    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = 5;  // 5 second timeout
    tv.tv_usec = 0;
    if (setsockopt(client->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        fprintf(stderr, "[DB] Failed to set socket timeout: %s\n", strerror(errno));
    }

    // Enable TCP keepalive
    int keepalive = 1;
    int keepcnt = 3;
    int keepidle = 30;
    int keepintvl = 5;
    if (setsockopt(client->sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(int)) < 0) {
        fprintf(stderr, "[DB] Failed to set TCP keepalive: %s\n", strerror(errno));
    }
    if (setsockopt(client->sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(int)) < 0) {
        fprintf(stderr, "[DB] Failed to set TCP keepalive count: %s\n", strerror(errno));
    }
    if (setsockopt(client->sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(int)) < 0) {
        fprintf(stderr, "[DB] Failed to set TCP keepalive idle: %s\n", strerror(errno));
    }
    if (setsockopt(client->sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(int)) < 0) {
        fprintf(stderr, "[DB] Failed to set TCP keepalive interval: %s\n", strerror(errno));
    }

    // Setup address for port 3001
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(3001);
    if (inet_pton(AF_INET, client->host, &server.sin_addr) <= 0) {
        fprintf(stderr, "[DB] Invalid address: %s\n", client->host);
        close(client->sock);
        return false;
    }
    fprintf(stderr, "[DB] Attempting connection to %s:3001...\n", client->host);

    // Connect
    if (connect(client->sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        fprintf(stderr, "[DB] Connection failed: %s\n", strerror(errno));
        close(client->sock);
        return false;
    }
    fprintf(stderr, "[DB] Connection established successfully\n");

    // Save original socket flags
    int sock_flags = fcntl(client->sock, F_GETFL, 0);
    if (sock_flags == -1) {
        fprintf(stderr, "[DB] Failed to get socket flags: %s\n", strerror(errno));
        close(client->sock);
        return false;
    }

    // Set socket to blocking mode for initial handshake
    if (fcntl(client->sock, F_SETFL, sock_flags & ~O_NONBLOCK) == -1) {
        fprintf(stderr, "[DB] Failed to set blocking mode: %s\n", strerror(errno));
        close(client->sock);
        return false;
    }

    // Calculate exact lengths needed
    size_t server_id_len = strlen(client->server_id);
    size_t token_len = strlen(client->server_token);
    
    // Sanity check lengths
    if (server_id_len > 32 || token_len > 128) {
        fprintf(stderr, "[DB] Credentials too long: server_id=%zu, token=%zu\n",
                server_id_len, token_len);
        close(client->sock);
        return false;
    }

    // Send auth request only once
    if (!client->auth_complete) {
        if (!sendAuthRequest(client)) {
            close(client->sock);
            client->state = CONN_STATE_DISCONNECTED;
            return false;
        }
        client->state = CONN_STATE_AUTHENTICATING;

        // Read auth response
        AuthResponseMessage response = {0};
        if (!readAuthResponse(client)) {
            close(client->sock);
            client->state = CONN_STATE_DISCONNECTED;
            return false;
        }

        client->auth_complete = response.success;
    }

    if (client->auth_complete) {
        client->state = CONN_STATE_CONNECTED;
        client->connected = true;
        fprintf(stderr, "[DB] Connection established and authenticated\n");
        return true;
    }

    return false;
}

bool checkDatabaseHealth(DatabaseClient* client, DatabaseHealth* health) {
    time_t now = time(NULL);

    // Only initialize if disconnected
    if (client->state == CONN_STATE_DISCONNECTED) {
        if (!initializeHealthCheck(client)) {
            return false;
        }
    }

    // After authentication, send initial health check
    if (client->state == CONN_STATE_CONNECTED && !client->last_health_check) {
        MessageHeader header = {
            .type = MSG_HEALTH_CHECK,     // Now using 0x05
            .version = MESSAGE_VERSION,
            .sequence = client->sequence++,
            .length = 0  // No payload needed, connection already authenticated
        };

        fprintf(stderr, "[DB] Sending initial health check - type=0x%02x ver=%d seq=%d\n",
                header.type, header.version, header.sequence);

        if (send(client->sock, &header, sizeof(header), MSG_NOSIGNAL) != sizeof(header)) {
            fprintf(stderr, "[DB] Failed to send health check message\n");
            return false;
        }

        // Handle multiple possible responses
        uint8_t buffer[1024];
        time_t start_time = time(NULL);
        const int MAX_WAIT_SECS = 5;  // Maximum wait time
        
        while (time(NULL) - start_time < MAX_WAIT_SECS) {
            ssize_t bytes = recv(client->sock, buffer, sizeof(buffer), 0);
            if (bytes < sizeof(MessageHeader)) {
                fprintf(stderr, "[DB] Invalid message - too short (%zd bytes)\n", bytes);
                return false;
            }

            MessageHeader* resp_header = (MessageHeader*)buffer;
            
            switch(resp_header->type) {
                case MSG_SERVER_INFO:
                    fprintf(stderr, "[DB] Server info message - skipping %d bytes\n", 
                            resp_header->length);
                    if (!skipMessagePayload(client->sock, resp_header->length)) {
                        fprintf(stderr, "[DB] Failed to skip server info payload\n");
                        return false;
                    }
                    fprintf(stderr, "[DB] Successfully skipped server info message\n");
                    continue;

                case MSG_HEALTH_RESPONSE:
                    fprintf(stderr, "[DB] Got health response\n");
                    if (parseHealthResponse(buffer + sizeof(MessageHeader), 
                                         resp_header->length,  // Length might be 0
                                         &client->last_health)) {
                        *health = client->last_health;
                        client->last_health_check = time(NULL);
                        return true;
                    }
                    return false;

                default:
                    fprintf(stderr, "[DB] Unknown message type 0x%02x - skipping %d bytes\n",
                            resp_header->type, resp_header->length);
                    if (!skipMessagePayload(client->sock, resp_header->length)) {
                        fprintf(stderr, "[DB] Failed to skip unknown message payload\n");
                        return false;
                    }
                    continue;
            }
        }

        fprintf(stderr, "[DB] Timed out waiting for health response\n");
        return false;
    }

    // Handle periodic health checks
    if (client->state == CONN_STATE_CONNECTED) {
        if (!client->connected) {
            client->state = CONN_STATE_DISCONNECTED;
            return false;
        }

        // Send health check every 5 seconds
        if (now - client->last_health_check >= 5) {
            MessageHeader header = {
                .type = MSG_HEALTH_CHECK,
                .version = MESSAGE_VERSION,
                .sequence = client->sequence++,
                .length = 0  // No payload needed for regular checks
            };
            
            if (send(client->sock, &header, sizeof(header), MSG_NOSIGNAL) != sizeof(header)) {
                fprintf(stderr, "[DB] Failed to send health check: %s\n", strerror(errno));
                return false;
            }
            
            fprintf(stderr, "[DB] Sent health check type=0x%02x seq=%d\n", 
                    header.type, header.sequence);
        }

        // Handle response
        uint8_t buffer[1024];
        ssize_t bytes = recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT);
        
        if (bytes >= sizeof(MessageHeader)) {
            MessageHeader* header = (MessageHeader*)buffer;
            if (header->type == MSG_HEALTH_RESPONSE) {
                client->last_health_check = now;  // Always update timestamp
                
                if (header->length > 0) {
                    // Parse full health data if provided
                    if (parseHealthResponse(buffer + sizeof(MessageHeader),
                                         header->length,
                                         &client->last_health)) {
                        *health = client->last_health;
                    }
                }
                return true;  // Connection is alive
            }
        }
    }

    // Handle responses
    uint8_t buffer[1024];
    ssize_t bytes = recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT);
    
    if (bytes > 0) {
        if (bytes >= sizeof(MessageHeader)) {
            MessageHeader* header = (MessageHeader*)buffer;
            if (header->type == MSG_HEALTH_RESPONSE) {
                if (parseHealthResponse(buffer + sizeof(MessageHeader), 
                                     bytes - sizeof(MessageHeader), 
                                     &client->last_health)) {
                    client->last_health_check = now;
                    *health = client->last_health;
                    return true;
                }
            }
        }
    }

    // ... rest of existing error handling ...
}

// Add new helper function to parse health response
static bool parseHealthResponse(const uint8_t* buffer, size_t size, DatabaseHealth* health) {
    // Allow empty responses for keepalive-style health checks
    if (size == 0) {
        // Just mark as healthy with minimal data
        health->status = 1;
        health->timestamp = time(NULL);
        health->db_latency = 0;
        health->memory_used = 0;
        health->memory_total = 0;
        health->uptime_ms = 0;
        return true;
    }

    // If we have payload, it must be the full health response
    if (size != sizeof(HealthResponsePayload)) {
        fprintf(stderr, "[DB] Invalid health response size: got %zu\n", size);
        return false;
    }

    const HealthResponsePayload* resp = (const HealthResponsePayload*)buffer;
    
    // Copy values directly from response structure
    health->status = resp->status;
    health->timestamp = resp->timestamp;
    health->db_latency = resp->db_latency;
    health->memory_used = resp->memory_used;
    health->memory_total = resp->memory_total;
    health->uptime_ms = resp->uptime;

    return validateHealthValues(health);
}

// Add helper function to validate values
bool validateHealthValues(const DatabaseHealth* health) {
    // Add reasonable limits
    const uint32_t MAX_LATENCY_MS = 60000;  // 60 seconds
    const uint64_t MAX_MEMORY = 1024ULL * 1024ULL * 1024ULL * 1024ULL;  // 1 TB
    
    if (health->db_latency > MAX_LATENCY_MS ||
        health->memory_used > MAX_MEMORY ||
        health->memory_total > MAX_MEMORY ||
        health->memory_used > health->memory_total) {
        return false;
    }
    return true;
}

// Update the sendCompleteMessage function signature to include client
static bool sendCompleteMessage(DatabaseClient* client, uint8_t type, const void* payload, size_t payload_len) {
    MessageHeader header = {
        .type = type,
        .version = MESSAGE_VERSION,
        .sequence = client->sequence++,  // Use client's sequence
        .length = payload_len
    };

    // Allocate single buffer for complete message
    uint8_t* packet = malloc(sizeof(header) + payload_len);
    if (!packet) {
        fprintf(stderr, "[DB] Failed to allocate message buffer\n");
        return false;
    }

    // Copy header and payload into single buffer
    memcpy(packet, &header, sizeof(header));
    if (payload && payload_len > 0) {
        memcpy(packet + sizeof(header), payload, payload_len);
    }

    // Send complete message
    bool success = (send(client->sock, packet, sizeof(header) + payload_len, MSG_NOSIGNAL) == 
                   sizeof(header) + payload_len);
    
    if (!success) {
        fprintf(stderr, "[DB] Failed to send message: %s\n", strerror(errno));
    }

    free(packet);
    return success;
}

bool verifyUserToken(DatabaseClient* client, const char* token, TokenVerifyResult* result) {
    // First ensure we have an active connection
    if (client->state != CONN_STATE_CONNECTED) {
        if (!initializeHealthCheck(client)) {
            fprintf(stderr, "[DB] Cannot verify token - failed to establish connection\n");
            return false;
        }
    }

    size_t token_len = strlen(token);
    size_t total_message_size = sizeof(MessageHeader) + token_len;

    fprintf(stderr, "[DB] Preparing token verify message (total %zu bytes):\n", total_message_size);
    fprintf(stderr, "[DB] - Header: %zu bytes\n", sizeof(MessageHeader));
    fprintf(stderr, "[DB] - Token payload: %zu bytes\n", token_len);

    // Send verify token message as single packet
    if (!sendCompleteMessage(client, MSG_VERIFY_TOKEN, token, token_len)) {
        fprintf(stderr, "[DB] Failed to send verify token message (%zu bytes)\n", total_message_size);
        return false;
    }

    fprintf(stderr, "[DB] Sent token verification request - %zu bytes sent\n", total_message_size);

    // Read response with timeout
    uint8_t buffer[1024];
    ssize_t bytes = recv(client->sock, buffer, sizeof(buffer), 0);
    if (bytes < sizeof(MessageHeader)) {
        fprintf(stderr, "[DB] Invalid verify response\n"); 
        return false;
    }

    MessageHeader* resp = (MessageHeader*)buffer;
    if (resp->type != MSG_VERIFY_TOKEN) {
        fprintf(stderr, "[DB] Unexpected response type 0x%02x\n", resp->type);
        return false;
    }

    // Parse verification response:
    // [Header][success byte][player_id or error message]
    if (resp->length < 1) {
        fprintf(stderr, "[DB] Invalid verify response length\n");
        return false;
    }

    uint8_t success = buffer[sizeof(MessageHeader)];
    result->success = success;

    if (success) {
        // Copy player ID from response
        if (resp->length != 5) {  // 1 byte success + 4 bytes player_id
            fprintf(stderr, "[DB] Invalid success response length: %d\n", resp->length);
            return false;
        }
        memcpy(&result->data.player_id, buffer + sizeof(MessageHeader) + 1, sizeof(uint32_t));
        fprintf(stderr, "[DB] Token verified for player %u\n", result->data.player_id);
    } else {
        // Copy error message
        size_t error_len = resp->length - 1;  // -1 for success byte
        if (error_len >= sizeof(result->data.error)) {
            error_len = sizeof(result->data.error) - 1;
        }
        memcpy(result->data.error, buffer + sizeof(MessageHeader) + 1, error_len);
        result->data.error[error_len] = '\0';
        fprintf(stderr, "[DB] Token verification failed: %s\n", result->data.error);
    }

    return true;
}

void cleanupDatabaseClient(DatabaseClient* client) {
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client->host);
    free(client->server_id);     // Free server ID
    free(client->server_token);  // Free server token
}

static void onWebSocketMessage(void* user, const uint8_t* data, size_t len) {
    DatabaseClient* client = (DatabaseClient*)user;
    
    if (len >= 42 && data[0] == MSG_HEALTH_CHECK) {
        if (parseHealthResponse(data, len, &client->last_health)) {
            client->last_health_check = time(NULL);
        }
    }
}

bool initDatabaseClient(DatabaseClient* client, const char* host, int port,
                       const char* server_id, const char* server_token,
                       ConnectionType type) {
    client->host = strdup(host);
    client->port = port;
    client->server_id = strdup(server_id);
    client->server_token = strdup(server_token);
    
    client->curl = curl_easy_init();
    if (!client->curl) {
        return false;
    }
    
    client->conn_type = type;
    
    if (type == CONN_TYPE_WEBSOCKET) {
        // Initialize WebSocket
        client->ws.host = client->host;
        client->ws.port = client->port;
        client->ws.path = "/ws/health";
        client->ws.auth_id = client->server_id;
        client->ws.auth_token = client->server_token;
        ws_set_message_handler(&client->ws, onWebSocketMessage);
    }
    
    client->sequence = 0;  // Initialize sequence number
    client->connected = false;
    
    client->state = CONN_STATE_DISCONNECTED;
    client->auth_complete = false;
    client->auth_success = false;  // Add this explicit init

    return true;
}

static void sendWebSocketHealthCheck(DatabaseClient* client) {
    if (client->ws.connected) {
        if (!ws_send_health_check(&client->ws)) {
            fprintf(stderr, "[DB] Failed to send WebSocket health check\n");
        }
    }
}

static bool sendAuthRequest(DatabaseClient* client) {
    // Prepare fixed-size auth payload
    AuthPayload payload = {0};  // Initialize to zeros for null padding
    
    // Copy credentials with length checks
    size_t id_len = strlen(client->server_id);
    size_t token_len = strlen(client->server_token);
    
    if (id_len > sizeof(payload.server_id) || token_len > sizeof(payload.token)) {
        fprintf(stderr, "[DB] Credentials too long: id=%zu, token=%zu\n", 
                id_len, token_len);
        return false;
    }
    
    // Copy raw server_id and token - NOT hashed
    memcpy(payload.server_id, client->server_id, id_len);
    memcpy(payload.token, client->server_token, token_len);
    
    // Prepare header
    MessageHeader header = {
        .type = MSG_AUTH_REQUEST,
        .version = 1,
        .sequence = client->sequence++,
        .length = sizeof(AuthPayload)
    };
    
    fprintf(stderr, "[DB] Sending auth request: type=0x%02x, version=%d, seq=%d, len=%d\n",
            header.type, header.version, header.sequence, header.length);
    
    // Send header
    if (send(client->sock, &header, sizeof(header), MSG_NOSIGNAL) != sizeof(header)) {
        fprintf(stderr, "[DB] Failed to send auth header: %s\n", strerror(errno));
        return false;
    }
    
    // Send payload
    if (send(client->sock, &payload, sizeof(payload), MSG_NOSIGNAL) != sizeof(payload)) {
        fprintf(stderr, "[DB] Failed to send auth payload: %s\n", strerror(errno));
        return false;
    }
    
    fprintf(stderr, "[DB] Auth request sent successfully\n");
    return true;
}

static bool readMessage(DatabaseClient* client, uint8_t* type, void* payload, uint32_t* length) {
    MessageHeader header;
    
    if (recv(client->sock, &header, sizeof(header), 0) != sizeof(header)) {
        return false;
    }

    if (header.version != 1) {
        return false;
    }

    *type = header.type;
    *length = header.length;

    if (payload && header.length > 0) {
        if (recv(client->sock, payload, header.length, 0) != header.length) {
            return false;
        }
    }

    return true;
}

static bool readMultiPartMessage(DatabaseClient* client, uint8_t* buffer, size_t* total_size) {
    MultiPartHeader mp_header;
    size_t bytes_read = 0;
    
    while (bytes_read < *total_size) {
        // Read header including part info
        if (recv(client->sock, &mp_header, sizeof(mp_header), 0) != sizeof(mp_header)) {
            return false;
        }

        if (mp_header.header.length > *total_size - bytes_read) {
            return false;
        }

        // Read payload for this part
        if (recv(client->sock, buffer + bytes_read, mp_header.header.length, 0) != mp_header.header.length) {
            return false;
        }

        bytes_read += mp_header.header.length;

        // Check if this was the last part
        if (mp_header.header.type & MSG_FLAG_LAST_PART) {
            break;
        }
    }

    *total_size = bytes_read;
    return true;
}

static bool sendMessage(DatabaseClient* client, uint8_t type, const void* payload, uint32_t length) {
    MessageHeader header = {
        .type = type,
        .version = 1,
        .sequence = client->sequence++,
        .length = length
    };
    
    // Send header
    if (send(client->sock, &header, sizeof(header), MSG_NOSIGNAL) != sizeof(header)) {
        return false;
    }

    // Send payload if present
    if (payload && length > 0) {
        if (send(client->sock, payload, length, MSG_NOSIGNAL) != length) {
            return false;
        }
    }

    return true;
}

static bool skipMessagePayload(int sock, size_t length) {
    uint8_t skip_buffer[1024];
    size_t remaining = length;
    
    while (remaining > 0) {
        size_t to_read = remaining > sizeof(skip_buffer) ? sizeof(skip_buffer) : remaining;
        ssize_t bytes = recv(sock, skip_buffer, to_read, 0);
        if (bytes <= 0) {
            fprintf(stderr, "[DB] Failed to skip payload: %s\n", strerror(errno));
            return false;
        }
        remaining -= bytes;
    }
    return true;
}
