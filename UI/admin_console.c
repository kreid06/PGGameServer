#include "admin_console.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

static void* adminConsoleThread(void* data) {
    AdminConsole* console = (AdminConsole*)data;
    char cmd[256];
    
    printf("Admin Console Started\n");
    printf("Commands: list, add, delete <id>, help, quit\n");
    
    while (console->isRunning) {
        printf("admin> ");
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) break;
        
        if (strncmp(cmd, "list", 4) == 0) {
            printf("Ships (%d total):\n", console->ships->count);
            for (int i = 0; i < console->ships->count; i++) {
                Ship* ship = &console->ships->ships[i];
                printf("[%d] Pos: (%.1f, %.1f)\n", 
                       i, ship->physicsPos.x, ship->physicsPos.y);
            }
        }
        else if (strncmp(cmd, "add", 3) == 0) {
            // Create ship using proper Box2D initialization
            b2BodyId newShipId = createShipHull(console->worldId, 0, 0, (b2Rot){1, 0});
            if (b2Body_IsValid(newShipId)) {
                Ship ship = {
                    .id = newShipId,
                    .physicsPos = {0, 0},
                    .screenPos = {0, 0}
                };
                addShip(console->ships, ship);
                printf("Added new ship at origin\n");
            } else {
                printf("Failed to create ship body\n");
            }
        }
        else if (strncmp(cmd, "delete", 6) == 0) {
            int id;
            if (sscanf(cmd, "delete %d", &id) == 1 && id < console->ships->count) {
                b2DestroyBody(console->ships->ships[id].id);
                // Shift remaining ships left
                for (int i = id; i < console->ships->count - 1; i++) {
                    console->ships->ships[i] = console->ships->ships[i + 1];
                }
                console->ships->count--;
                printf("Deleted ship %d\n", id);
            }
        }
        else if (strncmp(cmd, "help", 4) == 0) {
            printf("Available commands:\n");
            printf("  list              - List all ships\n");
            printf("  add               - Add a new ship\n");
            printf("  delete <id>       - Delete ship by ID\n");
            printf("  help              - Show this help\n");
            printf("  quit              - Exit admin console\n");
        }
        else if (strncmp(cmd, "quit", 4) == 0) {
            break;
        }
    }
    
    console->isRunning = false;
    return NULL;
}

void initAdminConsole(AdminConsole* console, b2WorldId worldId, ShipArray* ships) {
    console->worldId = worldId;
    console->ships = ships;
    console->isRunning = true;
}

void startAdminConsoleThread(AdminConsole* console) {
    pthread_t thread;
    pthread_create(&thread, NULL, adminConsoleThread, console);
    pthread_detach(thread);
}

void stopAdminConsole(AdminConsole* console) {
    console->isRunning = false;
}
