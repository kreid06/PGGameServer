#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <raylib.h>
#include <box2d/box2d.h>

// Global scale constants
#define PIXELS_PER_METER 10.0f     // Reduced from 100.0f for more manageable scaling
#define METER_PER_PIXEL 0.1f       // Inverse of PIXELS_PER_METER
#define SHIP_SCALE 4.0f            // Doubled from 2.0f to match physics size

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

typedef struct {
    Vector2 target;
    float zoom;
    Vector2 offset;
    Vector2 dragStart;
    bool isDragging;
    int shipsCreated;
    bool isPlacingShip;
    Vector2 placementPreview;
    ShipArray ships;
} Camera2DState;

#endif // GAME_STATE_H
