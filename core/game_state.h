#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <raylib.h>
#include <box2d/box2d.h>
#include "../database/db_client.h"    // Add this include for DatabaseClient
#include "../database/network/db_protocol.h" // Add this include for DatabaseHealth

// Unified scale constants
#define PIXELS_PER_METER 100.0f       // Screen pixels per physics meter
#define METERS_PER_PIXEL (1.0f/PIXELS_PER_METER)
#define VISUAL_SCALE_FACTOR 0.65f      // Visual ship scale
#define PHYSICS_SCALE_FACTOR 0.01f    // Physics scale

// Ship dimensions
#define PHYSICS_SHIP_LENGTH 4.5f      // Length of ship in meters
#define PHYSICS_SHIP_WIDTH 1.8f       // Width of ship in meters

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

typedef struct {
    DatabaseClient dbClient;
    DatabaseHealth dbHealth;
    double lastHealthCheck;
    bool isDbHealthy;
} GameServer;

#endif // GAME_STATE_H
