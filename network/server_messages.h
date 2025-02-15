#ifndef SERVER_MESSAGES_H
#define SERVER_MESSAGES_H

#include "common_protocol.h"

// Server message types (0x30-0x4F)
enum ServerMessageTypes {
    // Authentication & Connection
    SVR_MSG_AUTH_RESPONSE = 0x30,
    SVR_MSG_KICK = 0x31,
    
    // Game State
    SVR_MSG_WORLD_STATE = 0x40,
    SVR_MSG_ENTITY_UPDATE = 0x41,
    SVR_MSG_PLAYER_STATE = 0x42,
    SVR_MSG_EVENT = 0x43
};

// Server auth response
typedef struct {
    MessageHeader header;  // type must be SVR_MSG_AUTH_RESPONSE
    uint8_t status;       // Auth status (success/failure)
    uint32_t player_id;   // Assigned player ID
    uint32_t world_seed;  // World generation seed
} __attribute__((packed)) ServerAuthResponse;

// Server player state update
typedef struct {
    MessageHeader header;  // type must be SVR_MSG_PLAYER_STATE
    uint32_t player_id;   // Player identifier
    float pos_x;         // Position
    float pos_y;
    float velocity;      // Current velocity
    float rotation;      // Current rotation
    uint8_t state;       // Player state flags
} __attribute__((packed)) ServerPlayerState;

#endif
