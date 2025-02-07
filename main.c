#include <box2d/box2d.h>
#include <raylib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "ship_shapes.h"
#include "game_state.h"
#include <signal.h>
#include "admin_console.h"
#include "admin_window.h"
#include <time.h>
#include "coord_utils.h"

// Unified timing constants
#define PHYSICS_UPDATE_HZ 60        // Physics runs at 60Hz
#define VISUAL_UPDATE_HZ 1          // Visual updates at 1Hz
#define TARGET_FPS 60               // Target 60 FPS
#define PHYSICS_TIME_STEP (1.0f / PHYSICS_UPDATE_HZ)
#define VISUAL_TIME_STEP (1.0f / VISUAL_UPDATE_HZ)


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
    array->renderStates = malloc(initialCapacity * sizeof(ShipRenderState));
    array->capacity = initialCapacity;
    array->count = 0;
    
    // Initialize render states
    for (int i = 0; i < initialCapacity; i++) {
        array->renderStates[i] = (ShipRenderState){
            .lastPos = (Vector2){0, 0},
            .currentPos = (Vector2){0, 0},
            .lastAngle = 0,
            .currentAngle = 0,
            .updateTime = 0
        };
    }
}

void addShip(ShipArray* array, Ship ship) {
    if (array->count >= array->capacity) {
        array->capacity *= 2;
        array->ships = (Ship*)realloc(array->ships, array->capacity * sizeof(Ship));
        array->renderStates = realloc(array->renderStates, 
                                    array->capacity * sizeof(ShipRenderState));
    }
    array->ships[array->count] = ship;
    
    // Initialize render state
    array->renderStates[array->count] = (ShipRenderState){
        .lastPos = ship.screenPos,
        .currentPos = ship.screenPos,
        .lastAngle = 0,
        .currentAngle = 0,
        .updateTime = GetTime()
    };
    
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

int main() {
    logDebug("Starting Game Dashboard initialization...");
    
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    logDebug("Config flags set");
    
    InitWindow(1280, 720, "Game Dashboard");
    logDebug("Window initialized");
    SetTargetFPS(TARGET_FPS);
    
    // Create physics world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, 0.0f};
    worldDef.enableSleep = false;
    b2WorldId worldId = b2CreateWorld(&worldDef);
    logDebug("Physics world created: %d", worldId);

    // Initialize camera and ships
    Camera2DState camera = {0};
    camera.zoom = 1.0f;
    initShipArray(&camera.ships, 10);
    logDebug("Camera and ship array initialized");

    // Initialize admin console
    AdminConsole adminConsole;
    initAdminConsole(&adminConsole, worldId, &camera.ships);
    logDebug("Admin console initialized");
    startAdminConsoleThread(&adminConsole);
    logDebug("Admin console thread started");

    // Initialize admin window
    AdminWindow adminWindow;
    initAdminWindow(&adminWindow, worldId, &camera.ships, &camera);
    logDebug("Admin window initialized");

    double lastPhysicsUpdate = GetTime();
    double lastVisualUpdate = GetTime();
    Vector2 lastCameraOffset = {0};
    float lastCameraZoom = 1.0f;
    
    logDebug("Entering main loop");
    int frameCount = 0;
    double lastFrameTime = GetTime();
    
    while (!WindowShouldClose()) {
        double currentTime = GetTime();
        frameCount++;
        
        // Log performance stats every 5 seconds
        if (currentTime - lastFrameTime >= 5.0) {
            double fps = frameCount / (currentTime - lastFrameTime);
            logDebug("FPS: %.1f, Ships: %d, Camera zoom: %.2f", 
                    fps, camera.ships.count, camera.zoom);
            frameCount = 0;
            lastFrameTime = currentTime;
        }

        // Log any significant state changes
        if (camera.zoom != lastCameraZoom) {
            logDebug("Camera zoom changed: %.2f -> %.2f", lastCameraZoom, camera.zoom);
            lastCameraZoom = camera.zoom;
        }

        UpdateGameCamera(&camera);

        // Physics updates
        if (currentTime - lastPhysicsUpdate >= PHYSICS_TIME_STEP) {
            b2World_Step(worldId, PHYSICS_TIME_STEP, 1);
            lastPhysicsUpdate = currentTime;
        }

        // Visual update at 1Hz
        if (currentTime - lastVisualUpdate >= VISUAL_TIME_STEP) {
            for (int i = 0; i < camera.ships.count; i++) {
                Ship* ship = &camera.ships.ships[i];
                ShipRenderState* renderState = &camera.ships.renderStates[i];
                
                if (b2Body_IsValid(ship->id)) {
                    // Store last position
                    renderState->lastPos = renderState->currentPos;
                    renderState->lastAngle = renderState->currentAngle;
                    
                    // Update to new position
                    b2Vec2 pos = b2Body_GetPosition(ship->id);
                    b2Rot rot = b2Body_GetRotation(ship->id);
                    
                    ship->physicsPos = pos;
                    ship->screenPos = physicsToScreen(pos, &camera);
                    renderState->currentPos = ship->screenPos;
                    renderState->currentAngle = atan2f(rot.s, rot.c);
                    renderState->updateTime = currentTime;
                }
            }
            lastVisualUpdate = currentTime;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        
        // Draw grid first (background layer)
        DrawPhysicsGrid(50.0f, &camera);
        
        // Draw UI elements that should be behind ships
        DrawText("Server Dashboard", 10, 10, 20, BLACK);
        
        // Interpolate and draw ships
        for (int i = 0; i < camera.ships.count; i++) {
            Ship* ship = &camera.ships.ships[i];
            ShipRenderState* renderState = &camera.ships.renderStates[i];
            
            if (b2Body_IsValid(ship->id)) {
                // Calculate interpolation factor
                float t = fminf(1.0f, (float)(currentTime - renderState->updateTime) / VISUAL_TIME_STEP);
                
                // Interpolate position and angle
                Vector2 interpolatedPos = {
                    renderState->lastPos.x + (renderState->currentPos.x - renderState->lastPos.x) * t,
                    renderState->lastPos.y + (renderState->currentPos.y - renderState->lastPos.y) * t
                };
                
                float interpolatedAngle = renderState->lastAngle + 
                    (renderState->currentAngle - renderState->lastAngle) * t;
                
                DrawShipHull(interpolatedPos, interpolatedAngle, BLUE, &camera);
            }
        }
        
        // Draw ships on top of grid
        for (int i = 0; i < camera.ships.count; i++) {
            Ship* ship = &camera.ships.ships[i];
            if (b2Body_IsValid(ship->id)) {
                b2Vec2 pos = b2Body_GetPosition(ship->id);
                b2Rot rot = b2Body_GetRotation(ship->id);
                ship->physicsPos = pos;
                ship->screenPos = physicsToScreen(pos, &camera);
                float angle = atan2f(rot.s, rot.c);
                
                // Add debug output for ship positions
                logDebug("Drawing ship at screen pos=(%.2f, %.2f) physics pos=(%.2f, %.2f)", 
                        ship->screenPos.x, ship->screenPos.y, pos.x, pos.y);
                        
                DrawShipHull(ship->screenPos, angle, BLUE, &camera);
            }
        }
        
        // Draw admin panel last (top layer)
        if (adminWindow.isOpen) {
            updateAdminWindow(&adminWindow);
        }
        
        EndDrawing();
        
        // Handle input outside of drawing
        if (IsKeyPressed(KEY_TAB)) {
            adminWindow.isOpen = !adminWindow.isOpen;
        }
    }

    logDebug("Cleaning up...");
    closeAdminWindow(&adminWindow);
    stopAdminConsole(&adminConsole);
    b2DestroyWorld(worldId);
    CloseWindow();
    logDebug("Shutdown complete");
    
    return 0;
}
