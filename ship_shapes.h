#ifndef SHIP_SHAPES_H
#define SHIP_SHAPES_H

#include <box2d/box2d.h>
#include <raylib.h>
#include "game_state.h"

// Helper function for ship drawing
Vector2 TransformPoint(Vector2 p, float angle, float zoom, Vector2 center);
Vector2 QuadraticBezier(Vector2 p0, Vector2 p1, Vector2 p2, float t);

// Box2D shape creation functions
b2Hull createShipHullShape(void);
b2BodyId createShipHull(b2WorldId worldId, float x, float y, b2Rot rotation);

// Visual rendering functions
void DrawShipHull(Vector2 center, float angle, Color color, const Camera2DState* camera);

#endif // SHIP_SHAPES_H
