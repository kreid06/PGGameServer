#ifndef NETWORK_H
#define NETWORK_H

#include "game_state.h"
#include <sys/socket.h>
#include <sys/select.h>  // Add this for fd_set
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <netinet/tcp.h>  // For TCP_KEEPIDLE, etc.
#include <errno.h>        // For errno
#include <libwebsockets.h> // Add to includes

// Callback function for CURL responses
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

#define MAX_CLIENTS 32

// Separate ports for TCP and WebSocket
#define TCP_PORT 8080
#define WS_PORT 8081  // Add new WebSocket port

// Replace existing PORT definition
#undef PORT

// Add movement flags before the message types
typedef enum MovementFlags {
    MOVE_LEFT = 1 << 0,    // 0001
    MOVE_RIGHT = 1 << 1,   // 0010
    MOVE_UP = 1 << 2,      // 0100
    MOVE_DOWN = 1 << 3,    // 1000
    // Additional movement flags can be added here
} MovementFlags;

// Core System Messages (0-9)
#define MSG_TYPE_HANDSHAKE        0
#define MSG_TYPE_READY            1
#define MSG_TYPE_AUTH             2
#define MSG_TYPE_ERROR            3
#define MSG_TYPE_DISCONNECT       4
#define MSG_TYPE_KEEPALIVE        5
#define MSG_TYPE_STATE_SYNC       6
#define MSG_TYPE_DELTA_UPDATE      7

// Add after other message types:
#define MSG_TYPE_AUTH_PENDING     8  // Server is verifying auth token

// World State Messages (10-19)
#define MSG_TYPE_WORLD_STATE      10
#define MSG_TYPE_WORLD_UPDATE     11
#define MSG_TYPE_ENVIRONMENT      12
#define MSG_TYPE_COLLISION        13

// Entity Messages (20-39)
#define MSG_TYPE_ENTITY_SPAWN     20
#define MSG_TYPE_ENTITY_DESPAWN   21
#define MSG_TYPE_ENTITY_STATE     22
#define MSG_TYPE_ENTITY_UPDATE    23
#define MSG_TYPE_ENTITY_ACTION    24

// Authentication message types
#define AUTH_TYPE_VERIFICATION 0x01
#define AUTH_TYPE_TOKEN_VERIFY 0x02
#define AUTH_TYPE_NOTIFICATION 0x03

// Priority Levels
#define PRIORITY_CRITICAL    0
#define PRIORITY_HIGH        1
#define PRIORITY_NORMAL      2
#define PRIORITY_LOW         3

// Message flags
#define MSG_FLAG_COMPRESSED  0x01
#define MSG_FLAG_BATCHED    0x02
#define MSG_FLAG_RELIABLE   0x04
#define MSG_FLAG_DELTA      0x08

// Basic message structure
typedef struct {
    uint8_t type;            // Message type
    uint8_t flags;           // Priority, compression, etc.
    uint16_t sequence;       // Sequence number
    uint16_t length;         // Payload length
    float x;                 // Position x
    float y;                 // Position y
    int clientId;            // Client identifier
    uint8_t payload[];       // Additional message data
} NetworkMessage;

// Delta update structure
typedef struct {
    uint32_t baseSequence;    // Base state sequence number
    uint8_t fieldMask;        // Bit mask of changed fields
    uint8_t deltaData[];      // Only changed fields
} DeltaUpdate;

// Batched message structure
typedef struct {
    uint8_t messageCount;
    struct {
        uint8_t type;
        uint16_t length;
        uint8_t data[];
    } messages[];
} BatchedMessage;

// Update frequency configuration
typedef struct {
    float updateInterval;     // Time between updates
    float distanceThreshold; // Distance threshold for updates
    uint8_t priority;        // Message priority
    bool requiresAck;        // Whether acknowledgment is required
} UpdateConfig;

// Add these player movement parameters
#define PLAYER_SPEED 5.0f
#define PLAYER_DAMPING 0.9f

typedef struct {
    b2BodyId physicsBody;  // Player physics body
    Vector2 position;      // Current position
    bool isAuthenticated;  // For future auth system
    int socket;
    bool isWebSocket; // Add this field
    int id;
    bool connected;
} Player;

// Add after Player struct:
typedef struct {
    uint32_t id;
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
    bool inUse;
    time_t lastSeen;
} PlayerConnection;

// Add temporary client structure for unverified connections
typedef struct {
    int socket;
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
    time_t connectTime;
    struct lws* wsi;         // WebSocket connection instance
    bool isWebSocket;        // Indicates if this is a WebSocket connection
    uint32_t id;            // Client ID
    // ... rest of PendingClient fields ...
} PendingClient;

typedef struct {
    int serverSocket;
    Player players[MAX_CLIENTS];
    int playerCount;
    fd_set activeSockets;
    struct sockaddr_in serverAddr;
    uint16_t currentSequence;
    float lastUpdateTime;
    UpdateConfig updateConfig;
    // Add more network state tracking as needed
    
    // Add new fields for connection tracking
    PlayerConnection connections[MAX_CLIENTS];
    uint32_t nextPlayerId;
    time_t connectionTimeout;  // How long before considering connection dead
    
    // Add pending clients for unverified connections
    PendingClient pendingClients[MAX_CLIENTS];
    int pendingCount;
    
    struct lws_context* wsContext;     // WebSocket context
    struct lws_vhost* vhost;          // WebSocket virtual host
    const struct lws_protocols* protocols;  // WebSocket protocols
} NetworkServer;

// Handshake message sent from server to new client
typedef struct {
    uint8_t type;        // MSG_TYPE_HANDSHAKE
    uint16_t clientId;   // Assigned client ID
    uint16_t protocol;   // Protocol version
    uint8_t maxPlayers;  // Maximum players allowed
    uint8_t numPlayers;  // Current player count
} HandshakeMessage;

// Auth message sent from client to server
typedef struct {
    uint8_t type;        // MSG_TYPE_AUTH
    char username[32];   // Client username
    uint16_t protocol;   // Client protocol version
} AuthMessage;

// Ready message sent from server to client
typedef struct {
    uint8_t type;        // MSG_TYPE_READY
    uint16_t worldSeed;  // World seed if needed
    float spawnX;        // Initial spawn position
    float spawnY;        // Initial spawn position
} ReadyMessage;

// World state update sent to clients
typedef struct {
    uint8_t type;           // MSG_TYPE_WORLD_STATE
    uint16_t sequence;      // Sequence number for ordering
    uint8_t playerCount;    // Number of players in update
    struct {
        uint16_t clientId;
        float x;
        float y;
        float rotation;
        uint8_t flags;     // Player state flags
    } players[];           // Flexible array member
} WorldStateMessage;

// Input message sent from client
typedef struct {
    uint8_t type;          // MSG_TYPE_ENTITY_UPDATE
    uint16_t sequence;     // Client sequence number
    uint8_t movementFlags; // Movement input flags
    float deltaTime;       // Client frame time
} InputMessage;

// Initialize the server
bool initServer(NetworkServer* server);

// Accept new client connections
void acceptNewClients(NetworkServer* server);

// Handle client messages
void handleNetworkMessages(NetworkServer* server, b2WorldId worldId, Camera2DState* camera);

// Send world state to all clients
void broadcastWorldState(NetworkServer* server, Camera2DState* camera);

// Cleanup network resources
void cleanupServer(NetworkServer* server);

// Add new functions
void createPlayerBody(NetworkServer* server, int playerIndex, b2WorldId worldId);
void updatePlayerPositions(NetworkServer* server, b2WorldId worldId);

// Add this function declaration
void updatePlayerMovement(Player* player, uint8_t movementFlags, float deltaTime);

// Function declarations
void sendNetworkMessage(NetworkServer* server, int clientId, NetworkMessage* msg);
void handleIncomingMessage(NetworkServer* server, int clientId, NetworkMessage* msg);
void processMessageBatch(NetworkServer* server, int clientId, BatchedMessage* batch);
void sendDeltaUpdate(NetworkServer* server, int clientId, DeltaUpdate* delta);

// Forward declarations
void handleNewConnection(NetworkServer* server, Player* player);
bool handleAuthMessage(NetworkServer* server, Player* player, AuthMessage* auth);

// Helper functions to send/receive these messages
void sendHandshake(NetworkServer* server, int clientId);
bool receiveAuth(NetworkServer* server, int clientId, AuthMessage* auth);
void sendReady(NetworkServer* server, int clientId, const ReadyMessage* ready);

// Auth server configuration
#define AUTH_SERVER_HOST "localhost"
#define AUTH_SERVER_PORT 3000
#define AUTH_ENDPOINT "/api/verify-token"
#define AUTH_VERIFY_ENDPOINT "/api/verify-token"
#define AUTH_TOKEN_HEADER "Content-Type: application/json"

// Auth server health check endpoint
#define AUTH_HEALTH_ENDPOINT "/health"

// Health check response structure
typedef struct {
    bool isHealthy;
    int64_t timestamp;
    struct {
        bool isConnected;
        int latencyMs;
        char error[128];
    } database;
    struct {
        char name[32];
        char version[16];
        int64_t uptime;
    } service;
    struct {
        uint64_t used;
        uint64_t total;
    } memory;
} AuthServerHealth;

// Health check function declarations
bool checkAuthServerHealth(AuthServerHealth* health);

// Auth response structure from auth server
typedef struct {
    bool success;        // Whether auth was successful
    int32_t playerid;   // Player ID from auth server 
    char username[32];  // Username 
    char token[64];     // Auth token for session
} AuthServerResponse;

// Updated auth response structure
typedef struct {
    bool status;         // true if "success"
    int32_t playerid;    // Assigned player ID from auth server
    char username[32];   // Username from auth server
} AuthVerifyResponse;

// Add new message structure
typedef struct {
    uint8_t type;        // MSG_TYPE_AUTH_PENDING
    uint32_t timeout;    // How long client should wait before retry (milliseconds)
} AuthPendingMessage;

// Function declarations for auth server communication
bool validateWithAuthServer(NetworkServer* server, Player* player, const char* username, const char* token);
bool verifyAuthToken(NetworkServer* server, const char* token);
void notifyAuthServer(NetworkServer* server, int playerId, const char* token);

// Add new function declarations
uint32_t allocatePlayerId(NetworkServer* server);
void freePlayerId(NetworkServer* server, uint32_t id);
PlayerConnection* findExistingConnection(NetworkServer* server, const char* ip, uint16_t port);
void cleanupStaleConnections(NetworkServer* server);

// Update function declarations
bool verifyClientToken(NetworkServer* server, int socket, const char* token, AuthVerifyResponse* response);
void handlePendingClient(NetworkServer* server, PendingClient* client, const char* token);

// WebSocket callback declarations
int callback_game_protocol(struct lws* wsi, enum lws_callback_reasons reason,
                         void* user, void* in, size_t len);

// Helper functions for WebSocket
void ws_send_binary(struct lws* wsi, void* data, size_t len);
void ws_broadcast_state(NetworkServer* server, void* data, size_t len);

// Add these declarations
void handleWebSocketMessage(NetworkServer* server, struct lws* wsi, void* data, size_t len);
void removeWebSocketClient(NetworkServer* server, struct lws* wsi);
PendingClient* findPendingClientById(NetworkServer* server, int id);

#endif // NETWORK_H
