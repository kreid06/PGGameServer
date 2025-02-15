#include "coord_utils.h"
#include <math.h>

extern void logDebug(const char* format, ...);

Vector2 physicsToScreen(b2Vec2 position, const Camera2DState* camera) {
    float screenX = (position.x * PIXELS_PER_METER * camera->zoom) + camera->offset.x + GetScreenWidth() / 2;
    float screenY = GetScreenHeight() / 2 - (position.y * PIXELS_PER_METER * camera->zoom) + camera->offset.y;
    return (Vector2){ screenX, screenY };
}

b2Vec2 screenToPhysics(Vector2 screenPos, const Camera2DState* camera) {
    float zoom = (camera->zoom <= 0.0f) ? 0.1f : camera->zoom;
    
    float physX = ((screenPos.x - GetScreenWidth() / 2.0f - camera->offset.x) / (zoom * PIXELS_PER_METER));
    float physY = ((GetScreenHeight() / 2.0f - screenPos.y + camera->offset.y) / (zoom * PIXELS_PER_METER));
    
    // Validate and clamp coordinates
    if (!isfinite(physX)) physX = 0.0f;
    if (!isfinite(physY)) physY = 0.0f;
    physX = fmaxf(fminf(physX, 1000.0f), -1000.0f);
    physY = fmaxf(fminf(physY, 1000.0f), -1000.0f);
    
    logDebug("Screen->Physics conversion: (%f,%f) -> (%f,%f)", screenPos.x, screenPos.y, physX, physY);
    
    return (b2Vec2){ physX, physY };
}
