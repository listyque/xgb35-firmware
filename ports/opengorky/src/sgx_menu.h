#ifndef OG_SGX_MENU_H
#define OG_SGX_MENU_H
#include "sdl_xgb35.h"

typedef struct {
    const SDL_Vertex *verts;
    const int *indices;
    int vertices, count;
    const void *pixels;
    unsigned *texture_id;
    int width, height, rgb565, linear;
} OgSgxMesh;

typedef bool OgSgxDraw(uint16_t *target, int width, int height, const OgSgxMesh *mesh, int count);
OgSgxDraw og_sgx_draw;
void og_sgx_close(void);
void og_sgx_forget(unsigned id);
#endif
