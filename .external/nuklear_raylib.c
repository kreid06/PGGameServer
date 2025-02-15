#define NK_IMPLEMENTATION
#include "nuklear_raylib.h"
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

extern void logDebug(const char* format, ...);  // Declare external logging function

static struct nk_context ctx;
static struct nk_buffer cmds;
static struct nk_user_font font;
static struct nk_input input;

static float get_text_width(nk_handle handle, float height, const char* text, int len) {
    if (!text) return 0;
    return MeasureText(text, (int)height);
}

// Add input handling function
void nk_raylib_input_begin(void) {
    nk_input_begin(&ctx);
    
    // Mouse position
    Vector2 mousePos = GetMousePosition();
    nk_input_motion(&ctx, (int)mousePos.x, (int)mousePos.y);
    
    // Mouse buttons
    nk_input_button(&ctx, NK_BUTTON_LEFT, (int)mousePos.x, (int)mousePos.y, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    nk_input_button(&ctx, NK_BUTTON_RIGHT, (int)mousePos.x, (int)mousePos.y, IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
    nk_input_button(&ctx, NK_BUTTON_MIDDLE, (int)mousePos.x, (int)mousePos.y, IsMouseButtonDown(MOUSE_BUTTON_MIDDLE));
    
    // Mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        nk_input_scroll(&ctx, nk_vec2(0, wheel));
    }
    
    // Keyboard input (add as needed)
    if (IsKeyPressed(KEY_BACKSPACE)) nk_input_key(&ctx, NK_KEY_BACKSPACE, 1);
    if (IsKeyPressed(KEY_ENTER)) nk_input_key(&ctx, NK_KEY_ENTER, 1);
    
    nk_input_end(&ctx);
}

struct nk_context* nk_raylib_init(void) {
    logDebug("Initializing Nuklear context");
    font.userdata = nk_handle_ptr(NULL);
    font.height = 20;
    font.width = get_text_width;

    if (nk_init_default(&ctx, &font) == 0) {
        logDebug("Failed to initialize Nuklear context");
        return NULL;
    }
    
    nk_buffer_init_default(&cmds);
    if (cmds.memory.ptr == NULL) {
        logDebug("Failed to initialize Nuklear command buffer");
        nk_free(&ctx);
        return NULL;
    }
    
    logDebug("Nuklear initialization successful");
    return &ctx;
}

void nk_raylib_render(void) {
    const struct nk_command* cmd;
    static int cmdCount = 0;
    cmdCount = 0;
    
    // Don't call BeginDrawing/EndDrawing here - let the main loop handle it
    nk_foreach(cmd, &ctx) {
        cmdCount++;
        switch (cmd->type) {
            case NK_COMMAND_RECT_FILLED: {
                const struct nk_command_rect_filled* r = (const struct nk_command_rect_filled*)cmd;
                DrawRectangle((int)r->x, (int)r->y, (int)r->w, (int)r->h, 
                    (Color){r->color.r, r->color.g, r->color.b, r->color.a});
            } break;
            
            case NK_COMMAND_TEXT: {
                const struct nk_command_text* t = (const struct nk_command_text*)cmd;
                DrawText(t->string, (int)t->x, (int)t->y, (int)font.height,
                    (Color){t->foreground.r, t->foreground.g, t->foreground.b, t->foreground.a});
            } break;
        }
    }
    
    if (cmdCount > 1000) {  // Arbitrary threshold for suspicious command count
        logDebug("Warning: High Nuklear command count: %d", cmdCount);
    }
    
    nk_clear(&ctx);
}

void nk_raylib_shutdown(void) {
    nk_buffer_free(&cmds);
    nk_free(&ctx);
}
