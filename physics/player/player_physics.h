#ifndef PLAYER_PHYSICS_H
#define PLAYER_PHYSICS_H

#include <box2d/box2d.h>
#include <math.h>
#include "../network/game_protocol.h"  // Add this to get input flags

// Add Box2D angle calculation helpers for Box2D 3.0
static inline float b2Body_GetAngle(b2BodyId bodyId) {
    b2Rot rot = b2Body_GetRotation(bodyId);
    return atan2f(rot.s, rot.c);  // Calculate angle from sine and cosine
}

static inline void b2Body_SetAngle(b2BodyId bodyId, float angle) {
    b2Rot rot = {cosf(angle), sinf(angle)};
    b2Body_SetTransform(bodyId, b2Body_GetPosition(bodyId), rot);
}

#define PLAYER_RADIUS 1.0f        // 1 meter radius
#define PLAYER_DENSITY 1.0f       // kg/m^2
#define PLAYER_FRICTION 0.2f      // Sliding friction
#define PLAYER_RESTITUTION 0.4f   // Bounciness
#define PLAYER_LINEAR_DAMPING 0.5f // Air resistance
#define PLAYER_ANGULAR_DAMPING 2.0f // Rotation damping

// Movement constants
#define PLAYER_MOVE_FORCE 500.0f   // Newtons
#define PLAYER_MAX_SPEED 20.0f     // meters/second
#define PLAYER_TURN_TORQUE 100.0f  // Newton-meters
#define PLAYER_STRAFE_FACTOR 0.7f  // Strafe movement multiplier
#define PLAYER_BOOST_MULTIPLIER 2.0f
#define PLAYER_BRAKE_FORCE 250.0f  // Add brake force constant

// Function declarations
b2BodyId createPlayerBody(b2WorldId worldId, float x, float y);
void applyPlayerMovement(b2BodyId bodyId, uint16_t inputFlags, float dt);  // Change to uint16_t
void limitPlayerVelocity(b2BodyId bodyId);

#endif
