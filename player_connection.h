#ifndef PLAYER_CONNECTION_H
#define PLAYER_CONNECTION_H

#include "db_client.h"
#include "websocket.h"
#include <stdbool.h>

typedef struct {
    uint32_t player_id;
    char* username;
    bool authenticated;
    WebSocket ws;
    time_t connect_time;
    time_t last_activity;
} PlayerConnection;

typedef struct {
    PlayerConnection* connections;
    size_t count;
    size_t capacity;
    DatabaseClient* db_client;
} PlayerConnectionManager;

// Initialize the connection manager
bool initPlayerConnectionManager(PlayerConnectionManager* manager, DatabaseClient* db_client);

// Handle new WebSocket connection request
bool handleNewPlayerConnection(PlayerConnectionManager* manager, 
                             const char* token,
                             WebSocket* ws);

// Cleanup connection manager
void cleanupPlayerConnectionManager(PlayerConnectionManager* manager);

#endif
