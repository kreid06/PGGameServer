#include "db_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>    
#include <unistd.h>  
#include <errno.h>    
#include <fcntl.h>
#include <pthread.h>  // Add pthread header

//-----------------------------------------------------------------------------
// System-specific Definitions
//-----------------------------------------------------------------------------

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
#define TCP_KEEPINTVL  10    /* Interval between keepalives */
#endif 

#define MAX_MISSED_PONGS 3
#define RECONNECT_MAX_ATTEMPTS 5
#define RECONNECT_BACKOFF_MS 1000

// Add handshake timeout constant
#define HANDSHAKE_TIMEOUT_SEC 5

//-----------------------------------------------------------------------------
// Network Operations Implementation
//-----------------------------------------------------------------------------

bool network_connect(NetworkConnection* net, const char* host, int port) {
    if (!net || !host) {
        fprintf(stderr, "network_connect: Invalid parameters\n");
        return false;
    }

    // Validate host and port
    if (!host[0] || port <= 0) {
        fprintf(stderr, "Invalid host or port: host='%s', port=%d\n", 
                host ? host : "NULL", port);
        return false;
    }

    // Debug input parameters
    fprintf(stderr, "Connecting with parameters: host='%s', port=%d\n", host, port);

    // Create socket first
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Socket creation failed: %s\n", strerror(errno));
        return false;
    }

    // Make a copy of the host string
    char* new_host = strdup(host);
    if (!new_host) {
        fprintf(stderr, "Failed to copy host string\n");
        close(sock);
        return false;
    }

    // Clean up existing socket
    if (net->sock >= 0) {
        close(net->sock);
    }

    // Update network state
    net->sock = sock;
    net->port = port;
    net->connected = false;

    // Update host string safely
    if (net->host) {
        free(net->host);
    }
    net->host = new_host;

    // Set up server address
    memset(&net->addr, 0, sizeof(net->addr));
    net->addr.sin_family = AF_INET;
    net->addr.sin_port = htons(port);

    // Debug current state
    fprintf(stderr, "Network state before connect: sock=%d, host='%s', port=%d\n",
            net->sock, net->host, net->port);

    // Convert address with error checking
    if (inet_pton(AF_INET, net->host, &net->addr.sin_addr) <= 0) {
        fprintf(stderr, "Address resolution failed for '%s': %s\n", 
                net->host, strerror(errno));
        close(net->sock);
        net->sock = -1;
        return false;
    }

    // Set socket to blocking for connect
    int flags = fcntl(net->sock, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(net->sock, F_SETFL, flags);

    // Connect with timeout
    fprintf(stderr, "Attempting connection to %s:%d\n", net->host, net->port);
    
    if (connect(net->sock, (struct sockaddr*)&net->addr, sizeof(net->addr)) < 0) {
        fprintf(stderr, "Connection failed: %s\n", strerror(errno));
        close(net->sock);
        net->sock = -1;
        return false;
    }

    // Set non-blocking for normal operation
    flags = fcntl(net->sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(net->sock, F_SETFL, flags);

    net->connected = true;
    fprintf(stderr, "Successfully connected to %s:%d\n", net->host, net->port);
    return true;
}

void network_disconnect(NetworkConnection* net) {
    if (!net) return;
    
    if (net->sock >= 0) {
        close(net->sock);
        net->sock = -1;
    }
    
    // Don't free the host string anymore - we keep it for reconnection
    net->connected = false;
}

// Modify init_socket to use NetworkConnection
bool init_socket(DatabaseClient* client) {
    if (!client) return false;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Socket creation failed: %s\n", strerror(errno));
        return false;
    }
    client->net.sock = sock;

    // Set socket to blocking mode for initial connection
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) {
        flags &= ~O_NONBLOCK;  // Clear non-blocking flag
        fcntl(sock, F_SETFL, flags);
    }

    fprintf(stderr, "Socket created (fd=%d) for %s:%d\n", 
            sock, client->net.host, client->net.port);
    return true;
}

//-----------------------------------------------------------------------------
// Client Lifecycle Implementation
//-----------------------------------------------------------------------------

bool db_client_init(DatabaseClient* client, const char* host, int port,
                    const char* server_id, const char* server_token) {
    if (!client || !host || !server_id || !server_token) {
        fprintf(stderr, "Invalid parameters for db_client_init\n");
        return false;
    }

    // Validate server_id and token lengths
    size_t id_len = strlen(server_id);
    size_t token_len = strlen(server_token);

    if (id_len >= MAX_SERVER_ID_LENGTH) {
        fprintf(stderr, "Server ID too long (max %d bytes)\n", MAX_SERVER_ID_LENGTH);
        return false;
    }

    if (token_len >= MAX_SERVER_TOKEN_LENGTH) {
        fprintf(stderr, "Server token too long (max %d bytes)\n", MAX_SERVER_TOKEN_LENGTH);
        return false;
    }

    // Clear all fields first
    memset(client, 0, sizeof(DatabaseClient));

    // Initialize credentials first
    client->server_id = strdup(server_id);
    client->server_token = strdup(server_token);
    
    if (!client->server_id || !client->server_token) {
        fprintf(stderr, "Failed to allocate credential strings\n");
        db_client_cleanup(client);
        return false;
    }

    // Initialize network state
    client->net.host = strdup(host);
    client->net.port = port;
    
    if (!client->net.host) {
        fprintf(stderr, "Failed to allocate host string\n");
        db_client_cleanup(client);
        return false;
    }

    // Initialize connection state
    client->state = CONN_STATE_DISCONNECTED;
    client->auth_complete = false;
    client->auth_success = false;
    client->sequence = 0;
    
    // Initialize time tracking
    time_t current_time = time(NULL);
    client->last_keepalive = current_time;
    client->reconnect_attempts = 0;
    client->is_reconnecting = false;

    // Initialize connection metrics
    client->metrics = (ConnectionQualityMetrics){
        .last_successful_connect = 0,
        .failed_attempts_count = 0,
        .connection_uptime = 0,
        .connection_stable = false
    };

    // Now try to connect and authenticate
    if (!network_connect(&client->net, host, port)) {
        fprintf(stderr, "Initial connection failed\n");
        db_client_cleanup(client);
        return false;
    }

    client->state = CONN_STATE_CONNECTED;
    
    // Attempt authentication
    if (!db_client_authenticate(client)) {
        fprintf(stderr, "Initial authentication failed\n");
        db_client_cleanup(client);
        return false;
    }

    // Wait for auth response
    if (!db_client_wait_for_auth(client, 5)) {
        fprintf(stderr, "Initial authentication timeout\n");
        db_client_cleanup(client);
        return false;
    }

    if (!client->auth_success) {
        fprintf(stderr, "Initial authentication rejected\n");
        db_client_cleanup(client);
        return false;
    }

    fprintf(stderr, "Connection and authentication successful\n");
    return true;
}

void db_client_cleanup(DatabaseClient* client) {
    if (!client) return;
    
    network_disconnect(&client->net);
    curl_easy_cleanup(client->curl);
    curl_global_cleanup();
    free(client->server_id);
    free(client->server_token);
    free(client->net.host);  // Now free it only during final cleanup
    client->net.host = NULL;
}

//-----------------------------------------------------------------------------
// Connection Operations Implementation
//-----------------------------------------------------------------------------

// Add connection state verification structure
typedef struct {
    bool tcp_connected;
    bool handshake_complete;
    bool auth_complete;
} ConnectionVerifyState;

bool verify_connection(DatabaseClient* client) {
    if (!client || client->net.sock < 0) return false;
    
    // Process any pending messages first
    if (!db_client_process_messages(client)) {
        return false;
    }

    // Trust authentication state - no handshake needed
    if (client->state == CONN_STATE_CONNECTED && client->auth_success) {
        return true;
    }

    // Quick TCP check
    char probe = 0;
    if (send(client->net.sock, &probe, 0, MSG_NOSIGNAL) < 0) {
        if (errno == EPIPE || errno == ECONNRESET) {
            return false;
        }
    }

    // If not authenticated, need to re-authenticate
    return client->auth_success;
}

bool db_client_connect(DatabaseClient* client) {
    if (!client) return false;
    
    fprintf(stderr, "Starting connection and authentication process to %s:%d\n", 
            client->net.host, client->net.port);

    // Clear any existing connection
    network_disconnect(&client->net);
    
    // Attempt new connection
    if (!network_connect(&client->net, client->net.host, client->net.port)) {
        fprintf(stderr, "Failed to establish connection\n");
        return false;
    }

    client->state = CONN_STATE_CONNECTED;
    fprintf(stderr, "TCP connection established, proceeding with authentication\n");

    // Send auth request
    if (!db_client_authenticate(client)) {
        fprintf(stderr, "Failed to send authentication request\n");
        network_disconnect(&client->net);
        client->state = CONN_STATE_DISCONNECTED;
        return false;
    }

    // Wait for auth response
    if (!db_client_wait_for_auth(client, 5)) {
        fprintf(stderr, "Authentication timeout\n");
        network_disconnect(&client->net);
        client->state = CONN_STATE_DISCONNECTED;
        return false;
    }

    if (!client->auth_success) {
        fprintf(stderr, "Authentication rejected\n");
        network_disconnect(&client->net);
        client->state = CONN_STATE_DISCONNECTED;
        return false;
    }

    fprintf(stderr, "Connection fully established and authenticated\n");
    return true;
}

bool db_client_authenticate(DatabaseClient* client) {
    if (!client || client->state != CONN_STATE_CONNECTED) {
        fprintf(stderr, "Cannot authenticate: client not connected\n");
        return false;
    }

    // Debug print original token information
    fprintf(stderr, "Original token length: %zu\n", strlen(client->server_token));
    fprintf(stderr, "Original token: '%s'\n", client->server_token);

    // Prepare auth request
    MessageHeader header = {0};
    header.type = MSG_AUTH_REQUEST;
    header.version = MESSAGE_VERSION;
    header.sequence = client->sequence++;
    header.length = sizeof(AuthRequestPayload);  // Set the correct payload size

    // Prepare payload with explicit zeroing
    AuthRequestPayload payload;
    memset(&payload, 0, sizeof(AuthRequestPayload));
    
    // Copy data with bounds checking
    if (strlen(client->server_id) >= sizeof(payload.server_id)) {
        fprintf(stderr, "Server ID too long: %zu bytes\n", strlen(client->server_id));
        return false;
    }
    if (strlen(client->server_token) >= sizeof(payload.auth_token)) {
        fprintf(stderr, "Auth token too long: %zu bytes\n", strlen(client->server_token));
        return false;
    }

    // Copy strings with explicit lengths
    strncpy(payload.server_id, client->server_id, sizeof(payload.server_id) - 1);
    strncpy(payload.auth_token, client->server_token, sizeof(payload.auth_token) - 1);
    
    // Ensure null termination
    payload.server_id[sizeof(payload.server_id) - 1] = '\0';
    payload.auth_token[sizeof(payload.auth_token) - 1] = '\0';

    // Verify payload contents before sending
    fprintf(stderr, "Payload verification:\n");
    fprintf(stderr, "Server ID: '%s' (len: %zu)\n", payload.server_id, strlen(payload.server_id));
    fprintf(stderr, "Auth token: '%s' (len: %zu)\n", payload.auth_token, strlen(payload.auth_token));
    fprintf(stderr, "Total payload size: %zu bytes\n", sizeof(AuthRequestPayload));

    // Send header
    ssize_t header_sent = send(client->net.sock, &header, sizeof(header), 0);
    if (header_sent < 0) {
        fprintf(stderr, "Failed to send auth header: %s\n", strerror(errno));
        return false;
    }
    fprintf(stderr, "Sent header: %zd bytes\n", header_sent);

    // Send payload
    ssize_t payload_sent = send(client->net.sock, &payload, sizeof(payload), 0);
    if (payload_sent < 0) {
        fprintf(stderr, "Failed to send auth payload: %s\n", strerror(errno));
        return false;
    }
    fprintf(stderr, "Sent payload: %zd bytes\n", payload_sent);

    client->state = CONN_STATE_AUTHENTICATING;
    return true;
}

bool db_client_wait_for_auth(DatabaseClient* client, int timeout_seconds) {
    if (!client || client->state != CONN_STATE_AUTHENTICATING) {
        return false;
    }

    fd_set read_fds;
    struct timeval timeout;
    time_t start_time = time(NULL);

    while (time(NULL) - start_time < timeout_seconds) {
        FD_ZERO(&read_fds);
        FD_SET(client->net.sock, &read_fds);
        
        timeout.tv_sec = 1;  // Check every second
        timeout.tv_usec = 0;

        int result = select(client->net.sock + 1, &read_fds, NULL, NULL, &timeout);
        if (result > 0) {
            AuthResponseMessage response;
            ssize_t received = recv(client->net.sock, &response, sizeof(response), 0);
            
            if (received > 0) {
                if (response.header.type == MSG_AUTH_RESPONSE) {
                    client->auth_complete = true;
                    client->auth_success = response.success;
                    
                    if (response.success) {
                        client->state = CONN_STATE_CONNECTED;
                        return true;
                    } else {
                        fprintf(stderr, "Authentication failed: %s\n", response.error);
                        client->state = CONN_STATE_DISCONNECTED;
                        return false;
                    }
                }
            }
        } else if (result < 0) {
            fprintf(stderr, "Error waiting for auth response: %s\n", strerror(errno));
            return false;
        }
    }

    fprintf(stderr, "Authentication timed out\n");
    return false;
}

//-----------------------------------------------------------------------------
// Health & Monitoring Implementation
//-----------------------------------------------------------------------------

bool db_client_process_pong(DatabaseClient* client, const MessageHeader* response) {
    if (!client || !response) return false;
    
    if (response->type != MSG_PONG) return false;
    
    // Check if this is the pong we're waiting for
    if (client->ping_state.expecting_pong && 
        response->sequence == client->ping_state.last_sequence) {
        
        client->ping_state.last_pong = time(NULL);
        client->ping_state.last_successful = time(NULL);
        client->ping_state.missed_pongs = 0;
        client->ping_state.expecting_pong = false;
        
        return true;
    }
    
    return false;
}

ssize_t db_client_send(DatabaseClient* client, const void* data, size_t len) {
    ssize_t result = send(client->net.sock, data, len, 0);
    if (result < 0 && (errno == ECONNRESET || errno == EPIPE)) {
        // Connection lost
        db_client_handle_disconnect(client);
        return -1;
    }
    return result;
}

bool db_client_ping(DatabaseClient* client) {
    if (!client) return false;

    // Only ping if fully connected and authenticated
    if (!client->net.connected || 
        client->state != CONN_STATE_CONNECTED || 
        !client->auth_success || 
        client->is_reconnecting) {
        return false;
    }

    // Quick socket check before sending
    char probe = 0;
    if (send(client->net.sock, &probe, 0, MSG_NOSIGNAL) < 0) {
        if (errno == EPIPE || errno == ECONNRESET) {
            fprintf(stderr, "Socket closed during ping check\n");
            return db_client_handle_disconnect(client);
        }
    }

    MessageHeader header = {0};
    header.type = MSG_PING;
    header.version = MESSAGE_VERSION;
    header.sequence = client->sequence++;
    
    client->ping_state.last_sequence = header.sequence;
    client->ping_state.timestamp = time(NULL);
    client->ping_state.expecting_pong = true;
    
    fprintf(stderr, "Sending ping message (seq: %u)\n", header.sequence);
    
    ssize_t sent = db_client_send(client, &header, sizeof(header));
    if (sent < 0) {
        fprintf(stderr, "Failed to send ping: %s\n", strerror(errno));
        return db_client_handle_disconnect(client);
    }
    
    fprintf(stderr, "Ping sent successfully, waiting for pong\n");

    // Process any pending responses immediately
    return db_client_process_messages(client);
}

// Add new message processing function
bool db_client_process_messages(DatabaseClient* client) {
    if (!client || client->net.sock < 0) return false;

    uint8_t buffer[1024];
    ssize_t received;

    // Non-blocking read of any pending messages
    while ((received = recv(client->net.sock, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0) {
        if (received < sizeof(MessageHeader)) {
            fprintf(stderr, "Received incomplete message header\n");
            continue;
        }

        MessageHeader* header = (MessageHeader*)buffer;
        fprintf(stderr, "Received message type: %d, seq: %u\n", header->type, header->sequence);
        
        switch (header->type) {
            case MSG_PONG:
                fprintf(stderr, "Processing pong message (seq: %u)\n", header->sequence);
                if (db_client_process_pong(client, header)) {
                    // Valid pong received, reset failure counters
                    client->ping_state.missed_pongs = 0;
                    client->metrics.failed_attempts_count = 0;
                    client->ping_state.last_successful = time(NULL);
                    client->ping_state.expecting_pong = false;
                    fprintf(stderr, "Processed pong message successfully\n");
                }
                break;

            case MSG_SERVER_INFO:
                // Process server info messages
                if (received >= sizeof(MessageHeader) + sizeof(ServerInfoPayload)) {
                    memcpy(&client->server_info, buffer + sizeof(MessageHeader), 
                           sizeof(ServerInfoPayload));
                }
                break;

            case MSG_HEALTH_RESPONSE:
                // Process health check responses
                if (received >= sizeof(MessageHeader) + sizeof(DatabaseHealth)) {
                    memcpy(&client->last_health, buffer + sizeof(MessageHeader),
                           sizeof(DatabaseHealth));
                }
                break;

            default:
                fprintf(stderr, "Received unknown message type: %d\n", header->type);
                break;
        }
    }

    if (received == 0) {
        fprintf(stderr, "Server closed connection\n");
        return db_client_handle_disconnect(client);
    } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        fprintf(stderr, "Error reading messages: %s\n", strerror(errno));
        return db_client_handle_disconnect(client);
    }

    // Check for missed pongs
    if (client->ping_state.expecting_pong) {
        time_t now = time(NULL);
        if (now - client->ping_state.timestamp > PING_TIMEOUT_MS/1000) {
            client->ping_state.missed_pongs++;
            fprintf(stderr, "Missed pong (count: %d)\n", client->ping_state.missed_pongs);
            
            if (client->ping_state.missed_pongs >= MAX_MISSED_PONGS) {
                fprintf(stderr, "Too many missed pongs, connection lost\n");
                return db_client_handle_disconnect(client);
            }
        }
    }

    // Process timeouts even if no messages received
    time_t now = time(NULL);
    
    // Check for ping timeout
    if (client->ping_state.expecting_pong && 
        (now - client->ping_state.timestamp > PING_TIMEOUT_MS/1000)) {
        client->ping_state.missed_pongs++;
        fprintf(stderr, "Ping timeout - missed pongs: %d\n", client->ping_state.missed_pongs);
        
        if (client->ping_state.missed_pongs >= MAX_MISSED_PONGS) {
            fprintf(stderr, "Too many missed pongs, connection lost\n");
            return db_client_handle_disconnect(client);
        }
    }

    // Send periodic ping if needed
    if (!client->ping_state.expecting_pong && 
        (now - client->ping_state.last_successful > PING_RETRY_INTERVAL_MS/1000)) {
        fprintf(stderr, "Sending periodic ping\n");
        return db_client_ping(client);
    }

    return true;
}

ConnectionState db_client_get_state(const DatabaseClient* client) {
    if (!client) return CONN_STATE_DISCONNECTED;
    return client->state;
}

//-----------------------------------------------------------------------------
// Retry Mechanism Implementation
//-----------------------------------------------------------------------------

void db_client_sleep_with_backoff(int attempt) {
    // Calculate delay with exponential backoff
    int delay_ms = DB_RETRY_DELAY_MS * (1 << (attempt-1)); // 2^(attempt-1) * base_delay
    usleep(delay_ms * 1000); // Convert to microseconds
}

bool db_client_retry_operation(DatabaseClient* client, bool (*operation)(DatabaseClient*), const char* op_name) {
    for (int attempt = 1; attempt <= DB_MAX_RETRIES; attempt++) {
        if (operation(client)) {
            if (attempt > 1) {
                fprintf(stderr, "%s succeeded on attempt %d\n", op_name, attempt);
            }
            return true;
        }

        if (attempt < DB_MAX_RETRIES) {
            fprintf(stderr, "%s failed, attempt %d/%d - retrying...\n", 
                    op_name, attempt, DB_MAX_RETRIES);
            db_client_sleep_with_backoff(attempt);
        } else {
            fprintf(stderr, "%s failed after %d attempts\n", op_name, DB_MAX_RETRIES);
        }
    }
    return false;
}

// Modify db_client_ensure_connected to use retries
bool db_client_ensure_connected(DatabaseClient* client) {
    if (!client || client->is_reconnecting) return false;

    // Trust authentication state if already connected
    if (client->state == CONN_STATE_CONNECTED && 
        client->auth_success && 
        client->net.connected) {
        return true;
    }

    // Only verify if not authenticated
    if (!client->auth_success) {
        if (!verify_connection(client)) {
            fprintf(stderr, "Connection needs re-authentication\n");
            return db_client_handle_disconnect(client);
        }
    }

    // Rest of connection logic
    // If we're already reconnecting, don't try again
    if (client->is_reconnecting) {
        return false;
    }

    // Check if connection is lost
    if (client->state == CONN_STATE_CONNECTED && !client->auth_success) {
        return db_client_handle_disconnect(client);
    }

    // Already connected and authenticated
    if (client->state == CONN_STATE_CONNECTED && client->auth_success) {
        return true;
    }

    // Try to connect if disconnected
    if (client->state == CONN_STATE_DISCONNECTED) {
        if (!db_client_retry_operation(client, db_client_connect, "Connection")) {
            return false;
        }
    }

    // Authenticate if needed
    if (client->state == CONN_STATE_CONNECTED && !client->auth_success) {
        if (!db_client_retry_operation(client, db_client_authenticate, "Authentication")) {
            return false;
        }
        
        // Wait for auth with retries
        bool auth_wait_success = false;
        for (int attempt = 1; attempt <= DB_MAX_RETRIES; attempt++) {
            if (db_client_wait_for_auth(client, 5)) {  // 5 second timeout
                auth_wait_success = true;
                break;
            }
            if (attempt < DB_MAX_RETRIES) {
                fprintf(stderr, "Auth wait failed, attempt %d/%d - retrying...\n", 
                        attempt, DB_MAX_RETRIES);
                db_client_sleep_with_backoff(attempt);
            }
        }
        
        if (!auth_wait_success) {
            fprintf(stderr, "Auth wait failed after %d attempts\n", DB_MAX_RETRIES);
            return false;
        }
    }

    return client->state == CONN_STATE_CONNECTED && client->auth_success;
}

//-----------------------------------------------------------------------------
// Reconnection Implementation
//-----------------------------------------------------------------------------

bool db_client_handle_disconnect(DatabaseClient* client) {
    if (!client || client->is_reconnecting) return false;

    fprintf(stderr, "Connection lost, initiating reconnect process...\n");
    
    // Set reconnecting flag first to prevent races
    client->is_reconnecting = true;

    // Disconnect but preserve host info
    if (client->net.sock >= 0) {
        close(client->net.sock);
        client->net.sock = -1;
    }
    client->net.connected = false;
    
    // Reset connection state
    client->state = CONN_STATE_DISCONNECTED;
    client->auth_success = false;
    client->auth_complete = false;
    client->ping_state.missed_pongs = 0;
    client->ping_state.expecting_pong = false;

    // Start background reconnection
    pthread_t reconnect_thread;
    if (pthread_create(&reconnect_thread, NULL, db_client_reconnect_thread, client) != 0) {
        fprintf(stderr, "Failed to create reconnect thread\n");
        client->is_reconnecting = false;
        return false;
    }
    pthread_detach(reconnect_thread);

    return true;
}

// Add new function for background reconnection
void* db_client_reconnect_thread(void* arg) {
    DatabaseClient* client = (DatabaseClient*)arg;
    
    // Only attempt reconnect if we're not in a rapid failure loop
    if (client->metrics.failed_attempts_count > MAX_RECONNECT_ATTEMPTS * 2) {
        time_t now = time(NULL);
        if (now - client->metrics.last_successful_connect < 300) { // 5 minutes
            fprintf(stderr, "Too many recent failures, waiting 5 minutes before retry\n");
            sleep(300);
        }
    }
    
    if (db_client_reconnect(client)) {
        fprintf(stderr, "Background reconnection successful\n");
    } else {
        fprintf(stderr, "Background reconnection failed\n");
    }
    
    client->is_reconnecting = false;
    return NULL;
}

bool db_client_reconnect(DatabaseClient* client) {
    if (!client || !client->is_reconnecting) return false;

    // Get host directly from environment
    const char* env_host = getEnvOrDefault("AUTH_SERVER_HOST", "localhost");
    int env_port = atoi(getEnvOrDefault("AUTH_SERVER_PORT", "3001"));

    if (!env_host || env_port <= 0) {
        fprintf(stderr, "Invalid environment configuration: host=%s, port=%d\n",
                env_host ? env_host : "NULL", env_port);
        client->is_reconnecting = false;
        return false;
    }

    fprintf(stderr, "Starting reconnection process using environment config: %s:%d\n", 
            env_host, env_port);
    
    double delay_ms = RECONNECT_INITIAL_DELAY_MS;
    time_t reconnect_start = time(NULL);

    for (int attempt = 0; attempt < MAX_RECONNECT_ATTEMPTS; attempt++) {
        fprintf(stderr, "Reconnection attempt %d/%d (delay: %.1fs)\n", 
                attempt + 1, MAX_RECONNECT_ATTEMPTS, delay_ms/1000.0);

        // Update metrics
        client->metrics.failed_attempts_count++;
        
        // Try connect + auth in one step
        if (db_client_connect(client)) {
            // Success - update metrics
            time_t now = time(NULL);
            client->metrics.last_successful_connect = now;
            client->metrics.failed_attempts_count = 0;
            client->is_reconnecting = false;
            
            fprintf(stderr, "Reconnection and authentication successful\n");
            return true;
        }

        // Calculate next delay with exponential backoff
        delay_ms *= RECONNECT_BACKOFF_MULTIPLIER;
        if (delay_ms > RECONNECT_MAX_DELAY_MS) {
            delay_ms = RECONNECT_MAX_DELAY_MS;
        }

        // Sleep with interruptible delay
        double sleep_start = GetTime();
        while (GetTime() - sleep_start < delay_ms/1000.0) {
            if (!client->is_reconnecting) {
                fprintf(stderr, "Reconnection cancelled\n");
                return false;
            }
            usleep(100000); // Sleep in 100ms chunks to stay responsive
        }
    }

    // Failed after all attempts
    client->is_reconnecting = false;
    fprintf(stderr, "Failed to reconnect after %d attempts\n", MAX_RECONNECT_ATTEMPTS);
    return false;
}