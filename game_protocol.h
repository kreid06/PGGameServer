#ifndef GAME_PROTOCOL_H
#define GAME_PROTOCOL_H

// WebSocket Game Protocol
// Connection (0x10-0x1F)
#define GAME_MSG_CONNECT      0x10  // Initial connect with token
#define GAME_MSG_DISCONNECT   0x11  // Client disconnect
#define GAME_MSG_KICK        0x12  // Server kicks client
#define GAME_MSG_ERROR       0x1F  // Error message

// Player Actions (0x20-0x2F)
#define GAME_MSG_MOVE        0x20  // Player movement
#define GAME_MSG_ACTION      0x21  // Player action
#define GAME_MSG_CHAT        0x22  // Chat message

// Ship Actions (0x30-0x3F)
#define GAME_MSG_SHIP_MOVE   0x30  // Ship movement
#define GAME_MSG_SHIP_ACTION 0x31  // Ship action/ability

// World State (0x40-0x4F)
#define GAME_MSG_WORLD_STATE 0x40  // World state update
#define GAME_MSG_ENTITY_POS  0x41  // Entity position update

// ... rest of game-specific protocol ...

#endif
