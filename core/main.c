#include <box2d/box2d.h>
#include <raylib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "includes.h"
#include "../database/db_client.h"  // Add database client header

// Unified timing constants
#define PHYSICS_UPDATE_HZ 60        // Physics runs at 60Hz
#define VISUAL_UPDATE_HZ 1          // Visual updates at 1Hz
#define TARGET_FPS 60               // Target 60 FPS
#define PHYSICS_TIME_STEP (1.0f / PHYSICS_UPDATE_HZ)
#define VISUAL_TIME_STEP (1.0f / VISUAL_UPDATE_HZ)
#define DB_HEALTH_CHECK_INTERVAL 10.0  // Check every 10 seconds instead of 5

// Update game camera based on input
void UpdateGameCamera(Camera2DState* camera) {
    // Zoom with mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        camera->zoom *= (1.0f + wheel * 0.1f);
        if (camera->zoom < 0.1f) camera->zoom = 0.1f;
        if (camera->zoom > 10.0f) camera->zoom = 10.0f;
    }

    // Pan with middle mouse button
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        camera->isDragging = true;
        camera->dragStart = GetMousePosition();
    }
    
    if (camera->isDragging) {
        Vector2 currentPos = GetMousePosition();
        Vector2 delta = {
            currentPos.x - camera->dragStart.x,
            currentPos.y - camera->dragStart.y
        };
        camera->offset.x += delta.x;
        camera->offset.y += delta.y;
        camera->dragStart = currentPos;
    }
    
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
        camera->isDragging = false;
    }
}

void DrawPhysicsBox(Vector2 center, float angle, Vector2 size, Color color, const Camera2DState* camera) {
    float width = size.x * PIXELS_PER_METER * camera->zoom;
    float height = size.y * PIXELS_PER_METER * camera->zoom;
    
    Rectangle rect = {
        center.x,
        center.y,
        width * 2,
        height * 2
    };
    
    Vector2 origin = {width, height};
    DrawRectanglePro(rect, origin, angle * RAD2DEG, color);
    DrawCircleV(center, 3.0f, RED);
}

// Replace the DrawPhysicsGrid function
void DrawPhysicsGrid(float spacing, const Camera2DState* camera) {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    int centerX = screenWidth / 2;
    int centerY = screenHeight / 2;
    
    // Calculate grid spacing for 1000 unit increments
    float zoomedSpacing = 1000.0f * camera->zoom;
    Color subGridColor = Fade(GRAY, 0.2f);
    Color textColor = Fade(DARKGRAY, 0.5f);
    
    // Calculate how many grid lines we need in each direction
    int numLinesX = (screenWidth / zoomedSpacing) + 2;
    int numLinesY = (screenHeight / zoomedSpacing) + 2;
    
    // Calculate where the (0,0) point is on screen
    float originX = centerX + camera->offset.x;
    float originY = centerY + camera->offset.y;
    
    // Draw vertical grid lines
    for (int i = -numLinesX/2; i <= numLinesX/2; i++) {
        float x = originX + (i * zoomedSpacing);
        if (x >= 0 && x <= screenWidth) {
            DrawLineV((Vector2){x, 0}, (Vector2){x, screenHeight}, subGridColor);
            // Draw coordinate label (shows actual world coordinates)
            int worldX = (int)(i * 1000); // Each line represents 1000 units
            DrawText(TextFormat("%d", worldX), x + 5, originY + 5, 20, textColor);
        }
    }
    
    // Draw horizontal grid lines
    for (int i = -numLinesY/2; i <= numLinesY/2; i++) {
        float y = originY + (i * zoomedSpacing);
        if (y >= 0 && y <= screenHeight) {
            DrawLineV((Vector2){0, y}, (Vector2){screenWidth, y}, subGridColor);
            // Draw coordinate label (shows actual world coordinates)
            int worldY = (int)(-i * 1000); // Negative because Y is inverted in screen space
            DrawText(TextFormat("%d", worldY), originX + 5, y + 5, 20, textColor);
        }
    }
    
    // Draw main axes with higher alpha
    DrawLineEx((Vector2){0, originY}, 
               (Vector2){screenWidth, originY}, 
               2.0f, Fade(GRAY, 0.9f));
    DrawLineEx((Vector2){originX, 0},
               (Vector2){originX, screenHeight},
               2.0f, Fade(GRAY, 0.9f));
    
    // Draw origin marker
    DrawText("(0,0)", originX + 10, originY + 10, 20, RED);
}

// Add struct to store interpolation data

void initShipArray(ShipArray* array, int initialCapacity) {
    array->ships = (Ship*)malloc(initialCapacity * sizeof(Ship));
    array->capacity = initialCapacity;
    array->count = 0;
}

void addShip(ShipArray* array, Ship ship) {
    if (array->count >= array->capacity) {
        array->capacity *= 2;
        array->ships = (Ship*)realloc(array->ships, array->capacity * sizeof(Ship));
    }
    array->ships[array->count] = ship;
    array->count++;
}

// Add this function before main():
void updateShipPositions(b2WorldId worldId, Camera2DState* camera) {
    for (int i = 0; i < camera->ships.count; i++) {
        Ship* ship = &camera->ships.ships[i];
        b2Vec2 pos = b2Body_GetPosition(ship->id);
        b2Rot rot = b2Body_GetRotation(ship->id);
        Vector2 screenPos = physicsToScreen(pos, camera);
        
        // Update stored positions
        ship->screenPos = screenPos;
        ship->physicsPos = pos;
        
        float angle = atan2f(rot.s, rot.c);
        
        // Now properly declared in ship_shapes.h
        DrawShipHull(screenPos, angle, BLUE, camera);
    }
}

// Add admin commands
typedef enum {
    CMD_NONE,
    CMD_LIST_SHIPS,
    CMD_ADD_SHIP,
    CMD_DELETE_SHIP,
    CMD_HELP
} AdminCommand;

// Add ship management functions
void printShipList(const ShipArray* ships) {
    printf("\n--- Ships List ---\n");
    for (int i = 0; i < ships->count; i++) {
        Ship* ship = &ships->ships[i];
        printf("Ship %d: Pos(%.1f, %.1f)\n", 
               i, ship->physicsPos.x, ship->physicsPos.y);
    }
    printf("----------------\n");
}

void printAdminHelp() {
    printf("\nCommands:\n");
    printf("L - List all ships\n");
    printf("A - Add ship (follow with x y coordinates)\n");
    printf("D - Delete ship (follow with ship number)\n");
    printf("H - Show this help\n");
}

// Add debug logging function
void logDebug(const char* format, ...) {
    time_t now;
    time(&now);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0';  // Remove newline
    
    va_list args;
    va_start(args, format);
    printf("[%s] DEBUG: ", timestamp);
    vprintf(format, args);
    printf("\n");
    fflush(stdout);  // Ensure output is written immediately
    va_end(args);
}

// Rename from GameServer to DatabaseState to avoid conflict
typedef struct {
    DatabaseClient dbClient;
    time_t lastHealthCheck;
    bool isDbHealthy;
    DatabaseHealth dbHealth;
} DatabaseState;

// Add this before main()
void updateDatabaseState(DatabaseState* dbState, DatabaseClient* client) {
    if (!dbState || !client) return;

    // Check basic connection state
    bool wasHealthy = dbState->isDbHealthy;
    bool isConnected = client->state == CONN_STATE_CONNECTED && 
                      client->auth_success && 
                      client->net.connected;

    // Verify connection is actually working
    if (isConnected) {
        // Send a quick probe message
        MessageHeader probe = {0};
        probe.type = MSG_PING;
        probe.version = MESSAGE_VERSION;
        probe.sequence = client->sequence++;
        
        if (send(client->net.sock, &probe, sizeof(probe), MSG_NOSIGNAL) < 0) {
            fprintf(stderr, "Connection verification failed: %s\n", strerror(errno));
            dbState->isDbHealthy = false;
            return;
        }
        
        // Connection appears valid
        dbState->isDbHealthy = true;
    } else {
        dbState->isDbHealthy = false;
    }

    // Log state changes
    if (dbState->isDbHealthy != wasHealthy) {
        logDebug("Database connection state changed: %s -> %s",
                wasHealthy ? "healthy" : "unhealthy",
                dbState->isDbHealthy ? "healthy" : "unhealthy");
    }
}

int main() {
    logDebug("Starting Game Dashboard initialization...");
    
    // Initialize basic systems first
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 720, "Game Dashboard");
    SetTargetFPS(TARGET_FPS);
    
    // Create physics world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, 0.0f};
    worldDef.enableSleep = false;
    b2WorldId worldId = b2CreateWorld(&worldDef);
    logDebug("Core systems initialized");

    // Initialize visual components
    Camera2DState camera = {0};
    camera.zoom = 1.0f;
    initShipArray(&camera.ships, 10);
    
    AdminConsole adminConsole;
    initAdminConsole(&adminConsole, worldId, &camera.ships);
    startAdminConsoleThread(&adminConsole);
    
    AdminWindow adminWindow;
    initAdminWindow(&adminWindow, worldId, &camera.ships, &camera);
    logDebug("Visual components initialized");

    // Get executable path and workspace directory
    char exe_path[PATH_MAX];
    char workspace_path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", exe_path, PATH_MAX - 1);
    if (count != -1) {
        exe_path[count] = '\0';  // Ensure null termination
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';  // Remove executable name
            char* build_dir = strstr(exe_path, "/build");
            if (build_dir) {
                *build_dir = '\0';  // Remove /build from path
            }
            strncpy(workspace_path, exe_path, PATH_MAX - 1);
            workspace_path[PATH_MAX - 1] = '\0';  // Ensure null termination
        }
    }

    // Construct full path to .env file with bounds checking
    char env_path[PATH_MAX];
    size_t base_len = strlen(workspace_path);
    if (base_len + 6 > PATH_MAX) {  // 6 = strlen("/.env") + 1
        logDebug("ERROR: Path too long for .env file");
        return -1;
    }
    memcpy(env_path, workspace_path, base_len);
    memcpy(env_path + base_len, "/.env", 6);  // Includes null terminator

    logDebug("Looking for .env at: %s", env_path);

    // Load environment variables with full path
    if (!loadEnvFile(env_path)) {
        logDebug("Warning: Failed to load .env file at %s, falling back to environment variables", env_path);
        // Check if we're in development mode
        if (strcmp(getEnvOrDefault("ENV", "dev"), "production") == 0) {
            logDebug("ERROR: Missing .env file in production mode");
            return -1;
        }
    } else {
        logDebug("Successfully loaded .env file");
    }

    // Get all required configuration from environment
    const char* server_id = getEnvOrDefault("GAME_SERVER_ID", NULL);
    const char* server_token = getEnvOrDefault("GAME_SERVER_TOKEN", NULL);
    const char* auth_host = getEnvOrDefault("AUTH_SERVER_HOST", "localhost");
    // Remove auth port from environment variables since it's fixed
    const char* game_port_str = getEnvOrDefault("GAME_SERVER_PORT", "8080");
    int game_port = atoi(game_port_str);

    if (!server_id || !server_token) {
        logDebug("ERROR: Required environment variables GAME_SERVER_ID and GAME_SERVER_TOKEN must be set");
        logDebug("Please copy .env.example to .env and configure with your credentials");
        return -1;
    }

    // Initialize database client in background
    DatabaseState dbState = {0};
    dbState.lastHealthCheck = 0;
    dbState.isDbHealthy = false;

    if (!db_client_init(&dbState.dbClient, auth_host, 3001, server_id, server_token)) {
        logDebug("Warning: Failed to initialize database connection - continuing in offline mode");
        // Continue without database connection
    }

    // Initialize player manager but don't require database connection
    PlayerConnectionManager playerManager = {0};
    playerManager.worldId = worldId;
    playerManager.db_client = &dbState.dbClient;
    
    // Start WebSocket server but don't accept connections until database is ready
    if (!ws_start_server(NULL, game_port)) {
        logDebug("Warning: Failed to start WebSocket server - player connections disabled");
    } else {
        logDebug("WebSocket server started (waiting for database connection)");
    }

    // Add performance tracking variables
    double lastFrameTime = GetTime();
    int frameCount = 0;
    float lastCameraZoom = 1.0f;

    // Main game loop
    double lastPhysicsUpdate = GetTime();
    double lastVisualUpdate = GetTime();
    logDebug("Entering main loop - Dashboard active, waiting for database connection");
    
    while (!WindowShouldClose()) {
        double currentTime = GetTime();
        
        // Process database messages and maintain connection
        if (dbState.dbClient.auth_success) {
            // Always process messages first
            if (!db_client_process_messages(&dbState.dbClient)) {
                dbState.isDbHealthy = false;
            }

            // Check if it's time to send a ping
            time_t now = time(NULL);
            if (now - dbState.dbClient.ping_state.last_successful > PING_RETRY_INTERVAL_MS/1000) {
                fprintf(stderr, "Time to send ping (last success: %ld, now: %ld)\n",
                        dbState.dbClient.ping_state.last_successful, now);
                if (!db_client_ping(&dbState.dbClient)) {
                    dbState.isDbHealthy = false;
                }
            }
        }

        // Try to establish database connection if not connected
        if (!dbState.isDbHealthy && !dbState.dbClient.is_reconnecting && 
            (currentTime - dbState.lastHealthCheck >= DB_HEALTH_CHECK_INTERVAL)) {
            
            if (db_client_ensure_connected(&dbState.dbClient)) {
                updateDatabaseState(&dbState, &dbState.dbClient);
                if (dbState.isDbHealthy) {
                    logDebug("Database connection established - player connections enabled");
                }
            }
            dbState.lastHealthCheck = currentTime;
        }

        // Only handle player connections if database is healthy
        if (dbState.isDbHealthy && ws_has_pending_connections()) {
            const char* token = ws_get_connect_token();
            WebSocket* ws = ws_accept_connection();
            
            if (!handleNewPlayerConnection(&playerManager, token, ws)) {
                logDebug("Rejected player connection - invalid token");
                ws_disconnect(ws);
                free(ws);
            }
        }

        // Rest of game loop continues normally
        UpdateGameCamera(&camera);
        
        if (currentTime - lastPhysicsUpdate >= PHYSICS_TIME_STEP) {
            b2World_Step(worldId, PHYSICS_TIME_STEP, 1);
            lastPhysicsUpdate = currentTime;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        
        // Draw status banner if database is not connected
        if (!dbState.isDbHealthy) {
            DrawRectangle(0, 0, GetScreenWidth(), 30, ColorAlpha(RED, 0.8f));
            DrawText("DATABASE OFFLINE - Player connections disabled", 
                    10, 5, 20, WHITE);
        }

        DrawPhysicsGrid(50.0f, &camera);
        DrawText("Server Dashboard", 10, 10, 20, BLACK);
        
        // Draw ships and update game state even if database is down
        updateShipPositions(worldId, &camera);
        
        // Draw database connection status
        // const char* dbStatus = "DB: ";
        // const char* dbStatusDetail;
        // Color statusColor;
        
        // if (dbState.dbClient.is_reconnecting) {
        //     dbStatusDetail = "RECONNECTING";
        //     statusColor = YELLOW;
        // } else if (dbState.isDbHealthy) {
        //     dbStatusDetail = "CONNECTED";
        //     statusColor = GREEN;
        // } else {
        //     dbStatusDetail = "DISCONNECTED";
        //     statusColor = RED;
        // }
        
        // DrawText(TextFormat("%s%s", dbStatus, dbStatusDetail),
        //         10, GetScreenHeight() - 30, 20, statusColor);

        // Draw extra database info if disconnected
        if (!dbState.isDbHealthy) {
            DrawText("Database offline - Game continuing in limited mode", 
                    10, GetScreenHeight() - 60, 20, YELLOW);
        }
        
        // Update and draw admin panel
        if (IsKeyPressed(KEY_TAB)) {
            adminWindow.isOpen = !adminWindow.isOpen;
            logDebug("Admin panel visibility toggled: %d", adminWindow.isOpen);
        }

        if (adminWindow.isOpen) {
            updateAdminWindow(&adminWindow);
        }

        // Draw database status indicator
        DrawText(dbState.isDbHealthy ? "DB: OK" : "DB: ERROR",
                10, GetScreenHeight() - 30, 20,
                dbState.isDbHealthy ? GREEN : RED);
        
        EndDrawing();
        
        // Log performance stats
        if (currentTime - lastFrameTime >= 5.0) {
            double fps = frameCount / (currentTime - lastFrameTime);
            frameCount = 0;
            lastFrameTime = currentTime;
        }

        // Log any significant state changes
        if (camera.zoom != lastCameraZoom) {
            logDebug("Camera zoom changed: %.2f -> %.2f", lastCameraZoom, camera.zoom);
            lastCameraZoom = camera.zoom;
        }
    }

    logDebug("Cleaning up...");
    cleanupPlayerConnectionManager(&playerManager);
    closeAdminWindow(&adminWindow);
    stopAdminConsole(&adminConsole);
    // db_client_cleanup(&dbState.dbClient);
    b2DestroyWorld(worldId);
    ws_stop_server();
    CloseWindow();
    logDebug("Shutdown complete");
    
    return 0;
}
