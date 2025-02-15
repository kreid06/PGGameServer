# Game Protocol Documentation

## Protocol Version
- Major: 1
- Minor: 0

## Base Protocol Range (0x00-0x0F)
| Code   | Name      | Description                  |
|--------|-----------|------------------------------|
| 0x00   | MSG_NONE  | No operation                |
| 0x01   | MSG_ACK   | Acknowledge receipt         |
| 0x02   | MSG_ERROR | Error notification          |
| 0x03   | MSG_PING  | Connection health check     |
| 0x04   | MSG_PONG  | Ping response              |

## Client -> Server Messages (0x20-0x3F)
### Connection Messages (0x20-0x27)
| Code   | Name            | Description               |
|--------|----------------|---------------------------|
| 0x20   | MSG_CONNECT    | Initial connection       |
| 0x21   | MSG_DISCONNECT | Graceful disconnect      |
| 0x22   | MSG_AUTH_REQUEST | Authentication request |
| 0x23   | MSG_AUTH_RESPONSE| Auth response          |
| 0x24   | MSG_ERROR      | Error notification       |

### Game Input (0x28-0x2F)
| Code   | Name             | Description            |
|--------|-----------------|------------------------|
| 0x28   | MSG_PLAYER_INPUT| Player movement/action |
| 0x29   | MSG_PLAYER_COMMAND| Game command         |
| 0x2A   | MSG_PLAYER_CHAT | Chat message          |

## Server -> Client Messages (0x40-0x5F)
### Game State Updates (0x40-0x47)
| Code   | Name             | Description           |
|--------|-----------------|------------------------|
| 0x40   | MSG_WORLD_STATE | Complete world state  |
| 0x41   | MSG_PLAYER_STATE| Player state update   |
| 0x42   | MSG_ENTITY_UPDATE| Entity state update  |
| 0x43   | MSG_SPAWN      | Entity spawn          |
| 0x44   | MSG_DESPAWN    | Entity despawn        |

### Game Events (0x48-0x4F)
| Code   | Name           | Description             |
|--------|---------------|-------------------------|
| 0x48   | MSG_EVENT     | Game event             |
| 0x49   | MSG_NOTIFICATION| Player notification   |
| 0x4A   | MSG_CHAT      | Chat broadcast         |

## Player States
| State  | Description                |
|--------|----------------------------|
| 0x00   | PLAYER_STATE_NONE         |
| 0x01   | PLAYER_STATE_VERIFYING    |
| 0x02   | PLAYER_STATE_ACCEPTED     |
| 0x03   | PLAYER_STATE_REJECTED     |

## Input Flags
| Flag    | Value    | Description           |
|---------|----------|-----------------------|
| NONE    | 0x00     | No input            |
| FORWARD | 1 << 0   | Move forward        |
| BACKWARD| 1 << 1   | Move backward       |
| LEFT    | 1 << 2   | Turn/strafe left    |
| RIGHT   | 1 << 3   | Turn/strafe right   |
| BOOST   | 1 << 4   | Speed boost         |
| ACTION1 | 1 << 5   | Primary action      |
| ACTION2 | 1 << 6   | Secondary action    |
| BRAKE   | 1 << 7   | Brake/stop          |

## Message Structures
All messages start with MessageHeader:
