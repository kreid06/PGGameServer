#ifndef NUKLEAR_RAYLIB_H
#define NUKLEAR_RAYLIB_H

#include <stddef.h>
#include <raylib.h>

// All Nuklear configuration in one place
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

// Include Nuklear implementation only in one source file
#ifdef NK_IMPLEMENTATION
    #include "nuklear.h"
#else
    #include "nuklear.h"
#endif

// Function declarations
struct nk_context* nk_raylib_init(void);
void nk_raylib_render(void);
void nk_raylib_shutdown(void);

// Add input handling function declaration
void nk_raylib_input_begin(void);

#endif // NUKLEAR_RAYLIB_H
