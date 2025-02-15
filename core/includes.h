#ifndef CORE_INCLUDES_H
#define CORE_INCLUDES_H

// Core includes
#include "game_state.h"

// Network includes
#include "../network/common_protocol.h"
#include "../network/game_protocol.h"
#include "../network/player_connection.h"
#include "../network/websockets/websocket.h"
#include "../network/tcpsockets/tcp_protocol.h"
#include "../network/server_messages.h"
#include "../network/player_connection.h"
#include "../network/websockets/ws_protocol.h"

// Database includes
#include "../database/db_client.h"
#include "../database/protocol/db_protocol.h"

// Physics includes
#include "../physics/player/player_physics.h"
#include "../physics/ship/ship_shapes.h"

// UI includes
#include "../UI/admin_console.h"
#include "../UI/admin_window.h"

// World includes
#include "../world/coord_utils.h"

// External includes
#include "../env_loader.h"
#include "../.external/nuklear_raylib.h"

#endif
