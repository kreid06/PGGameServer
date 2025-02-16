#ifndef PLAYER_CONNECTION_H
#define PLAYER_CONNECTION_H

#include <box2d/box2d.h>
#include <stdbool.h>
#include <time.h>

#include "../database/db_client.h"
#include "../physics/player/player_physics.h"
#include "websockets/websocket.h"
#include "game_protocol.h"

// Forward declare message handler before structs
static void onPlayerMessage(void* context, WebSocket* ws, const uint8_t* data, size_t length);

// Full structure definitions - remove 'typedef struct' to avoid redefinition
struct PlayerConnection {
    uint32_t player_id;
    char* username;
    bool authenticated;
    WebSocket ws;
    time_t connect_time;
    time_t last_activity;
    b2BodyId physics_body;
    uint32_t last_input_seq;
    double last_input_time;
};

struct PlayerConnectionManager {
    struct PlayerConnection* connections;
    size_t count;
    size_t capacity;
    DatabaseClient* db_client;
    b2WorldId worldId;
    bool db_ready;
};

// Type aliases
typedef struct PlayerConnection PlayerConnection;
typedef struct PlayerConnectionManager PlayerConnectionManager;

// Function declarations
bool initPlayerConnectionManager(PlayerConnectionManager* manager, DatabaseClient* db_client, b2WorldId worldId);
bool handleNewPlayerConnection(PlayerConnectionManager* manager, const char* token, WebSocket* ws);
void removeDisconnectedPlayers(PlayerConnectionManager* manager);
void cleanupPlayerConnectionManager(PlayerConnectionManager* manager);
void handlePlayerInput(PlayerConnection* player, const uint8_t* data, size_t length, PlayerConnectionManager* manager);
void sendPlayerState(PlayerConnection* player, PlayerConnectionManager* manager);
bool verifyUserToken(DatabaseClient* client, const char* token, TokenVerifyResult* result);

#endif
