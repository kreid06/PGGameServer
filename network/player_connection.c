#include "core/includes.h"
#include "player_connection.h"
#include "game_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Remove duplicate PlayerConnection struct definition

bool initPlayerConnectionManager(PlayerConnectionManager* manager, 
                              DatabaseClient* db_client,
                              b2WorldId worldId) {
    manager->connections = malloc(sizeof(PlayerConnection) * 100); // Start with 100 slots
    if (!manager->connections) return false;
    
    manager->count = 0;
    manager->capacity = 100;
    manager->db_client = db_client;
    manager->worldId = worldId;
    return true;
}

// Define handler before it's used
static void onPlayerMessage(void* context, WebSocket* ws, const uint8_t* data, size_t length) {
    PlayerConnectionManager* manager = (PlayerConnectionManager*)context;
    PlayerConnection* player = (PlayerConnection*)ws->user_data;
    
    if (!player || length < 1) return;
    
    switch (data[0]) {
        case GAME_MSG_INPUT:  // Changed from GAME_MSG_PLAYER_INPUT
            handlePlayerInput(player, data + 1, length - 1, manager);
            break;
    }
}

bool handleNewPlayerConnection(PlayerConnectionManager* manager, 
                             const char* token,
                             WebSocket* ws) {
    // Add detailed parameter validation logging
    if (!manager) {
        fprintf(stderr, "[Player] Invalid parameter: manager is NULL\n");
        return false;
    }
    if (!token && ws) {
        token = ws_get_token(ws);
        fprintf(stderr, "[Player] Using token from WebSocket connection\n");
    }
    if (!token) {
        fprintf(stderr, "[Player] No token available from any source\n");
        return false;
    }
    if (!ws) {
        fprintf(stderr, "[Player] Invalid parameter: WebSocket is NULL\n");
        return false;
    }
    if (!ws->initialized || !ws->valid) {
        fprintf(stderr, "[Player] Invalid WebSocket state: initialized=%d, valid=%d\n",
                ws->initialized, ws->valid);
        return false;
    }
    if (!ws->handshake_complete) {
        fprintf(stderr, "[Player] Error: WebSocket handshake not complete (socket=%d)\n",
                ws->sock);
        return false;
    }

    fprintf(stderr, "[Player] Processing new connection:\n");
    fprintf(stderr, "  - Token length: %zu\n", strlen(token));
    fprintf(stderr, "  - WebSocket: initialized=%d, valid=%d, connected=%d, socket=%d\n",
            ws->initialized, ws->valid, ws->connected, ws->sock);

    // Ensure the socket is valid and connected
    if (!ws->handshake_complete) {
        fprintf(stderr, "[Player] Error: WebSocket handshake not complete\n");
        return false;
    }

    // Store the WebSocket locally first - don't use the passed pointer directly
    WebSocket temp_ws = *ws;
    
    // Send initial verification message
    uint8_t verifying_msg[] = {
        GAME_MSG_CONNECT,          // Initial connect message
        GAME_STATE_VERIFYING,      // Verifying state
        0x00, 0x00                // No payload
    };
    ws_send_binary(ws, verifying_msg, sizeof(verifying_msg));
    
    // Verify token before allocating any resources
    TokenVerifyResult result = {0};
    if (!verifyUserToken(manager->db_client, token, &result)) {
        // Send verification failed message
        uint8_t verify_failed[] = {
            GAME_MSG_ERROR,
            GAME_ERR_AUTH,
            0x00, 0x00
        };
        ws_send_binary(ws, verify_failed, sizeof(verify_failed));
        fprintf(stderr, "[Player] Token verification failed\n");
        return false;
    }

    if (!result.success) {
        // Send invalid token message with error
        size_t err_len = strlen(result.data.error);
        uint8_t* invalid_token = malloc(4 + err_len);
        invalid_token[0] = GAME_MSG_ERROR;
        invalid_token[1] = 0x02;              // Invalid token error
        invalid_token[2] = (err_len >> 8) & 0xFF;
        invalid_token[3] = err_len & 0xFF;
        memcpy(invalid_token + 4, result.data.error, err_len);
        
        ws_send_binary(ws, invalid_token, 4 + err_len);
        free(invalid_token);
        fprintf(stderr, "[Player] Invalid token: %s\n", result.data.error);
        return false;
    }

    fprintf(stderr, "[Player] Token verified for player %u\n", result.data.player_id);

    // Check for existing connection with same player_id
    for (size_t i = 0; i < manager->count; i++) {
        if (manager->connections[i].player_id == result.data.player_id) {
            if (manager->connections[i].authenticated) {
                fprintf(stderr, "[Player] Player %u already connected\n", 
                        result.data.player_id);
                uint8_t error_msg[] = {
                    GAME_MSG_ERROR,
                    GAME_ERR_DUPLICATE,
                    0x00, 0x00
                };
                ws_send_binary(ws, error_msg, sizeof(error_msg));
                return false;
            } else {
                // Previous connection exists but not authenticated
                // Clean it up to allow reconnection
                fprintf(stderr, "[Player] Cleaning up previous unauthenticated connection for player %u\n",
                        result.data.player_id);
                removeDisconnectedPlayers(manager);
            }
        }
    }

    // Ensure capacity for new connection
    if (manager->count >= manager->capacity) {
        size_t new_capacity = manager->capacity * 2;
        PlayerConnection* new_conns = realloc(manager->connections, 
                                            new_capacity * sizeof(PlayerConnection));
        if (!new_conns) {
            fprintf(stderr, "[Player] Failed to expand connections array\n");
            return false;
        }
        manager->connections = new_conns;
        manager->capacity = new_capacity;
    }

    // Initialize new connection
    PlayerConnection* conn = &manager->connections[manager->count++];
    memset(conn, 0, sizeof(PlayerConnection)); // Clear the struct first
    
    conn->player_id = result.data.player_id;
    conn->authenticated = true;
    conn->ws = temp_ws;  // Copy our local WebSocket
    conn->connect_time = time(NULL);
    conn->last_activity = time(NULL);
    
    // Create physics body for player
    conn->physics_body = createPlayerBody(manager->worldId, 0.0f, 0.0f);
    if (!b2Body_IsValid(conn->physics_body)) {
        fprintf(stderr, "[Player] Failed to create physics body\n");
        return false;
    }

    // Update message handler setup
    ws->user_data = conn;  // Store player connection
    ws_set_message_handler(ws, onPlayerMessage, manager);  // Pass manager as context

    // Send successful connection message with player data
    uint8_t connect_success[12] = {
        GAME_MSG_AUTH_RESPONSE,
        GAME_STATE_ACCEPTED,
        0x00, 0x08,             // Payload length (8 bytes)
        // Player ID (4 bytes)
        (result.data.player_id >> 24) & 0xFF,
        (result.data.player_id >> 16) & 0xFF,
        (result.data.player_id >> 8) & 0xFF,
        result.data.player_id & 0xFF,
        // Connection time (4 bytes)
        (conn->connect_time >> 24) & 0xFF,
        (conn->connect_time >> 16) & 0xFF,
        (conn->connect_time >> 8) & 0xFF,
        conn->connect_time & 0xFF
    };
    
    ws_send_binary(ws, connect_success, sizeof(connect_success));

    // Send initial player state
    uint8_t player_init[] = {
        GAME_MSG_WORLD_STATE,       // Changed from GAME_MSG_WORLD_STATE
        0x01,                  // Initial state
        0x00, 0x00            // No additional data for now
    };
    ws_send_binary(ws, player_init, sizeof(player_init));
    
    fprintf(stderr, "[Player] Player %u authenticated and connected successfully\n", 
            conn->player_id);
    return true;
}

// Fix null pointer issues in cleanup
void cleanupPlayerConnectionManager(PlayerConnectionManager* manager) {
    if (!manager) return;
    
    // Close all connections
    for (size_t i = 0; i < manager->count; i++) {
        PlayerConnection* conn = &manager->connections[i];
        if (conn->authenticated && conn->ws.sock > 0) {  // Add socket check
            ws_disconnect(&conn->ws);
        }
        if (conn->username) {  // Add null check
            free(conn->username);
            conn->username = NULL;
        }
    }
    
    if (manager->connections) {  // Add null check
        free(manager->connections);
        manager->connections = NULL;
    }
    manager->count = 0;
    manager->capacity = 0;
}

void removeDisconnectedPlayers(PlayerConnectionManager* manager) {
    for (size_t i = 0; i < manager->count; i++) {
        PlayerConnection* conn = &manager->connections[i];
        
        if (!conn->ws.connected || conn->ws.sock <= 0) {
            fprintf(stderr, "[Player] Player %u disconnecting, cleaning up...\n", 
                    conn->player_id);

            // Send graceful disconnect message to other players
            uint8_t disconnect_msg[] = {
                GAME_MSG_DISCONNECT,
                0x00,  // Normal disconnect
                0x00, 0x04,  // 4 byte payload
                (conn->player_id >> 24) & 0xFF,
                (conn->player_id >> 16) & 0xFF,
                (conn->player_id >> 8) & 0xFF,
                conn->player_id & 0xFF
            };

            // Broadcast disconnect to other players
            for (size_t j = 0; j < manager->count; j++) {
                if (j != i && manager->connections[j].authenticated) {
                    ws_send_binary(&manager->connections[j].ws, 
                                 disconnect_msg, 
                                 sizeof(disconnect_msg));
                }
            }

            // Clean up physics body if it exists
            if (b2Body_IsValid(conn->physics_body)) {
                b2DestroyBody(conn->physics_body);
                conn->physics_body = b2_nullBodyId;
            }

            // Clean up allocated resources
            if (conn->username) {
                free(conn->username);
                conn->username = NULL;
            }

            // Reset authentication state to allow reconnection
            conn->authenticated = false;
            conn->last_activity = 0;
            conn->last_input_seq = 0;
            conn->last_input_time = 0;

            // Close WebSocket connection
            ws_disconnect(&conn->ws);

            // Remove from active connections array
            if (i < manager->count - 1) {
                memmove(&manager->connections[i],
                       &manager->connections[i + 1],
                       (manager->count - i - 1) * sizeof(PlayerConnection));
            }
            manager->count--;
            i--; // Recheck this index since we shifted elements

            fprintf(stderr, "[Player] Player %u cleanup complete\n", 
                    conn->player_id);
        }
    }
}

// Update function signature to include manager
void handlePlayerInput(PlayerConnection* player, const uint8_t* data, size_t length, PlayerConnectionManager* manager) {
    if (length < sizeof(GamePlayerInputMessage)) return;
    
    const GamePlayerInputMessage* input = (const GamePlayerInputMessage*)data;
    float dt = (input->client_time - player->last_input_time) / 1000.0f;
    
    // Only process newer inputs
    if (input->header.sequence > player->last_input_seq) {  // Fix: access sequence through header
        // Apply movement
        applyPlayerMovement(player->physics_body, input->input_flags, dt);
        limitPlayerVelocity(player->physics_body);
        
        // Update tracking
        player->last_input_seq = input->header.sequence;  // Fix: access sequence through header
        player->last_input_time = input->client_time;
        
        // Send immediate state update
        sendPlayerState(player, manager);
    }
}

// Update sendPlayerState signature
void sendPlayerState(PlayerConnection* player, PlayerConnectionManager* manager) {
    b2Vec2 pos = b2Body_GetPosition(player->physics_body);
    b2Vec2 vel = b2Body_GetLinearVelocity(player->physics_body);
    float rot = b2Body_GetAngle(player->physics_body);
    
    GamePlayerStateMessage msg = {0};  // Zero initialize
    msg.header.type = GAME_MSG_PLAYER_STATE;  // Fix: access type through header
    msg.header.sequence = player->last_input_seq;  // Fix: access sequence through header
    msg.player_id = player->player_id;
    msg.pos_x = pos.x;
    msg.pos_y = pos.y;
    msg.velocity_x = vel.x;
    msg.velocity_y = vel.y;
    msg.rotation = rot;
    msg.timestamp = (uint32_t)time(NULL);
    msg.state_flags = GAME_STATE_ACCEPTED;
    
    uint8_t packet[sizeof(msg) + 4] = {
        GAME_MSG_PLAYER_STATE,
        0x00,
        (sizeof(msg) >> 8) & 0xFF,
        sizeof(msg) & 0xFF
    };
    memcpy(packet + 4, &msg, sizeof(msg));
    
    // Broadcast to all players
    for (size_t i = 0; i < manager->count; i++) {
        ws_send_binary(&manager->connections[i].ws, packet, sizeof(packet));
    }
}
