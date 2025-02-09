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
    fprintf(stderr, "[Player] New connection request with token\n");
    
    if (!manager->db_client->connected || !manager->db_client->auth_complete) {
        fprintf(stderr, "[Player] Database not ready - rejecting connection\n");
        return false;
    }
    
    // First verify token with auth server
    TokenVerifyResult result;
    if (!verifyUserToken(manager->db_client, token, &result)) {
        fprintf(stderr, "[Player] Token verification failed\n");
        return false;
    }

    if (!result.success) {
        fprintf(stderr, "[Player] Invalid token: %s\n", result.data.error);
        return false;
    }

    // Check if player is already connected
    for (size_t i = 0; i < manager->count; i++) {
        if (manager->connections[i].player_id == result.data.player_id) {
            fprintf(stderr, "[Player] Player %u already connected\n", result.data.player_id);
            return false;
        }
    }

    // Add new connection
    if (manager->count >= manager->capacity) {
        size_t new_capacity = manager->capacity * 2;
        PlayerConnection* new_conns = realloc(manager->connections, 
                                            new_capacity * sizeof(PlayerConnection));
        if (!new_conns) return false;
        
        manager->connections = new_conns;
        manager->capacity = new_capacity;
    }

    // Initialize new connection
    PlayerConnection* conn = &manager->connections[manager->count++];
    conn->player_id = result.data.player_id;
    conn->authenticated = true;
    conn->ws = *ws; // Copy WebSocket connection
    conn->connect_time = time(NULL);
    conn->last_activity = time(NULL);
    
    fprintf(stderr, "[Player] Player %u authenticated and connected\n", 
            conn->player_id);
    return true;
}

void cleanupPlayerConnectionManager(PlayerConnectionManager* manager) {
    if (!manager) return;
    
    // Close all connections
    for (size_t i = 0; i < manager->count; i++) {
        PlayerConnection* conn = &manager->connections[i];
        if (conn->authenticated) {
            ws_disconnect(&conn->ws);
        }
        free(conn->username);
        conn->username = NULL;
    }
    
    free(manager->connections);
    manager->connections = NULL;
    manager->count = 0;
    manager->capacity = 0;
}
