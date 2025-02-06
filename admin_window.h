#ifndef ADMIN_WINDOW_H
#define ADMIN_WINDOW_H

#include "game_state.h"
#include <raylib.h>
#include "external/nuklear_raylib.h"
#include <stdio.h>

// Define GuiButton structure (move from main.c)
typedef struct {
    Rectangle bounds;
    const char* text;
    Color color;
    Color hoverColor;
    bool isHovered;
} GuiButton;

typedef struct {
    b2WorldId worldId;
    ShipArray* ships;
    bool isOpen;
    struct nk_context* ctx;
    int selectedShipIndex;
    bool isPositioningShip;  // Add this flag
    Camera2DState* camera;   // Add camera reference
} AdminWindow;

// GuiButton functions declarations
GuiButton CreateButton(float x, float y, float width, float height, const char* text, Color color);
bool GuiButtonUpdate(GuiButton* button);

// Add function declaration for addShip
void addShip(ShipArray* array, Ship ship);

// Add coordinate conversion function declaration
b2Vec2 screenToPhysics(Vector2 screenPos, const Camera2DState* camera);

// Admin window functions
void initAdminWindow(AdminWindow* admin, b2WorldId worldId, ShipArray* ships, Camera2DState* camera);
void updateAdminWindow(AdminWindow* admin);
void closeAdminWindow(AdminWindow* admin);

#endif // ADMIN_WINDOW_H
