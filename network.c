#include "network.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <box2d/box2d.h>
#include <raylib.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <libwebsockets.h>

// ... existing includes ...

// Move WriteCallback implementation to the top of the file
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    char* response = (char*)userp;
    strncat(response, contents, realsize);
    return realsize;
}

// WebSocket protocol definition
static struct lws_protocols protocols[] = {
    {
        .name = "game-protocol",
        .callback = callback_game_protocol,
        .per_session_data_size = 0,
        .rx_buffer_size = 4096,
        .id = 0,
        .user = NULL,
        .tx_packet_size = 4096,
    },
    { NULL, NULL, 0, 0 } // terminator
};

// WebSocket callback implementation
int callback_game_protocol(struct lws* wsi, enum lws_callback_reasons reason,
                         void* user, void* in, size_t len)
{
    NetworkServer* server = (NetworkServer*)lws_context_user(lws_get_context(wsi));
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            // Handle new WebSocket connection
            if (server->pendingCount < MAX_CLIENTS) {
                PendingClient* pending = &server->pendingClients[server->pendingCount++];
                pending->wsi = wsi;
                pending->isWebSocket = true;
                pending->connectTime = time(NULL);
                
                // Send handshake
                HandshakeMessage handshake = {
                    .type = MSG_TYPE_HANDSHAKE,
                    .protocol = 1,
                    .maxPlayers = MAX_CLIENTS,
                    .numPlayers = server->playerCount
                };
                ws_send_binary(wsi, &handshake, sizeof(HandshakeMessage));
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            handleWebSocketMessage(server, wsi, in, len);
            break;

        case LWS_CALLBACK_CLOSED:
            removeWebSocketClient(server, wsi);
            break;
    }
    
    return 0;
}

void createPlayerBody(NetworkServer* server, int playerIndex, b2WorldId worldId) {
    // Create a circular physics body for the player
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = (b2Vec2){0.0f, 0.0f};  // Start at origin
    
    b2BodyId playerId = b2CreateBody(worldId, &bodyDef);
    
    // Create circular shape for player
    b2Circle circle = {
        .center = {0.0f, 0.0f},
        .radius = 1.0f  // 1 meter radius
    };
    
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 1.0f;
    shapeDef.friction = 0.3f;
    
    b2CreateCircleShape(playerId, &shapeDef, &circle);
    
    server->players[playerIndex].physicsBody = playerId;
}

bool checkAuthServerHealth(AuthServerHealth* health) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", AUTH_SERVER_HOST, AUTH_SERVER_PORT, AUTH_HEALTH_ENDPOINT);

    char response[2048] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

    CURLcode res = curl_easy_perform(curl);
    bool success = false;

    if (res == CURLE_OK) {
        json_object* json = json_tokener_parse(response);
        if (json) {
            // Parse status
            json_object* status;
            if (json_object_object_get_ex(json, "status", &status)) {
                health->isHealthy = (strcmp(json_object_get_string(status), "healthy") == 0);
            }

            // Parse timestamp
            json_object* timestamp;
            if (json_object_object_get_ex(json, "timestamp", &timestamp)) {
                health->timestamp = json_object_get_int64(timestamp);
            }

            // Parse database info
            json_object* database;
            if (json_object_object_get_ex(json, "database", &database)) {
                json_object* dbStatus;
                if (json_object_object_get_ex(database, "status", &dbStatus)) {
                    health->database.isConnected = (strcmp(json_object_get_string(dbStatus), "connected") == 0);
                }

                json_object* latency;
                if (json_object_object_get_ex(database, "latency_ms", &latency)) {
                    health->database.latencyMs = json_object_get_int(latency);
                }

                json_object* error;
                if (json_object_object_get_ex(database, "error", &error)) {
                    strncpy(health->database.error, json_object_get_string(error), sizeof(health->database.error) - 1);
                }
            }

            // Parse service info
            json_object* service;
            if (json_object_object_get_ex(json, "service", &service)) {
                json_object* name;
                if (json_object_object_get_ex(service, "name", &name)) {
                    strncpy(health->service.name, json_object_get_string(name), sizeof(health->service.name) - 1);
                }

                json_object* version;
                if (json_object_object_get_ex(service, "version", &version)) {
                    strncpy(health->service.version, json_object_get_string(version), sizeof(health->service.version) - 1);
                }

                json_object* uptime;
                if (json_object_object_get_ex(service, "uptime", &uptime)) {
                    health->service.uptime = json_object_get_int64(uptime);
                }
            }

            // Parse memory info if healthy
            if (health->isHealthy) {
                json_object* memory;
                if (json_object_object_get_ex(json, "memory", &memory)) {
                    json_object* used;
                    if (json_object_object_get_ex(memory, "used", &used)) {
                        health->memory.used = json_object_get_int64(used);
                    }

                    json_object* total;
                    if (json_object_object_get_ex(memory, "total", &total)) {
                        health->memory.total = json_object_get_int64(total);
                    }
                }
            }

            success = true;
            json_object_put(json);
        }
    }

    curl_easy_cleanup(curl);
    return success;
}

uint32_t allocatePlayerId(NetworkServer* server) {
    // First try to find an unused ID
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!server->connections[i].inUse) {
            server->connections[i].inUse = true;
            server->connections[i].id = server->nextPlayerId++;
            return server->connections[i].id;
        }
    }
    return UINT32_MAX; // No IDs available
}

void freePlayerId(NetworkServer* server, uint32_t id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->connections[i].inUse && server->connections[i].id == id) {
            server->connections[i].inUse = false;
            server->connections[i].lastSeen = 0;
            memset(server->connections[i].ip, 0, INET_ADDRSTRLEN);
            server->connections[i].port = 0;
            break;
        }
    }
}

PlayerConnection* findExistingConnection(NetworkServer* server, const char* ip, uint16_t port) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->connections[i].inUse && 
            strcmp(server->connections[i].ip, ip) == 0 && 
            server->connections[i].port == port) {
            return &server->connections[i];
        }
    }
    return NULL;
}

void cleanupStaleConnections(NetworkServer* server) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->connections[i].inUse && 
            (now - server->connections[i].lastSeen) > server->connectionTimeout) {
            printf("Cleaning up stale connection: IP %s Port %d ID %d\n",
                   server->connections[i].ip,
                   server->connections[i].port,
                   server->connections[i].id);
            freePlayerId(server, server->connections[i].id);
        }
    }
}

bool initServer(NetworkServer* server) {
    // Check auth server health before initializing
    AuthServerHealth health = {0};
    if (!checkAuthServerHealth(&health)) {
        fprintf(stderr, "Failed to connect to auth server\n");
        return false;
    }

    if (!health.isHealthy) {
        fprintf(stderr, "Auth server is unhealthy: %s\n", health.database.error);
        return false;
    }

    printf("Auth server is healthy:\n");
    printf("  Service: %s v%s\n", health.service.name, health.service.version);
    printf("  Database latency: %dms\n", health.database.latencyMs);
    printf("  Memory usage: %lu/%lu bytes\n", health.memory.used, health.memory.total);

    // TCP socket setup
    server->serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->serverSocket < 0) {
        perror("Socket creation failed");
        return false;
    }

    // Add these socket options
    int opt = 1;
    if (setsockopt(server->serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        return false;
    }

    // Enable TCP keep-alive
    if (setsockopt(server->serverSocket, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_KEEPALIVE) failed");
        return false;
    }

    // Set TCP keep-alive parameters
    int keepalive_time = 60; // Time until first probe (seconds)
    int keepalive_intvl = 10; // Interval between probes (seconds)
    int keepalive_probes = 5; // Number of failed probes until connection drop

    setsockopt(server->serverSocket, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_time, sizeof(keepalive_time));
    setsockopt(server->serverSocket, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl, sizeof(keepalive_intvl));
    setsockopt(server->serverSocket, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_probes, sizeof(keepalive_probes));

    // Set socket to non-blocking
    int flags = fcntl(server->serverSocket, F_GETFL, 0);
    fcntl(server->serverSocket, F_SETFL, flags | O_NONBLOCK);

    server->serverAddr.sin_family = AF_INET;
    server->serverAddr.sin_addr.s_addr = INADDR_ANY;
    server->serverAddr.sin_port = htons(TCP_PORT);  // Use TCP_PORT
    server->playerCount = 0;

    if (bind(server->serverSocket, (struct sockaddr*)&server->serverAddr, sizeof(server->serverAddr)) < 0) {
        perror("Bind failed");
        return false;
    }

    if (listen(server->serverSocket, 3) < 0) {
        perror("Listen failed");
        return false;
    }

    FD_ZERO(&server->activeSockets);
    FD_SET(server->serverSocket, &server->activeSockets);

    // Initialize connection tracking
    memset(server->connections, 0, sizeof(server->connections));
    server->nextPlayerId = 1;  // Start from 1, 0 can be reserved
    server->connectionTimeout = 60; // 60 seconds timeout

    // Initialize WebSocket server with different port
    struct lws_context_creation_info info = {
        .port = WS_PORT,  // Use WS_PORT
        .protocols = protocols,
        .gid = -1,
        .uid = -1,
        .user = server,
        .options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT
    };

    server->wsContext = lws_create_context(&info);
    if (!server->wsContext) {
        fprintf(stderr, "WebSocket context creation failed\n");
        return false;
    }

    printf("TCP Server initialized on port %d\n", TCP_PORT);
    printf("WebSocket Server initialized on port %d\n", WS_PORT);
    return true;
}

// Modify acceptNewClients to initialize player data
bool verifyClientToken(NetworkServer* server, int socket, const char* token, AuthVerifyResponse* response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    // Prepare JSON payload
    json_object* payload = json_object_new_object();
    json_object_object_add(payload, "token", json_object_new_string(token));
    const char* jsonStr = json_object_to_json_string(payload);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", AUTH_SERVER_HOST, AUTH_SERVER_PORT, AUTH_ENDPOINT);

    char responseData[1024] = {0};
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, AUTH_TOKEN_HEADER);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, responseData);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    bool success = false;

    if (res == CURLE_OK) {
        json_object* json = json_tokener_parse(responseData);
        if (json) {
            json_object* status;
            if (json_object_object_get_ex(json, "status", &status)) {
                response->status = (strcmp(json_object_get_string(status), "success") == 0);
            }

            json_object* playerid;
            if (json_object_object_get_ex(json, "playerid", &playerid)) {
                response->playerid = json_object_get_int(playerid);
            }

            json_object* username;
            if (json_object_object_get_ex(json, "username", &username)) {
                strncpy(response->username, json_object_get_string(username), sizeof(response->username) - 1);
            }

            success = response->status;
            json_object_put(json);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    json_object_put(payload);

    return success;
}

void handlePendingClient(NetworkServer* server, PendingClient* client, const char* token) {
    // Send auth pending message first
    AuthPendingMessage pendingMsg = {
        .type = MSG_TYPE_AUTH_PENDING,
        .timeout = 5000  // 5 seconds timeout
    };
    send(client->socket, &pendingMsg, sizeof(AuthPendingMessage), 0);

    // Rest of authentication logic
    AuthVerifyResponse response = {0};
    
    if (verifyClientToken(server, client->socket, token, &response)) {
        // Create new player with verified ID from auth server
        int playerIndex = server->playerCount++;
        Player* newPlayer = &server->players[playerIndex];
        
        newPlayer->socket = client->socket;
        newPlayer->id = response.playerid;
        newPlayer->connected = true;
        newPlayer->isAuthenticated = true;
        newPlayer->physicsBody = b2_nullBodyId;
        newPlayer->position = (Vector2){0, 0};
        
        // Update connection tracking
        PlayerConnection* conn = &server->connections[playerIndex];
        conn->inUse = true;
        conn->id = response.playerid;
        strncpy(conn->ip, client->ip, INET_ADDRSTRLEN);
        conn->port = client->port;
        conn->lastSeen = time(NULL);

        printf("[AUTH SUCCESS] Player authenticated:\n");
        printf("  Username: %s\n", response.username);
        printf("  Player ID: %d\n", response.playerid);
        
        // Send success response to client
        ReadyMessage ready = {
            .type = MSG_TYPE_READY,
            .worldSeed = 0,
            .spawnX = 0.0f,
            .spawnY = 0.0f
        };
        send(client->socket, &ready, sizeof(ReadyMessage), 0);
    } else {
        // Auth failed - send error and close connection
        NetworkMessage error = {
            .type = MSG_TYPE_ERROR,
            .flags = 0
        };
        send(client->socket, &error, sizeof(NetworkMessage), 0);
        close(client->socket);
        
        printf("[AUTH FAILED] Client disconnected\n");
    }
}

void acceptNewClients(NetworkServer* server) {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    
    int newSocket = accept(server->serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
    if (newSocket >= 0) {
        // Enable keep-alive for client socket
        int opt = 1;
        if (setsockopt(newSocket, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
            perror("Client socket keep-alive failed");
            close(newSocket);
            return;
        }

        // Set TCP keep-alive parameters for client socket
        int keepalive_time = 60;
        int keepalive_intvl = 10;
        int keepalive_probes = 5;
        setsockopt(newSocket, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_time, sizeof(keepalive_time));
        setsockopt(newSocket, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl, sizeof(keepalive_intvl));
        setsockopt(newSocket, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_probes, sizeof(keepalive_probes));

        // Add error handling for send operations
        if (server->pendingCount < MAX_CLIENTS) {
            PendingClient* pending = &server->pendingClients[server->pendingCount++];
            pending->socket = newSocket;
            pending->connectTime = time(NULL);
            inet_ntop(AF_INET, &(clientAddr.sin_addr), pending->ip, INET_ADDRSTRLEN);
            pending->port = ntohs(clientAddr.sin_port);
            
            // Set socket non-blocking
            int flags = fcntl(newSocket, F_GETFL, 0);
            fcntl(newSocket, F_SETFL, flags | O_NONBLOCK);
            
            printf("[PENDING] New client connection:\n");
            printf("  IP: %s\n", pending->ip);
            printf("  Port: %d\n", pending->port);
            
            // Send handshake with error handling
            HandshakeMessage handshake = {
                .type = MSG_TYPE_HANDSHAKE,
                .protocol = 1,
                .maxPlayers = MAX_CLIENTS,
                .numPlayers = server->playerCount
            };
            
            ssize_t sent = send(newSocket, &handshake, sizeof(HandshakeMessage), MSG_NOSIGNAL);
            if (sent < 0) {
                perror("Failed to send handshake");
                close(newSocket);
                server->pendingCount--;
                return;
            }

            // Send auth pending message with error handling
            AuthPendingMessage pendingMsg = {
                .type = MSG_TYPE_AUTH_PENDING,
                .timeout = 5000
            };
            
            sent = send(newSocket, &pendingMsg, sizeof(AuthPendingMessage), MSG_NOSIGNAL);
            if (sent < 0) {
                perror("Failed to send auth pending message");
                close(newSocket);
                server->pendingCount--;
                return;
            }
        } else {
            close(newSocket);
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Accept failed");
    }

    // Service WebSocket events
    lws_service(server->wsContext, 0);

    // Periodically cleanup stale connections
    static time_t lastCleanup = 0;
    time_t now = time(NULL);
    if (now - lastCleanup > 10) { // Every 10 seconds
        cleanupStaleConnections(server);
        lastCleanup = now;
    }
}

void updatePlayerMovement(Player* player, uint8_t movementFlags, float deltaTime) {
    if (B2_IS_NULL(player->physicsBody)) return;
    
    b2Vec2 velocity = {0, 0};
    
    // Convert input flags to movement vector
    if (movementFlags & MOVE_LEFT) velocity.x -= 1.0f;
    if (movementFlags & MOVE_RIGHT) velocity.x += 1.0f;
    if (movementFlags & MOVE_UP) velocity.y -= 1.0f;
    if (movementFlags & MOVE_DOWN) velocity.y += 1.0f;
    
    // Normalize diagonal movement
    if (velocity.x != 0.0f && velocity.y != 0.0f) {
        float len = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        velocity.x /= len;
        velocity.y /= len;
    }
    
    // Apply movement force
    velocity.x *= PLAYER_SPEED;
    velocity.y *= PLAYER_SPEED;
    
    b2Body_SetLinearVelocity(player->physicsBody, velocity);
}

void handleIncomingMessage(NetworkServer* server, int clientId, NetworkMessage* msg) {
    Player* player = &server->players[clientId];
    
    switch (msg->type) {
        case MSG_TYPE_AUTH: {
            AuthMessage* auth = (AuthMessage*)msg;
            // Validate auth (placeholder)
            handleAuthMessage(server, player, auth);
            break;
        }
        
        case MSG_TYPE_ENTITY_UPDATE: {
            InputMessage* input = (InputMessage*)msg;
            if (player->isAuthenticated) {
                updatePlayerMovement(player, input->movementFlags, input->deltaTime);
            }
            break;
        }
        
        // ... handle other message types ...
    }
}

void handleNetworkMessages(NetworkServer* server, b2WorldId worldId, Camera2DState* camera) {
    uint8_t buffer[1024]; // Message buffer
    NetworkMessage* msg = (NetworkMessage*)buffer;
    
    for (int i = 0; i < server->playerCount; i++) {
        Player* player = &server->players[i];
        if (!player->connected) continue;

        ssize_t bytesRead = recv(player->socket, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (bytesRead >= sizeof(NetworkMessage)) {
            handleIncomingMessage(server, player->id, msg);
        }
    }
}

void broadcastWorldState(NetworkServer* server, Camera2DState* camera) {
    NetworkMessage msg;
    msg.type = MSG_TYPE_WORLD_STATE;    // Changed from MSG_WORLD_STATE to MSG_TYPE_WORLD_STATE
    
    // Send all player positions to all clients
    for (int i = 0; i < server->playerCount; i++) {
        Player* player = &server->players[i];
        if (!player->connected || B2_IS_NULL(player->physicsBody)) continue;

        b2Vec2 pos = b2Body_GetPosition(player->physicsBody);
        msg.x = pos.x;
        msg.y = pos.y;
        msg.clientId = player->id;
        
        for (int j = 0; j < server->playerCount; j++) {
            if (server->players[j].connected) {
                send(server->players[j].socket, &msg, sizeof(msg), MSG_DONTWAIT);
            }
        }
    }
}

void cleanupServer(NetworkServer* server) {
    for (int i = 0; i < server->playerCount; i++) {
        if (server->players[i].connected) {
            close(server->players[i].socket);
        }
    }
    close(server->serverSocket);
}

void handleNewConnection(NetworkServer* server, Player* player) {
    // Send initial handshake
    HandshakeMessage handshake = {
        .type = MSG_TYPE_HANDSHAKE,
        .clientId = player->id,
        .protocol = 1,  // Protocol version
        .maxPlayers = MAX_CLIENTS,
        .numPlayers = server->playerCount
    };
    
    send(player->socket, &handshake, sizeof(HandshakeMessage), 0);
}

bool validateWithAuthServer(NetworkServer* server, Player* player, const char* username, const char* token) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    // Prepare JSON payload
    json_object *payload = json_object_new_object();
    json_object_object_add(payload, "type", json_object_new_int(AUTH_TYPE_VERIFICATION));
    json_object_object_add(payload, "playerid", json_object_new_int(player->id));
    json_object_object_add(payload, "token", json_object_new_string(token));

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", AUTH_SERVER_HOST, AUTH_SERVER_PORT, AUTH_ENDPOINT);

    char response[1024] = {0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(payload));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    bool success = false;

    if (res == CURLE_OK) {
        // Parse response JSON
        json_object *json = json_tokener_parse(response);
        json_object *status = json_object_object_get(json, "status");
        if (status && json_object_get_boolean(status)) {
            success = true;
            player->isAuthenticated = true;
            // Store any additional auth data from response
            json_object *username = json_object_object_get(json, "username");
            if (username) {
                // Store username if needed
            }
        }
        json_object_put(json);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    json_object_put(payload);

    return success;
}

bool handleAuthMessage(NetworkServer* server, Player* player, AuthMessage* auth) {
    if (auth->protocol != 1) return false;

    // Validate with auth server
    if (!validateWithAuthServer(server, player, auth->username, "temp-token")) {
        printf("Authentication failed for player %d\n", player->id);
        return false;
    }

    // Send ready message only if auth succeeded
    ReadyMessage ready = {
        .type = MSG_TYPE_READY,
        .worldSeed = 0,
        .spawnX = 0.0f,
        .spawnY = 0.0f
    };
    
    send(player->socket, &ready, sizeof(ReadyMessage), 0);
    printf("Player %d authenticated successfully\n", player->id);
    return true;
}

void ws_send_binary(struct lws* wsi, void* data, size_t len) {
    unsigned char* buf = malloc(LWS_PRE + len);
    memcpy(buf + LWS_PRE, data, len);
    lws_write(wsi, buf + LWS_PRE, len, LWS_WRITE_BINARY);
    free(buf);
}

void ws_broadcast_state(NetworkServer* server, void* data, size_t len) {
    for (int i = 0; i < server->playerCount; i++) {
        Player* player = &server->players[i];
        if (player->connected && player->isWebSocket) {
            PendingClient* pending = findPendingClientById(server, player->id);
            if (pending && pending->wsi) {
                ws_send_binary(pending->wsi, data, len);
            }
        }
    }
}

// Add these implementations before ws_broadcast_state
void handleWebSocketMessage(NetworkServer* server, struct lws* wsi, void* data, size_t len) {
    // Convert incoming WebSocket data to NetworkMessage
    NetworkMessage* msg = (NetworkMessage*)data;
    
    // Find the player associated with this WebSocket
    for (int i = 0; i < server->playerCount; i++) {
        Player* player = &server->players[i];
        if (player->isWebSocket) {
            PendingClient* pending = findPendingClientById(server, player->id);
            if (pending && pending->wsi == wsi) {
                handleIncomingMessage(server, player->id, msg);
                break;
            }
        }
    }
}

void removeWebSocketClient(NetworkServer* server, struct lws* wsi) {
    // Find and remove the client with this WebSocket
    for (int i = 0; i < server->playerCount; i++) {
        Player* player = &server->players[i];
        if (player->isWebSocket) {
            PendingClient* pending = findPendingClientById(server, player->id);
            if (pending && pending->wsi == wsi) {
                // Clean up player
                player->connected = false;
                player->isWebSocket = false;
                
                // Clean up pending client
                pending->wsi = NULL;
                pending->isWebSocket = false;
                
                // Free player ID
                freePlayerId(server, player->id);
                break;
            }
        }
    }
}

PendingClient* findPendingClientById(NetworkServer* server, int id) {
    for (int i = 0; i < server->pendingCount; i++) {
        if (server->pendingClients[i].id == id) {
            return &server->pendingClients[i];
        }
    }
    return NULL;
}

// ... rest of existing code ...
