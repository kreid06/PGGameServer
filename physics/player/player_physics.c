#include "player_physics.h"
#include "game_protocol.h"
#include <math.h>
#include <box2d/box2d.h>
#include <stdio.h>
#include <string.h>     // Add for memcpy
#include <stdint.h>     // Add for uint64_t
#include <inttypes.h>   // Add for PRIu64

#define logDebug(fmt, ...) fprintf(stderr, "[Physics] " fmt "\n", ##__VA_ARGS__)

// Remove createShipHull as it's already defined in ship_shapes.c

b2BodyId createPlayerBody(b2WorldId worldId, float x, float y) {
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = (b2Vec2){x, y};
    bodyDef.linearDamping = PLAYER_LINEAR_DAMPING;
    bodyDef.angularDamping = PLAYER_ANGULAR_DAMPING;
    
    b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
    
    // Create circle shape directly with proper syntax for Box2D 3.0
    b2Circle circle;
    circle.center = (b2Vec2){0.0f, 0.0f};  // Use center instead of point
    circle.radius = PLAYER_RADIUS;
    
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = PLAYER_DENSITY;
    shapeDef.friction = PLAYER_FRICTION;
    shapeDef.restitution = PLAYER_RESTITUTION;
    
    b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
    
    if (!b2Shape_IsValid(shapeId)) {
        logDebug("Failed to create circle shape");
        b2DestroyBody(bodyId);
        return b2_nullBodyId;
    }
    
    return bodyId;
}

void applyPlayerMovement(b2BodyId bodyId, uint16_t inputFlags, float dt) {
    float angle = b2Body_GetAngle(bodyId);
    b2Vec2 facing = {cosf(angle), sinf(angle)};
    b2Vec2 right = {-facing.y, facing.x};  // Right vector for strafing
    b2Vec2 force = {0, 0};
    float torque = 0;
    
    // Handle strafe combinations
    if ((inputFlags & INPUT_STRAFE_LEFT) == INPUT_STRAFE_LEFT) {
        // Strafe left (forward + left)
        force.x += (facing.x * PLAYER_STRAFE_FACTOR + right.x * -PLAYER_STRAFE_FACTOR) * PLAYER_MOVE_FORCE;
        force.y += (facing.y * PLAYER_STRAFE_FACTOR + right.y * -PLAYER_STRAFE_FACTOR) * PLAYER_MOVE_FORCE;
    }
    else if ((inputFlags & INPUT_STRAFE_RIGHT) == INPUT_STRAFE_RIGHT) {
        // Strafe right (forward + right)
        force.x += (facing.x * PLAYER_STRAFE_FACTOR + right.x * PLAYER_STRAFE_FACTOR) * PLAYER_MOVE_FORCE;
        force.y += (facing.y * PLAYER_STRAFE_FACTOR + right.y * PLAYER_STRAFE_FACTOR) * PLAYER_MOVE_FORCE;
    }
    else {
        // Handle individual directional inputs
        if (inputFlags & INPUT_FORWARD) {
            force.x += facing.x * PLAYER_MOVE_FORCE;
            force.y += facing.y * PLAYER_MOVE_FORCE;
        }
        if (inputFlags & INPUT_BACKWARD) {
            force.x -= facing.x * PLAYER_MOVE_FORCE * 0.5f;
            force.y -= facing.y * PLAYER_MOVE_FORCE * 0.5f;
        }
        if (inputFlags & INPUT_LEFT) torque -= PLAYER_TURN_TORQUE;
        if (inputFlags & INPUT_RIGHT) torque += PLAYER_TURN_TORQUE;
    }
    
    // Apply forces with wake parameter
    b2Body_ApplyForceToCenter(bodyId, force, true);
    b2Body_ApplyTorque(bodyId, torque, true);
}

void limitPlayerVelocity(b2BodyId bodyId) {
    b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    
    if (speed > PLAYER_MAX_SPEED) {
        float scale = PLAYER_MAX_SPEED / speed;
        vel.x *= scale;
        vel.y *= scale;
        b2Body_SetLinearVelocity(bodyId, vel);
    }
}
