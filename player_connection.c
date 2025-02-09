#include "player_connection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool initPlayerConnectionManager(PlayerConnectionManager* manager, DatabaseClient* db_client) {
    manager->connections = malloc(sizeof(PlayerConnection) * 100); // Start with 100 slots
    if (!manager->connections) return false;
    
    manager->count = 0;
    manager->capacity = 100;
    manager->db_client = db_client;
    return true;
}

bool handleNewPlayerConnection(PlayerConnectionManager* manager, 
                             const char* token,
                             WebSocket* ws) {
    if (!manager || !token || !ws) {
        fprintf(stderr, "[Player] Invalid parameters for new connection\n");
        return false;
    }

    fprintf(stderr, "[Player] Processing new connection with token (len=%zu)\n", strlen(token));

    // Ensure the socket is valid and connected
    if (!ws->handshake_complete) {
        fprintf(stderr, "[Player] Error: WebSocket handshake not complete\n");
        return false;
    }

    // Store the WebSocket locally first - don't use the passed pointer directly
    WebSocket temp_ws = *ws;
    
    // Verify token before allocating any resources
    TokenVerifyResult result = {0};
    if (!verifyUserToken(manager->db_client, token, &result)) {
        fprintf(stderr, "[Player] Token verification failed\n");
        return false;
    }

    if (!result.success) {
        fprintf(stderr, "[Player] Invalid token: %s\n", result.data.error);
        return false;
    }

    fprintf(stderr, "[Player] Token verified for player %u\n", result.data.player_id);

    // Check for existing connection
    for (size_t i = 0; i < manager->count; i++) {
        if (manager->connections[i].player_id == result.data.player_id) {
            fprintf(stderr, "[Player] Player %u already connected\n", result.data.player_id);
            return false;
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
