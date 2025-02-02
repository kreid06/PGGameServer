#include <box2d/box2d.h>
#include <raylib.h>
#include <stdio.h>

// Conversion factors between physics and screen coordinates
#define PIXELS_PER_METER 50.0f
#define METER_PER_PIXEL (1.0f / PIXELS_PER_METER)

// Convert Box2D coordinates to screen coordinates
Vector2 physicsToScreen(b2Vec2 position) {
    return (Vector2){
        position.x * PIXELS_PER_METER + GetScreenWidth() / 2,
        GetScreenHeight() / 2 - position.y * PIXELS_PER_METER
    };
}

void DrawPhysicsBox(Vector2 center, float angle, Vector2 size, Color color) {
    // Convert physics size to screen size (in pixels)
    float width = size.x * PIXELS_PER_METER;
    float height = size.y * PIXELS_PER_METER;
    
    Rectangle rect = {
        center.x,     // Use center directly
        center.y,     // Use center directly
        width * 2,    // Full width
        height * 2    // Full height
    };
    
    // Origin at center for rotation
    Vector2 origin = {width, height};
    
    // Draw the rectangle
    DrawRectanglePro(rect, origin, angle * RAD2DEG, color);
    
    // Draw center dot
    DrawCircleV(center, 3.0f, RED);
}

void DrawPhysicsGrid(float spacing) {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    int centerX = screenWidth / 2;
    int centerY = screenHeight / 2;
    
    // Draw center lines
    DrawLine(0, centerY, screenWidth, centerY, Fade(GRAY, 0.5f));  // X axis
    DrawLine(centerX, 0, centerX, screenHeight, Fade(GRAY, 0.5f)); // Y axis
    
    // Draw grid
    for(float i = spacing; i < screenWidth/2; i += spacing) {
        // Vertical lines
        DrawLine(centerX + i, 0, centerX + i, screenHeight, Fade(LIGHTGRAY, 0.3f));
        DrawLine(centerX - i, 0, centerX - i, screenHeight, Fade(LIGHTGRAY, 0.3f));
        
        // Horizontal lines
        DrawLine(0, centerY + i, screenWidth, centerY + i, Fade(LIGHTGRAY, 0.3f));
        DrawLine(0, centerY - i, screenWidth, centerY - i, Fade(LIGHTGRAY, 0.3f));
        
        // Draw coordinates
        DrawText(TextFormat("%.1f", i * METER_PER_PIXEL), centerX + i, centerY, 10, GRAY);
        DrawText(TextFormat("%.1f", -i * METER_PER_PIXEL), centerX - i, centerY, 10, GRAY);
        DrawText(TextFormat("%.1f", -i * METER_PER_PIXEL), centerX, centerY + i, 10, GRAY);
        DrawText(TextFormat("%.1f", i * METER_PER_PIXEL), centerX, centerY - i, 10, GRAY);
    }
}

int main() {
    // Initialize window
    InitWindow(1280, 920, "Box2D with Raylib");
    SetTargetFPS(60);

    // Create the world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, -10.0f};
    b2WorldId worldId = b2CreateWorld(&worldDef);

    // Create ground body
    b2BodyDef groundBodyDef = b2DefaultBodyDef();
    groundBodyDef.position = (b2Vec2){0.0f, -10.0f};
    b2BodyId groundId = b2CreateBody(worldId, &groundBodyDef);
    
    // Create ground box (adjust size since b2MakeBox doubles the dimensions)
    b2Polygon groundBox = b2MakeBox(5.0f, 5.0f);  // This creates a 10x10 box
    b2ShapeDef groundShapeDef = b2DefaultShapeDef();
    b2CreatePolygonShape(groundId, &groundShapeDef, &groundBox);

    // Create dynamic body
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = (b2Vec2){0.0f, 4.0f};
    bodyDef.isAwake = true;
    b2BodyId boxId = b2CreateBody(worldId, &bodyDef);

    // Create dynamic box
    b2Polygon dynamicBox = b2MakeBox(1.0f, 1.0f);
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 1.0f;
    shapeDef.friction = 0.3f;
    b2CreatePolygonShape(boxId, &shapeDef, &dynamicBox);

    // Simulation parameters
    float timeStep = 1.0f / 60.0f;
    int subStepCount = 8;

    // Main game loop
    while (!WindowShouldClose()) {
        // Update physics
        b2World_Step(worldId, timeStep, subStepCount);

        // Start drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Draw grid with coordinates
        DrawPhysicsGrid(PIXELS_PER_METER);  // Draw grid every meter

        // Draw ground and get its position
        b2Vec2 groundPos = b2Body_GetPosition(groundId);
        float groundAngle = b2Rot_GetAngle(b2Body_GetRotation(groundId));
        Vector2 groundScreenPos = physicsToScreen(groundPos);
        DrawPhysicsBox(groundScreenPos, groundAngle, (Vector2){5.0f, 5.0f}, DARKGREEN);

        // Draw box
        b2Vec2 boxPos = b2Body_GetPosition(boxId);
        float boxAngle = b2Rot_GetAngle(b2Body_GetRotation(boxId));
        Vector2 boxScreenPos = physicsToScreen(boxPos);
        DrawPhysicsBox(boxScreenPos, boxAngle, (Vector2){1.0f, 1.0f}, MAROON);

        // Draw positions info
        DrawFPS(10, 10);
        DrawText(TextFormat("Box Position: (%.2f, %.2f)", boxPos.x, boxPos.y), 10, 30, 20, DARKGRAY);
        DrawText(TextFormat("Box Angle: %.2f", boxAngle), 10, 50, 20, DARKGRAY);
        DrawText(TextFormat("Ground Position: (%.2f, %.2f)", groundPos.x, groundPos.y), 10, 70, 20, DARKGRAY);
        DrawText(TextFormat("Ground Angle: %.2f", groundAngle), 10, 90, 20, DARKGRAY);

        EndDrawing();
    }

    // Cleanup
    b2DestroyWorld(worldId);
    CloseWindow();
    return 0;
}
