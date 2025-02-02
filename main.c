#include <box2d/box2d.h>
#include <raylib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "ship_shapes.h"

// GUI Button structure
typedef struct {
    Rectangle bounds;
    const char* text;
    Color color;
    Color hoverColor;
    bool isHovered;
} GuiButton;

// Add after the GuiButton structure
typedef struct {
    b2BodyId id;
    Vector2 screenPos;
    b2Vec2 physicsPos;
} Ship;

typedef struct {
    Ship* ships;
    int capacity;
    int count;
} ShipArray;

// Create a button
GuiButton CreateButton(float x, float y, float width, float height, const char* text, Color color) {
    return (GuiButton){
        .bounds = (Rectangle){x, y, width, height},
        .text = text,
        .color = color,
        .hoverColor = ColorBrightness(color, 0.2f),
        .isHovered = false
    };
}

// Update and draw button, returns true if clicked
bool GuiButtonUpdate(GuiButton* button) {
    Vector2 mousePoint = GetMousePosition();
    button->isHovered = CheckCollisionPointRec(mousePoint, button->bounds);
    
    // Draw button
    DrawRectangleRec(button->bounds, button->isHovered ? button->hoverColor : button->color);
    DrawRectangleLinesEx(button->bounds, 2, BLACK);
    
    // Center text
    int textWidth = MeasureText(button->text, 20);
    Vector2 textPos = {
        button->bounds.x + (button->bounds.width - textWidth) / 2,
        button->bounds.y + (button->bounds.height - 20) / 2
    };
    DrawText(button->text, textPos.x, textPos.y, 20, BLACK);
    
    return button->isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Conversion factors between physics and screen coordinates
#define PIXELS_PER_METER 1.0f
#define METER_PER_PIXEL 1.0f

// Camera state
typedef struct {
    Vector2 target;
    float zoom;
    Vector2 offset;
    Vector2 dragStart;
    bool isDragging;
    int shipsCreated;  // Add this to Camera2DState
    bool isPlacingShip;
    Vector2 placementPreview;
    ShipArray ships;  // Add this new field
} Camera2DState;

// Convert Box2D coordinates to screen coordinates with camera transformation
Vector2 physicsToScreen(b2Vec2 position, const Camera2DState* camera) {
    float screenX = (position.x * PIXELS_PER_METER * camera->zoom) + camera->offset.x + GetScreenWidth() / 2;
    float screenY = GetScreenHeight() / 2 - (position.y * PIXELS_PER_METER * camera->zoom) + camera->offset.y;
    return (Vector2){ screenX, screenY };
}

// Convert screen coordinates to physics coordinates
b2Vec2 screenToPhysics(Vector2 screenPos, const Camera2DState* camera) {
    float physX = ((screenPos.x - GetScreenWidth() / 2 - camera->offset.x) / camera->zoom) * METER_PER_PIXEL;
    float physY = ((GetScreenHeight() / 2 - screenPos.y + camera->offset.y) / camera->zoom) * METER_PER_PIXEL;
    return (b2Vec2){ physX, physY };
}

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

void DrawPhysicsGrid(float spacing, const Camera2DState* camera) {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    int centerX = screenWidth / 2;
    int centerY = screenHeight / 2;
    
    // Draw center lines
    DrawLine(0, centerY, screenWidth, centerY, Fade(GRAY, 0.5f));  // X axis
    DrawLine(centerX, 0, centerX, screenHeight, Fade(GRAY, 0.5f)); // Y axis
    
    // Draw grid with larger spacing for better visibility
    float gridSpacing = spacing * 50.0f * camera->zoom;  // Increase grid spacing for readability
    for(float i = gridSpacing; i < screenWidth/2; i += gridSpacing) {
        // Vertical lines
        DrawLine(centerX + i + camera->offset.x, 0, centerX + i + camera->offset.x, screenHeight, Fade(LIGHTGRAY, 0.3f));
        DrawLine(centerX - i + camera->offset.x, 0, centerX - i + camera->offset.x, screenHeight, Fade(LIGHTGRAY, 0.3f));
        
        // Horizontal lines
        DrawLine(0, centerY + i + camera->offset.y, screenWidth, centerY + i + camera->offset.y, Fade(LIGHTGRAY, 0.3f));
        DrawLine(0, centerY - i + camera->offset.y, screenWidth, centerY - i + camera->offset.y, Fade(LIGHTGRAY, 0.3f));
        
        // Draw coordinates (showing every 50 meters)
        DrawText(TextFormat("%.0f", i * METER_PER_PIXEL), centerX + i + camera->offset.x, centerY + camera->offset.y, 10, GRAY);
        DrawText(TextFormat("%.0f", -i * METER_PER_PIXEL), centerX - i + camera->offset.x, centerY + camera->offset.y, 10, GRAY);
        DrawText(TextFormat("%.0f", -i * METER_PER_PIXEL), centerX + camera->offset.x, centerY + i + camera->offset.y, 10, GRAY);
        DrawText(TextFormat("%.0f", i * METER_PER_PIXEL), centerX + camera->offset.x, centerY - i + camera->offset.y, 10, GRAY);
    }
}

// Add function to draw ship shape
void DrawShipHull(Vector2 center, float angle, Color color, const Camera2DState* camera) {
    // Convert ship vertices to screen space
    b2Hull hull = createShipHullShape();
    Vector2 points[6];
    
    for (int i = 0; i < hull.count; i++) {
        // Rotate point
        float cs = cosf(angle);
        float sn = sinf(angle);
        float px = hull.points[i].x * cs - hull.points[i].y * sn;
        float py = hull.points[i].x * sn + hull.points[i].y * cs;
        
        // Scale and translate to screen space
        points[i] = (Vector2){
            (px / SHIP_SCALE) * camera->zoom + center.x,
            (py / SHIP_SCALE) * camera->zoom + center.y
        };
    }
    
    // Draw ship hull
    DrawLineStrip(points, hull.count, color);
    DrawLineV(points[hull.count-1], points[0], color);
    
    // Draw center point
    DrawCircleV(center, 3.0f, RED);
}

// Add before main():
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
    array->ships[array->count++] = ship;
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
        
        // Convert b2Rot to angle in radians for DrawShipHull
        // b2Rot contains cos/sin values, use atan2 to get the angle
        float angle = atan2f(rot.s, rot.c);
        
        // Draw the ship
        DrawShipHull(screenPos, angle, BLUE, camera);
    }
}

int main() {
    // Initialize window
    InitWindow(1280, 920, "Box2D with Raylib");
    SetTargetFPS(60);

    // Create the world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, 0.0f};
    b2WorldId worldId = b2CreateWorld(&worldDef);

    // Simulation parameters
    float timeStep = 1.0f / 60.0f;
    int subStepCount = 8;

    // Initialize camera with ship counter
    Camera2DState camera = {
        .target = {0, 0},
        .zoom = 1.0f,
        .offset = {0, 0},
        .isDragging = false,
        .shipsCreated = 0,
        .isPlacingShip = false,
        .placementPreview = (Vector2){0, 0},
        .ships = {NULL, 0, 0}
    };
    initShipArray(&camera.ships, 10);

    // Create GUI buttons
    GuiButton createShipButton = CreateButton(10, 150, 200, 40, "Create Ship", SKYBLUE);
    
    // Main game loop
    while (!WindowShouldClose()) {
        // Update camera
        UpdateGameCamera(&camera);

        // Handle ship placement
        if (GuiButtonUpdate(&createShipButton)) {
            camera.isPlacingShip = true;
        }

        // Update ship placement preview
        if (camera.isPlacingShip) {
            camera.placementPreview = GetMousePosition();
            
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (!CheckCollisionPointRec(camera.placementPreview, createShipButton.bounds)) {
                    b2Vec2 physicsPos = screenToPhysics(camera.placementPreview, &camera);
                    b2BodyId newShipId = createShipHull(worldId, physicsPos.x, physicsPos.y, b2MakeRot(0.0f));
                    
                    Ship newShip = {
                        .id = newShipId,
                        .screenPos = camera.placementPreview,
                        .physicsPos = physicsPos
                    };
                    addShip(&camera.ships, newShip);
                    
                    camera.shipsCreated++;
                    camera.isPlacingShip = false;
                    
                    printf("Ship created (#%d):\n", camera.shipsCreated);
                    printf("  Screen pos: (%.2f, %.2f)\n", camera.placementPreview.x, camera.placementPreview.y);
                    printf("  Physics pos: (%.2f, %.2f)\n", physicsPos.x, physicsPos.y);
                    printf("  Body ID: %d\n", newShipId.index1);
                }
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                camera.isPlacingShip = false;
            }
        }

        // Update physics
        b2World_Step(worldId, timeStep, subStepCount);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Draw grid
        DrawPhysicsGrid(PIXELS_PER_METER, &camera);
        
        // Draw all ships
        updateShipPositions(worldId, &camera);

        // Draw UI elements
        DrawFPS(10, 10);
        DrawText(TextFormat("Zoom: %.2f", camera.zoom), 10, 110, 20, DARKGRAY);
        DrawText("Controls:", 10, 130, 20, DARKGRAY);
        DrawText(TextFormat("Ships Created: %d", camera.shipsCreated), 10, 200, 20, DARKGRAY);

        // Draw placement preview
        if (camera.isPlacingShip) {
            DrawShipHull(camera.placementPreview, 0.0f, Fade(GREEN, 0.5f), &camera);
            DrawText("Click to place ship, right click to cancel", 10, 220, 20, DARKGRAY);
        }

        // Draw ship coordinates
        for (int i = 0; i < camera.ships.count; i++) {
            Ship* ship = &camera.ships.ships[i];
            DrawText(TextFormat("Ship %d: Screen(%.1f, %.1f) Physics(%.1f, %.1f)",
                i + 1,
                ship->screenPos.x,
                ship->screenPos.y,
                ship->physicsPos.x,
                ship->physicsPos.y),
                10, 240 + i * 20, 20, DARKGRAY);
        }

        EndDrawing();
    }

    // Cleanup
    b2DestroyWorld(worldId);
    free(camera.ships.ships);
    CloseWindow();
    return 0;
}
