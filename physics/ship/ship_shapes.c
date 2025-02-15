#include "ship_shapes.h"
#include <math.h>
#include <raylib.h>
#include "game_state.h"
#include "coord_utils.h"
#include <stdlib.h>

#define CURVE_SEGMENTS 20
// Remove CANVAS_TO_PHYSICS and SHIP_SCALE references
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
    // Define visual shape points precisely matching the path
    const float VISUAL_BOW_X = 225 * VISUAL_SCALE_FACTOR;
    const float VISUAL_BOW_Y = 90 * VISUAL_SCALE_FACTOR;
    const float VISUAL_CONTROL_X = 500 * VISUAL_SCALE_FACTOR;
    const float VISUAL_STERN_X = -225 * VISUAL_SCALE_FACTOR;
    const float VISUAL_STERN_CONTROL_X = -325 * VISUAL_SCALE_FACTOR;

    // Draw physics box first if F1 is pressed
    if (IsKeyDown(KEY_F1)) {
        // Center point
        DrawCircleV(center, 4.0f, RED);
        
        // Draw the actual physics box using PHYSICS_SHIP_LENGTH and PHYSICS_SHIP_WIDTH
        float halfLength = (PHYSICS_SHIP_LENGTH * 0.5f) * PIXELS_PER_METER * camera->zoom;
        float halfWidth = (PHYSICS_SHIP_WIDTH * 0.5f) * PIXELS_PER_METER * camera->zoom;
        
        // Define the four corners of the physics box
        Vector2 corners[4] = {
            (Vector2){ halfLength,  halfWidth},  // Top right
            (Vector2){-halfLength,  halfWidth},  // Top left
            (Vector2){-halfLength, -halfWidth},  // Bottom left
            (Vector2){ halfLength, -halfWidth}   // Bottom right
        };
        
        // Transform and draw the physics box
        for (int i = 0; i < 4; i++) {
            Vector2 start = TransformPoint(corners[i], angle, 1.0f, center);
            Vector2 end = TransformPoint(corners[(i + 1) % 4], angle, 1.0f, center);
            DrawLineEx(start, end, 2.0f, YELLOW);
        }
        
        // Draw physics axes
        Vector2 xAxis = TransformPoint((Vector2){halfLength, 0}, angle, 1.0f, center);
        Vector2 yAxis = TransformPoint((Vector2){0, halfWidth}, angle, 1.0f, center);
        DrawLineEx(center, xAxis, 2.0f, RED);    // X axis in red
        DrawLineEx(center, yAxis, 2.0f, GREEN);  // Y axis in green
    }

    // Create points array following exact path description
    Vector2 curvePoints[CURVE_SEGMENTS + 1];
    int pointIndex = 0;

    // 1. Start at (225, 90)
    curvePoints[pointIndex++] = (Vector2){VISUAL_BOW_X, VISUAL_BOW_Y};

    // 2. Quadratic curve to (225, -90) with control point (500, 0)
    for (int i = 0; i <= CURVE_SEGMENTS/4; i++) {
        float t = i / (float)(CURVE_SEGMENTS/4);
        curvePoints[pointIndex++] = QuadraticBezier(
            (Vector2){VISUAL_BOW_X, VISUAL_BOW_Y},      // Start (225, 90)
            (Vector2){VISUAL_CONTROL_X, 0},             // Control (500, 0)
            (Vector2){VISUAL_BOW_X, -VISUAL_BOW_Y},     // End (225, -90)
            t
        );
    }

    // 3. Straight line to (-225, -90)
    curvePoints[pointIndex++] = (Vector2){VISUAL_STERN_X, -VISUAL_BOW_Y};

    // 4. Quadratic curve to (-225, 90) with control point (-325, 0)
    for (int i = 0; i <= CURVE_SEGMENTS/4; i++) {
        float t = i / (float)(CURVE_SEGMENTS/4);
        curvePoints[pointIndex++] = QuadraticBezier(
            (Vector2){VISUAL_STERN_X, -VISUAL_BOW_Y},   // Start (-225, -90)
            (Vector2){VISUAL_STERN_CONTROL_X, 0},       // Control (-325, 0)
            (Vector2){VISUAL_STERN_X, VISUAL_BOW_Y},    // End (-225, 90)
            t
        );
    }

    // 5. Straight line back to start
    curvePoints[pointIndex++] = (Vector2){VISUAL_BOW_X, VISUAL_BOW_Y};

    // Transform and draw shape
    Vector2* transformed = malloc(pointIndex * sizeof(Vector2));
    for (int i = 0; i < pointIndex; i++) {
        transformed[i] = TransformPoint(curvePoints[i], angle, camera->zoom, center);
    }

    // Draw filled shape and outline
    DrawTriangleFan(transformed, pointIndex, Fade(color, 0.3f));
    for (int i = 0; i < pointIndex - 1; i++) {
        DrawLineEx(transformed[i], transformed[i + 1], 2.0f, color);
    }

    // Draw alignment vectors
    float axisLength = 50.0f * camera->zoom;
    // X axis (ship's forward direction) - RED
    Vector2 xAxis = TransformPoint((Vector2){axisLength, 0}, angle, 1.0f, center);
    DrawLineEx(center, xAxis, 2.0f, RED);
    
    // Y axis (ship's side direction) - GREEN
    Vector2 yAxis = TransformPoint((Vector2){0, axisLength}, angle, 1.0f, center);
    DrawLineEx(center, yAxis, 2.0f, GREEN);

    // Draw key points for debugging
    if (IsKeyDown(KEY_F1)) {
        // Draw control points
        Vector2 controlPoint1 = TransformPoint((Vector2){VISUAL_CONTROL_X, 0}, angle, camera->zoom, center);
        Vector2 controlPoint2 = TransformPoint((Vector2){VISUAL_STERN_CONTROL_X, 0}, angle, camera->zoom, center);
        DrawCircleV(controlPoint1, 4.0f, YELLOW);
        DrawCircleV(controlPoint2, 4.0f, YELLOW);
        
        // Draw key vertices
        Vector2 bowPoint = TransformPoint((Vector2){VISUAL_BOW_X, 0}, angle, camera->zoom, center);
        Vector2 sternPoint = TransformPoint((Vector2){VISUAL_STERN_X, 0}, angle, camera->zoom, center);
        DrawCircleV(bowPoint, 4.0f, BLUE);
        DrawCircleV(sternPoint, 4.0f, BLUE);
    }

    free(transformed);
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
            b2Vec2 r = {p2.x - p1.x, p2.y - p3.y};
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
    // Use PHYSICS_SCALE_FACTOR instead of SHIP_SCALE
    const float BOW_LENGTH = 4.0f;   // Base length
    const float BEAM_WIDTH = 2.0f;   // Base width
    const float STERN_WIDTH = 3.0f;  // Base stern width
    
    logDebug("Creating ship hull with physics scale %f", PHYSICS_SCALE_FACTOR);
    
    // Create hull shape
    b2Hull hull;
    hull.count = 8;
    
    // Define points in counter-clockwise order, scaled by PHYSICS_SCALE_FACTOR
    hull.points[0] = (b2Vec2){ BOW_LENGTH * PHYSICS_SCALE_FACTOR,  0.0f};
    hull.points[1] = (b2Vec2){ BOW_LENGTH * 0.5f * PHYSICS_SCALE_FACTOR,  BEAM_WIDTH * 0.5f * PHYSICS_SCALE_FACTOR};
    hull.points[2] = (b2Vec2){-BOW_LENGTH * 0.5f * PHYSICS_SCALE_FACTOR,  BEAM_WIDTH * 0.5f * PHYSICS_SCALE_FACTOR};
    hull.points[3] = (b2Vec2){-BOW_LENGTH * PHYSICS_SCALE_FACTOR,    STERN_WIDTH * 0.5f * PHYSICS_SCALE_FACTOR};
    hull.points[4] = (b2Vec2){-BOW_LENGTH * PHYSICS_SCALE_FACTOR,   -STERN_WIDTH * 0.5f * PHYSICS_SCALE_FACTOR};
    hull.points[5] = (b2Vec2){-BOW_LENGTH * 0.5f * PHYSICS_SCALE_FACTOR, -BEAM_WIDTH * 0.5f * PHYSICS_SCALE_FACTOR};
    hull.points[6] = (b2Vec2){ BOW_LENGTH * 0.5f * PHYSICS_SCALE_FACTOR, -BEAM_WIDTH * 0.5f * PHYSICS_SCALE_FACTOR};
    hull.points[7] = (b2Vec2){ BOW_LENGTH * PHYSICS_SCALE_FACTOR,  0.0f};             // Back to bow

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
        float size = PHYSICS_SCALE_FACTOR * 0.5f;
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
    bodyDef.rotation = (b2Rot){1.0f, 0.0f};  // cos(0)=1, sin(0)=0
    bodyDef.linearDamping = 0.5f;     // Increased damping for more stable movement
    bodyDef.angularDamping = 0.7f;    // Increased angular damping
    bodyDef.gravityScale = 0.0f;      // No gravity effect
    
    b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
    if (!b2Body_IsValid(bodyId)) {
        logDebug("Failed to create body");
        return b2_nullBodyId;
    }
    
    // Create a simple rectangular box for physics using b2MakeBox
    b2Polygon box = b2MakeBox(PHYSICS_SHIP_LENGTH * 0.5f, PHYSICS_SHIP_WIDTH * 0.5f);
    
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 1.0f;
    shapeDef.friction = 0.3f;
    shapeDef.restitution = 0.2f;
    
    b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
    if (!b2Shape_IsValid(shapeId)) {
        logDebug("Failed to create polygon shape");
        b2DestroyBody(bodyId);
        return b2_nullBodyId;
    }
    
    logDebug("Successfully created ship body: %d", bodyId);
    return bodyId;
}
