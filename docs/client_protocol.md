# Game Client Protocol Cheatsheet

## Message Format
All messages follow this binary format:


[1 byte: type][4 bytes: length][N bytes: payload]
## Connection Flow
1. Client connects via WebSocket to `ws://server:port`
2. Client sends `MSG_CONNECT_REQUEST (0x01)` with auth token
3. Server responds with `MSG_CONNECT_SUCCESS (0x02)` or `MSG_AUTH_FAILURE (0x05)`
4. Begin normal gameplay communication

## Message Types

### Connection & Auth (0-9)
0x01 CONNECT_REQUEST → Payload: [N bytes: auth_token] ← Response: 0x02 or 0x05

0x02 CONNECT_SUCCESS ← Payload: [4 bytes: player_id][4 bytes: session_id]

0x05 AUTH_FAILURE ← Payload: [N bytes: error_message]

0x06 KEEP_ALIVE ↔ Payload: empty (send every 30s)

### Player Actions (10-29)
0x0A PLAYER_MOVEMENT → Payload: [4 bytes: x_pos][4 bytes: y_pos][4 bytes: direction]

0x0B OTHER_MOVEMENT ← Payload: [4 bytes: player_id][4 bytes: x_pos][4 bytes: y_pos][4 bytes: direction]

0x0D SHIP_INPUT → Payload: [1 byte: input_flags][4 bytes: steering] Input flags: 0x01 = Forward 0x02 = Backward 0x04 = Left 0x08 = Right

0x0F MOUNT_REQUEST → Payload: [4 bytes: ship_id][1 byte: mount_position]

### Ship States (30-49)
0x1E SHIP_STATE ← Payload: [4 bytes: ship_id][4 bytes: x][4 bytes: y][4 bytes: rotation] [4 bytes: velocity_x][4 bytes: velocity_y][1 byte: flags] Flags: 0x01 = Anchored 0x02 = Sailing 0x04 = Damaged

0x20 SHIP_ACTION → Payload: [4 bytes: ship_id][1 byte: action_type][N bytes: action_data]

### Projectiles (50-59)
0x32 PROJECTILE_SPAWN ← Payload: [4 bytes: proj_id][4 bytes: x][4 bytes: y][4 bytes: velocity_x] [4 bytes: velocity_y][1 byte: type]

0x34 PROJECTILE_HIT ← Payload: [4 bytes: proj_id][4 bytes: target_id][4 bytes: damage]

### Entity States (60-69)
0x3C ENTITY_SPAWN ← Payload: [4 bytes: entity_id][1 byte: type][4 bytes: x][4 bytes: y]

0x3E ENTITY_ACTION ← Payload: [4 bytes: entity_id][1 byte: action][N bytes: data]

### World State (70-89)
0x46 WORLD_STATE ← Payload: [4 bytes: time][1 byte: weather][4 bytes: num_entities] [N bytes: entity_data]

0x47 WIND_UPDATE ← Payload: [4 bytes: direction][4 bytes: speed]
