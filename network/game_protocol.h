#ifndef GAME_PROTOCOL_H
#define GAME_PROTOCOL_H

#include "common_protocol.h"

// Game message types (0x20-0x4F)
#define GAME_MSG_NONE          0x20
#define GAME_MSG_CONNECT       0x21
#define GAME_MSG_DISCONNECT    0x22
#define GAME_MSG_AUTH_REQUEST  0x23
#define GAME_MSG_AUTH_RESPONSE 0x24
#define GAME_MSG_ERROR         0x2F
#define GAME_MSG_INPUT         0x25  // Add missing input message type

// Game state messages (0x30-0x3F)
#define GAME_MSG_WORLD_STATE   0x30
#define GAME_MSG_PLAYER_STATE  0x31
#define GAME_MSG_ENTITY_UPDATE 0x32
#define GAME_MSG_SPAWN         0x33
#define GAME_MSG_DESPAWN       0x34

// Game States
#define GAME_STATE_NONE      0x00
#define GAME_STATE_VERIFYING 0x01
#define GAME_STATE_ACCEPTED  0x02
#define GAME_STATE_REJECTED  0x03

// Game Error Codes
#define GAME_ERR_NONE       0x00
#define GAME_ERR_AUTH       0x01
#define GAME_ERR_DUPLICATE  0x02
#define GAME_ERR_TIMEOUT    0x03

// Input Flags - Basic movement (bits 0-7)
#define INPUT_NONE           0x0000
#define INPUT_FORWARD        (1 << 0)  // W
#define INPUT_BACKWARD       (1 << 1)  // S
#define INPUT_LEFT          (1 << 2)  // A
#define INPUT_RIGHT         (1 << 3)  // D
#define INPUT_ACTION1       (1 << 4)  // Primary attack (e.g. Left click)
#define INPUT_ACTION2       (1 << 5)  // Secondary action (e.g. Right click)

// Combined input states
#define INPUT_STRAFE_LEFT   (INPUT_FORWARD | INPUT_LEFT)
#define INPUT_STRAFE_RIGHT  (INPUT_FORWARD | INPUT_RIGHT)

// Base header structure for game messages
typedef struct {
    uint8_t type;           // Message type
    uint8_t flags;          // Message flags
    uint16_t sequence;      // Sequence number
    uint32_t length;        // Payload length
} __attribute__((packed)) GameMessageHeader;

// Input and state messages
typedef struct {
    GameMessageHeader header;      // type = GAME_MSG_PLAYER_INPUT
    uint16_t input_flags;     // Current active inputs
    uint16_t changed_flags;   // Inputs that changed this frame
    float rotation;           // Ship rotation in radians
    uint32_t client_time;     // Client timestamp
    uint16_t ping;           // Client-measured ping
} __attribute__((packed)) GamePlayerInputMessage;

typedef struct {
    GameMessageHeader header;   // type = GAME_MSG_PLAYER_STATE
    uint32_t player_id;    // Player identifier
    uint32_t sequence;     // Input sequence being acked
    float pos_x;          // Current position
    float pos_y;
    float velocity_x;     // Current velocity
    float velocity_y;
    float rotation;       // Current rotation
    uint32_t timestamp;   // Server timestamp
    uint8_t state_flags;  // Player state flags
} __attribute__((packed)) GamePlayerStateMessage;

// Authentication messages
typedef struct {
    GameMessageHeader header;   // type = GAME_MSG_AUTH_REQUEST
    char token[256];       // Authentication token
    uint16_t version;      // Client version
} __attribute__((packed)) GameAuthRequestMessage;

typedef struct {
    GameMessageHeader header;   // type = GAME_MSG_AUTH_RESPONSE
    uint8_t status;        // Success/failure
    uint32_t player_id;    // Assigned player ID
    uint32_t world_seed;   // World generation seed
} __attribute__((packed)) GameAuthResponseMessage;

#endif
