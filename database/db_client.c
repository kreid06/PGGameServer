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

// Add constants at the top with other defines
#define MAX_MISSED_PONGS 3
#define RECONNECT_MAX_ATTEMPTS 5
#define RECONNECT_BACKOFF_MS 1000

// Forward declare static functions
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata);
static bool parseHealthResponse(const uint8_t* buffer, size_t size, DatabaseHealth* health);
static bool initializeHealthCheck(DatabaseClient* client);
static bool sendAuthRequest(DatabaseClient* client);
static bool readMultiPartMessage(DatabaseClient* client, uint8_t* buffer, size_t* total_size);
static bool sendMessage(DatabaseClient* client, uint8_t type, const void* payload, uint32_t length);
static bool readMessage(DatabaseClient* client, uint8_t* type, void* payload, uint32_t* length);
static bool skipMessagePayload(int sock, size_t length);
static bool waitForVerifyResponse(DatabaseClient* client, TokenVerifyResult* result);

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

// Add ping/pong tracking
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

    // Always use port 3001 regardless of what was passed to init
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

static bool waitForVerifyResponse(DatabaseClient* client, TokenVerifyResult* result) {
    uint8_t buffer[1024];
    time_t start_time = time(NULL);
    const int MAX_WAIT_SECS = 5;

    // First try reading any queued messages
    int flags = fcntl(client->sock, F_GETFL, 0);
    fcntl(client->sock, F_SETFL, flags | O_NONBLOCK);

    // Clear any pending health responses first
    fprintf(stderr, "[DB] Clearing pending messages before verification...\n");
    while (time(NULL) - start_time < 1) {  // Quick clean for 1 second
        ssize_t bytes = recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (bytes <= 0) break;  // No more pending messages
        
        if (bytes >= sizeof(MessageHeader)) {
            MessageHeader* header = (MessageHeader*)buffer;
            fprintf(stderr, "[DB] Skipping message type 0x%02x len=%d\n", 
                    header->type, header->length);
            if (header->length > 0) {
                skipMessagePayload(client->sock, header->length);
            }
        }
    }

    // Now set blocking and wait for verify response
    fcntl(client->sock, F_SETFL, flags & ~O_NONBLOCK);
    fprintf(stderr, "[DB] Now waiting for verification response...\n");

    while (time(NULL) - start_time < MAX_WAIT_SECS) {
        ssize_t bytes = recv(client->sock, buffer, sizeof(buffer), 0);
        if (bytes < sizeof(MessageHeader)) {
            fprintf(stderr, "[DB] Received incomplete message: %zd bytes\n", bytes);
            continue;
        }

        MessageHeader* resp = (MessageHeader*)buffer;
        fprintf(stderr, "[DB] Received message type 0x%02x len=%d\n", 
                resp->type, resp->length);

        switch (resp->type) {
            case MSG_VERIFY_TOKEN:
                if (resp->length < 1) {
                    fprintf(stderr, "[DB] Invalid verify response: missing status byte\n");
                    return false;
                }

                uint8_t status = buffer[sizeof(MessageHeader)];
                result->success = (status == 1);
                fprintf(stderr, "[DB] Token verify status: %d\n", status);

                if (result->success && resp->length >= 5) {
                    memcpy(&result->data.player_id, 
                           buffer + sizeof(MessageHeader) + 1, 
                           sizeof(uint32_t));
                    fprintf(stderr, "[DB] Token verified for player %u\n", 
                            result->data.player_id);
                    return true;
                } else if (!result->success) {
                    size_t error_len = resp->length - 1;
                    if (error_len > 0) {
                        if (error_len >= sizeof(result->data.error)) {
                            error_len = sizeof(result->data.error) - 1;
                        }
                        memcpy(result->data.error, 
                               buffer + sizeof(MessageHeader) + 1, 
                               error_len);
                        result->data.error[error_len] = '\0';
                        fprintf(stderr, "[DB] Token verification failed: %s\n", 
                                result->data.error);
                    } else {
                        strcpy(result->data.error, "Unknown verification error");
                        fprintf(stderr, "[DB] Token verification failed with no error message\n");
                    }
                    return false;
                }
                break;

            case MSG_HEALTH_RESPONSE:
            case MSG_SERVER_INFO:
                fprintf(stderr, "[DB] Skipping non-verify message type 0x%02x\n", 
                        resp->type);
                if (resp->length > 0) {
                    skipMessagePayload(client->sock, resp->length);
                }
                continue;

            default:
                fprintf(stderr, "[DB] Unexpected message type 0x%02x\n", resp->type);
                if (resp->length > 0) {
                    skipMessagePayload(client->sock, resp->length);
                }
                continue;
        }
    }

    fprintf(stderr, "[DB] Token verification timed out\n");
    strcpy(result->data.error, "Verification timeout");
    return false;
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

    // Wait for response with timeout
    return waitForVerifyResponse(client, result);
}

void cleanupDatabaseClient(DatabaseClient* client) {
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client->host);
    free(client->server_id);     // Free server ID
    free(client->server_token);  // Free server token
}

// Update init function implementation to match new signature
bool initDatabaseClient(DatabaseClient* client, const char* host, int port,
                       const char* server_id, const char* server_token) {
    client->host = strdup(host);
    client->port = port;
    client->server_id = strdup(server_id);
    client->server_token = strdup(server_token);
    
    client->curl = curl_easy_init();
    if (!client->curl) {
        return false;
    }
    
    client->sequence = 0;
    client->connected = false;
    client->state = CONN_STATE_DISCONNECTED;
    client->auth_complete = false;
    client->auth_success = false;

    client->missed_pongs = 0;
    client->last_successful_ping = 0;
    client->reconnect_attempts = 0;
    client->is_reconnecting = false;

    return true;
}

static bool sendAuthRequest(DatabaseClient* client) {
    AuthRequestPayload payload = {0};
    
    size_t id_len = strlen(client->server_id);
    size_t token_len = strlen(client->server_token);
    
    if (id_len > sizeof(payload.server_id) || token_len > sizeof(payload.auth_token)) {
        fprintf(stderr, "[DB] Credentials too long: id=%zu, token=%zu\n", 
                id_len, token_len);
        return false;
    }
    
    memcpy(payload.server_id, client->server_id, id_len);
    memcpy(payload.auth_token, client->server_token, token_len); // Changed from token to auth_token
    
    // Add size verification logging
    fprintf(stderr, "[DB] Auth request breakdown:\n");
    fprintf(stderr, "  - Header size: %zu bytes\n", sizeof(MessageHeader));
    fprintf(stderr, "  - Payload size: %zu bytes\n", sizeof(AuthRequestPayload));
    fprintf(stderr, "  - Total size: %zu bytes\n", sizeof(MessageHeader) + sizeof(AuthRequestPayload));
    
    // Prepare header
    MessageHeader header = {
        .type = MSG_AUTH_REQUEST,
        .version = 1,
        .sequence = client->sequence++,
        .length = sizeof(AuthRequestPayload)  // Should be 288
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

// Add ping implementation
bool db_client_ping(DatabaseClient* client) {
    time_t now = time(NULL);

    // Check if we need to reconnect
    if (client->missed_pongs >= MAX_MISSED_PONGS && !client->is_reconnecting) {
        fprintf(stderr, "[DB] Too many missed pongs (%d), initiating reconnection\n", 
                client->missed_pongs);
        client->is_reconnecting = true;
        client->state = CONN_STATE_DISCONNECTED;
        close(client->sock);
        client->sock = -1;
    }

    // Handle reconnection
    if (client->is_reconnecting) {
        if (client->reconnect_attempts >= RECONNECT_MAX_ATTEMPTS) {
            fprintf(stderr, "[DB] Max reconnection attempts reached\n");
            return false;
        }

        // Exponential backoff
        usleep(RECONNECT_BACKOFF_MS * (1 << client->reconnect_attempts));
        client->reconnect_attempts++;

        if (db_client_connect(client)) {
            fprintf(stderr, "[DB] Reconnection successful on attempt %d\n", 
                    client->reconnect_attempts);
            client->is_reconnecting = false;
            client->missed_pongs = 0;
            client->reconnect_attempts = 0;
            client->last_successful_ping = now;
            return true;
        }
        return false;
    }

    // Normal ping logic
    if (client->state != CONN_STATE_CONNECTED) {
        if (!db_client_connect(client)) {
            return false;
        }
    }

    MessageHeader header = {
        .type = MSG_PING,
        .version = MESSAGE_VERSION,
        .sequence = client->sequence++,
        .length = 0
    };

    if (send(client->sock, &header, sizeof(header), MSG_NOSIGNAL) != sizeof(header)) {
        client->missed_pongs++;
        fprintf(stderr, "[DB] Failed to send ping (missed pongs: %d)\n", 
                client->missed_pongs);
        return false;
    }

    // Check for pong response
    uint8_t buffer[sizeof(MessageHeader)];
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0}; // 1 second timeout
    setsockopt(client->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t bytes = recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (bytes == sizeof(MessageHeader)) {
        MessageHeader* response = (MessageHeader*)buffer;
        if (response->type == MSG_PONG) {
            client->missed_pongs = 0;
            client->last_successful_ping = now;
            return true;
        }
    }

    client->missed_pongs++;
    return false;
}

// Add db_client_connect implementation
bool db_client_connect(DatabaseClient* client) {
    // Try to establish initial connection
    if (!initializeHealthCheck(client)) {
        return false;
    }

    // Wait for authentication to complete
    time_t start = time(NULL);
    while (time(NULL) - start < 5) {  // 5 second timeout
        if (client->state == CONN_STATE_CONNECTED) {
            // After auth, expect SERVER_INFO message
            uint8_t buffer[1024];
            uint8_t type;
            uint32_t length;
            
            if (readMessage(client, &type, buffer, &length)) {
                if (type == MSG_SERVER_INFO && length >= sizeof(ServerInfoPayload)) {
                    memcpy(&client->server_info, buffer, sizeof(ServerInfoPayload));
                    fprintf(stderr, "[DB] Got server info - version: %d, max_players: %d\n",
                            client->server_info.version,
                            client->server_info.max_players);
                    return true;
                }
            }
        }
        usleep(100000);  // 100ms wait between checks
    }

    fprintf(stderr, "[DB] Connection timeout waiting for server info\n");
    return false;
}

// Update function names to match declarations
bool db_client_init(DatabaseClient* client, const char* host, int port,
                   const char* server_id, const char* server_token) {
    return initDatabaseClient(client, host, port, server_id, server_token);
}

void db_client_cleanup(DatabaseClient* client) {
    cleanupDatabaseClient(client);
}

bool db_client_verify_token(DatabaseClient* client, const char* token, TokenVerifyResult* result) {
    return verifyUserToken(client, token, result);
}
