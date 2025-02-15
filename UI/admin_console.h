#ifndef ADMIN_CONSOLE_H
#define ADMIN_CONSOLE_H

#include <box2d/box2d.h>  // Add this for b2Body_IsValid
#include <stdbool.h>
#include "../core/includes.h"

// Function declarations from main.c that admin console needs
void addShip(ShipArray* array, Ship ship);

typedef struct {
    b2WorldId worldId;
    ShipArray* ships;
    bool isRunning;
} AdminConsole;

void initAdminConsole(AdminConsole* console, b2WorldId worldId, ShipArray* ships);
void startAdminConsoleThread(AdminConsole* console);
void stopAdminConsole(AdminConsole* console);

#endif // ADMIN_CONSOLE_H
