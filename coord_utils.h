#ifndef COORD_UTILS_H
#define COORD_UTILS_H

#include <raylib.h>
#include <box2d/box2d.h>
#include "game_state.h"

// Coordinate conversion functions
Vector2 physicsToScreen(b2Vec2 position, const Camera2DState* camera);
b2Vec2 screenToPhysics(Vector2 screenPos, const Camera2DState* camera);

#endif // COORD_UTILS_H
