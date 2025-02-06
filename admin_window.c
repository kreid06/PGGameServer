#include "admin_window.h"
#include "ship_shapes.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "coord_utils.h"

extern void logDebug(const char* format, ...);  // Declare external logging function

// Add GuiButton implementation that was in main.c
GuiButton CreateButton(float x, float y, float width, float height, const char* text, Color color) {
    return (GuiButton){
        .bounds = (Rectangle){x, y, width, height},
        .text = text,
        .color = color,
        .hoverColor = ColorBrightness(color, 0.2f),
        .isHovered = false
    };
}

bool GuiButtonUpdate(GuiButton* button) {
    // ...existing GuiButton update code from main.c...
}

// Move coordinate conversion function from main.c
void initAdminWindow(AdminWindow* admin, b2WorldId worldId, ShipArray* ships, Camera2DState* camera) {
    admin->worldId = worldId;
    admin->ships = ships;
    admin->isOpen = true;
    admin->selectedShipIndex = -1;
    admin->isPositioningShip = false;
    admin->camera = camera;
    admin->ctx = nk_raylib_init();
}

void updateAdminWindow(AdminWindow* admin) {
    if (!admin->isOpen || !admin->ctx) {
        logDebug("Admin window update skipped: isOpen=%d, ctx=%p", 
                admin->isOpen, (void*)admin->ctx);
        return;
    }
    
    // Handle ship placement
    if (admin->isPositioningShip) {
        Vector2 mousePos = GetMousePosition();
        if (mousePos.x < GetScreenWidth() - 400) {
            // Preview ship position
            DrawCircleV(mousePos, 5, GREEN);
            DrawText("Click to place new ship", mousePos.x + 10, mousePos.y - 10, 20, GREEN);
            
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                b2Vec2 physicsPos = screenToPhysics(mousePos, admin->camera);
                logDebug("Attempting ship creation at pos=(%.2f, %.2f)", 
                        physicsPos.x, physicsPos.y);
                
                // Validate physics position before creating body
                if (isfinite(physicsPos.x) && isfinite(physicsPos.y)) {
                    // Create ship with proper orientation
                    b2Rot rotation = {1.0f, 0.0f};  // Default facing right
                    b2BodyId newShip = createShipHull(admin->worldId, physicsPos.x, physicsPos.y, rotation);
                    
                    if (b2Body_IsValid(newShip)) {
                        Ship ship = {
                            .id = newShip,
                            .physicsPos = physicsPos,
                            .screenPos = mousePos
                        };
                        addShip(admin->ships, ship);
                        printf("Created new ship at position (%.2f, %.2f)\n", physicsPos.x, physicsPos.y);
                    } else {
                        printf("ERROR: Failed to create ship body at (%.2f, %.2f)\n", physicsPos.x, physicsPos.y);
                    }
                } else {
                    printf("ERROR: Invalid position for ship creation: (%.2f, %.2f)\n", physicsPos.x, physicsPos.y);
                }
                admin->isPositioningShip = false;
            }
            
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                admin->isPositioningShip = false;
            }
        }
    }

    // Handle GUI
    nk_raylib_input_begin();
    
    struct nk_rect bounds = nk_rect(
        GetScreenWidth() - 400, 0, 
        400, GetScreenHeight()
    );
    
    if (!nk_begin(admin->ctx, "Admin Panel", bounds,
        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_MOVABLE)) {
        logDebug("Failed to begin Nuklear window");
        return;
    }
    
    // Static row for buttons
    nk_layout_row_static(admin->ctx, 30, 80, 2);
        
    // Add ship button
    if (nk_button_label(admin->ctx, admin->isPositioningShip ? "Cancel" : "Add Ship")) {
        admin->isPositioningShip = !admin->isPositioningShip;
        admin->selectedShipIndex = -1;
    }

    // Ships list with improved error checking
    if (nk_tree_push(admin->ctx, NK_TREE_TAB, "Ships", NK_MINIMIZED)) {
        for (int i = 0; i < admin->ships->count; i++) {
            Ship* ship = &admin->ships->ships[i];
            if (!b2Body_IsValid(ship->id)) continue;  // Skip invalid ships
            
            b2Vec2 pos = b2Body_GetPosition(ship->id);
            char label[64];
            sprintf(label, "Brigantine %d: (%.1f, %.1f)", i, pos.x, pos.y);
            
            nk_layout_row_dynamic(admin->ctx, 25, 2);
            if (nk_selectable_label(admin->ctx, label, NK_TEXT_LEFT, &(int){admin->selectedShipIndex == i})) {
                admin->selectedShipIndex = i;
            }
            
            if (nk_button_label(admin->ctx, "Delete")) {
                if (b2Body_IsValid(ship->id)) {
                    printf("Deleted ship %d at position (%.2f, %.2f)\n", 
                           i, ship->physicsPos.x, ship->physicsPos.y);
                    b2DestroyBody(ship->id);
                }
                
                // Safe array shrinking
                if (i < admin->ships->count - 1) {
                    memmove(&admin->ships->ships[i], 
                           &admin->ships->ships[i + 1],
                           (admin->ships->count - i - 1) * sizeof(Ship));
                }
                admin->ships->count--;
                admin->selectedShipIndex = -1;
                break;
            }
        }
        nk_tree_pop(admin->ctx);
    }
    nk_end(admin->ctx);
    
    nk_raylib_render();
}

void closeAdminWindow(AdminWindow* admin) {
    nk_raylib_shutdown();
    admin->isOpen = false;
}
