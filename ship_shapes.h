#ifndef SHIP_SHAPES_H
#define SHIP_SHAPES_H

#include <box2d/box2d.h>
#include <box2d/collision.h>

// Scale factor to convert the large canvas coordinates to reasonable physics sizes
#define SHIP_SCALE 0.05f

// Creates a ship hull polygon shape
// Returns the hull for creating the polygon
b2Hull createShipHullShape(void) {
    b2Vec2 points[6];
    
    // Convert canvas coordinates to physics coordinates (scaled down)
    // Original shape scaled down and centered around origin
    points[0] = (b2Vec2){ 22.5 * SHIP_SCALE, 9.0 * SHIP_SCALE};    // Top right
    points[1] = (b2Vec2){ 50.0 * SHIP_SCALE, 0};                  // Right point
    points[2] = (b2Vec2){ 22.5 * SHIP_SCALE, -9.0 * SHIP_SCALE};   // Bottom right
    points[3] = (b2Vec2){-22.5 * SHIP_SCALE, -9.0 * SHIP_SCALE};   // Bottom left
    points[4] = (b2Vec2){-32.5 * SHIP_SCALE, 0};                  // Left point
    points[5] = (b2Vec2){-22.5 * SHIP_SCALE, 9.0 * SHIP_SCALE};    // Top left
    
    return b2ComputeHull(points, 6);
}

// Creates the ship hull body in the physics world
b2BodyId createShipHull(b2WorldId worldId, float x, float y, b2Rot rotation) {
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = (b2Vec2){x, y};
    bodyDef.rotation = rotation;
    b2BodyId shipId = b2CreateBody(worldId, &bodyDef);

    // Create hull shape using computed hull
    b2Hull hull = createShipHullShape();
    b2Polygon polygon = b2MakePolygon(&hull, 0.0f); // 0.0f radius for sharp corners
    
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 1.0f;
    shapeDef.friction = 0.3f;
    
    b2CreatePolygonShape(shipId, &shapeDef, &polygon);
    
    return shipId;
}

#endif // SHIP_SHAPES_H
