#include "ship_shapes.h"
#include <math.h>
#include <raylib.h>
#include "game_state.h"
#include "coord_utils.h"

#define CURVE_SEGMENTS 20
#define CANVAS_TO_PHYSICS (SHIP_SCALE / 500.0f)
#define b2_maxPolygonVertices 8
#define MIN_VERTEX_DISTANCE 0.01f   // Minimum distance between vertices

extern void logDebug(const char* format, ...);

Vector2 TransformPoint(Vector2 p, float angle, float zoom, Vector2 center) {
    float cs = cosf(angle);
    float sn = sinf(angle);
    return (Vector2){
        (p.x * cs - p.y * sn) * zoom + center.x,
        (p.x * sn + p.y * cs) * zoom + center.y
    };
}

Vector2 QuadraticBezier(Vector2 p0, Vector2 p1, Vector2 p2, float t) {
    float u = 1.0f - t;
    return (Vector2){
        u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
        u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y
    };
}

void DrawShipHull(Vector2 center, float angle, Color color, const Camera2DState* camera) {
    // Ship points in physics scale - doubled size
    const float WIDTH = 4.0f * SHIP_SCALE;    // Doubled from 2.0f
    const float LENGTH = 8.0f * SHIP_SCALE;   // Doubled from 4.0f
    
    Vector2 points[] = {
        {LENGTH/2, 0},             // Bow
        {LENGTH/3, WIDTH/2},       // Starboard bow - adjusted curve
        {-LENGTH/3, WIDTH/2},      // Starboard quarter - adjusted curve
        {-LENGTH/2, WIDTH/4},      // Stern starboard
        {-LENGTH/2, -WIDTH/4},     // Stern port
        {-LENGTH/3, -WIDTH/2},     // Port quarter - adjusted curve
        {LENGTH/3, -WIDTH/2},      // Port bow - adjusted curve
    };
    
    const int NUM_POINTS = sizeof(points)/sizeof(points[0]);
    Vector2 transformed[NUM_POINTS + 1];  // +1 for closing the shape
    
    // Transform all points to screen space
    for (int i = 0; i < NUM_POINTS; i++) {
        transformed[i] = TransformPoint(points[i], angle, camera->zoom, center);
    }
    transformed[NUM_POINTS] = transformed[0];  // Close the shape
    
    // Draw filled ship
    DrawTriangleFan(transformed, NUM_POINTS, Fade(color, 0.3f));
    
    // Draw outline
    for (int i = 0; i < NUM_POINTS; i++) {
        DrawLineEx(transformed[i], transformed[(i + 1) % NUM_POINTS], 2.0f, color);
    }
    
    // Draw bow indicator (direction)
    Vector2 bowTip = transformed[0];
    Vector2 bowBase = {
        (transformed[1].x + transformed[6].x) / 2,
        (transformed[1].y + transformed[6].y) / 2
    };
    DrawLineEx(bowBase, bowTip, 3.0f, RED);
}

// Add hull validation helpers
static float crossProduct2D(b2Vec2 a, b2Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

static bool checkWindingOrder(const b2Hull* hull) {
    float area = 0.0f;
    for (int i = 0; i < hull->count; i++) {
        int j = (i + 1) % hull->count;
        area += crossProduct2D(hull->points[i], hull->points[j]);
    }
    logDebug("Hull area: %.3f (should be positive for CCW winding)", area * 0.5f);
    return area > 0.0f;
}

static bool checkSelfIntersection(const b2Hull* hull) {
    for (int i = 0; i < hull->count; i++) {
        int i2 = (i + 1) % hull->count;
        b2Vec2 p1 = hull->points[i];
        b2Vec2 p2 = hull->points[i2];
        
        for (int j = i + 2; j < hull->count; j++) {
            int j2 = (j + 1) % hull->count;
            if (i == 0 && j2 == hull->count - 1) continue;
            
            b2Vec2 p3 = hull->points[j];
            b2Vec2 p4 = hull->points[j2];
            
            // Check line segments for intersection
            b2Vec2 r = {p2.x - p1.x, p2.y - p1.y};
            b2Vec2 s = {p4.x - p3.x, p4.y - p3.y};
            float rxs = crossProduct2D(r, s);
            
            if (rxs != 0) {
                logDebug("Found potential self-intersection between segments %d-%d and %d-%d",
                        i, i2, j, j2);
                return true;
            }
        }
    }
    return false;
}

bool validateHull(const b2Hull* hull) {
    if (hull->count < 3 || hull->count > b2_maxPolygonVertices) {
        logDebug("Invalid hull vertex count: %d", hull->count);
        return false;
    }

    // Check for valid vertices and minimum size
    float minX = FLT_MAX, minY = FLT_MAX;
    float maxX = -FLT_MAX, maxY = -FLT_MAX;
    
    for (int i = 0; i < hull->count; i++) {
        if (!isfinite(hull->points[i].x) || !isfinite(hull->points[i].y)) {
            logDebug("Invalid vertex %d: (%.2f, %.2f)", i, hull->points[i].x, hull->points[i].y);
            return false;
        }
        
        minX = fminf(minX, hull->points[i].x);
        minY = fminf(minY, hull->points[i].y);
        maxX = fmaxf(maxX, hull->points[i].x);
        maxY = fmaxf(maxY, hull->points[i].y);
    }
    
    float width = maxX - minX;
    float height = maxY - minY;
    logDebug("Hull bounds: %.2f x %.2f meters", width, height);
    
    // Check winding order
    if (!checkWindingOrder(hull)) {
        logDebug("ERROR: Hull vertices must be in counter-clockwise order");
        return false;
    }
    
    // Check for self-intersection
    if (checkSelfIntersection(hull)) {
        logDebug("ERROR: Hull has self-intersecting edges");
        return false;
    }
    
    return true;
}

b2Hull createShipHullShape(void) {
    // Define ship shape in meters with doubled size
    const float BOW_LENGTH = 4.0f * SHIP_SCALE;   // Doubled from 2.0f
    const float BEAM_WIDTH = 2.0f * SHIP_SCALE;   // Doubled from 1.0f
    const float STERN_WIDTH = 3.0f * SHIP_SCALE;  // Doubled from 1.5f
    
    logDebug("Creating ship hull with scale %f", SHIP_SCALE);
    
    // Create simplified but larger hull shape
    b2Hull hull;
    hull.count = 8;
    
    // Define points in counter-clockwise order
    hull.points[0] = (b2Vec2){ BOW_LENGTH,  0.0f};             // Bow tip
    hull.points[1] = (b2Vec2){ BOW_LENGTH/2,  BEAM_WIDTH/2};   // Starboard bow
    hull.points[2] = (b2Vec2){-BOW_LENGTH/2,  BEAM_WIDTH/2};   // Starboard midship
    hull.points[3] = (b2Vec2){-BOW_LENGTH,    STERN_WIDTH/2};  // Starboard stern
    hull.points[4] = (b2Vec2){-BOW_LENGTH,   -STERN_WIDTH/2};  // Port stern
    hull.points[5] = (b2Vec2){-BOW_LENGTH/2, -BEAM_WIDTH/2};   // Port midship
    hull.points[6] = (b2Vec2){ BOW_LENGTH/2, -BEAM_WIDTH/2};   // Port bow
    hull.points[7] = (b2Vec2){ BOW_LENGTH,  0.0f};             // Back to bow

    // Log hull points
    for (int i = 0; i < hull.count; i++) {
        logDebug("Hull point %d: (%.2f, %.2f)", i, hull.points[i].x, hull.points[i].y);
        
        // Verify distance between consecutive points
        if (i > 0) {
            float dx = hull.points[i].x - hull.points[i-1].x;
            float dy = hull.points[i].y - hull.points[i-1].y;
            float dist = sqrtf(dx*dx + dy*dy);
            logDebug("Distance to previous point: %.3f meters", dist);
            if (dist < MIN_VERTEX_DISTANCE) {
                logDebug("WARNING: Vertices too close together at point %d", i);
            }
        }
    }

    // Validate hull before returning
    if (!validateHull(&hull)) {
        logDebug("WARNING: Creating fallback triangle shape");
        hull.count = 3;
        float size = SHIP_SCALE * 0.5f;
        hull.points[0] = (b2Vec2){ size,  0.0f};
        hull.points[1] = (b2Vec2){-size,  size};
        hull.points[2] = (b2Vec2){-size, -size};
    }
    
    return hull;
}

b2BodyId createShipHull(b2WorldId worldId, float x, float y, b2Rot rotation) {
    logDebug("Creating ship at position (%.2f, %.2f)", x, y);
    
    if (!b2World_IsValid(worldId)) {
        logDebug("Invalid world ID");
        return b2_nullBodyId;
    }
    
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = (b2Vec2){x, y};
    bodyDef.rotation = rotation;
    bodyDef.linearDamping = 0.5f;     // Increased damping for more stable movement
    bodyDef.angularDamping = 0.7f;    // Increased angular damping
    bodyDef.gravityScale = 0.0f;      // No gravity effect
    
    b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
    if (!b2Body_IsValid(bodyId)) {
        logDebug("Failed to create body");
        return b2_nullBodyId;
    }
    
    b2Hull hull = createShipHullShape();
    logDebug("Created hull with %d vertices", hull.count);
    
    // Increased vertex radius for more stable physics
    float radius = 0.1f;
    b2Polygon polygon = b2MakePolygon(&hull, radius);
    logDebug("Created polygon with vertex radius %.3f", radius);
    
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 1.0f;
    shapeDef.friction = 0.3f;
    shapeDef.restitution = 0.2f;
    
    b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &polygon);
    if (!b2Shape_IsValid(shapeId)) {
        logDebug("Failed to create polygon shape");
        b2DestroyBody(bodyId);
        return b2_nullBodyId;
    }
    
    logDebug("Successfully created ship body: %d", bodyId);
    return bodyId;
}
