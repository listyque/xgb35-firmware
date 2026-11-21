#ifndef OG_SGX_SPRITES_H
#define OG_SGX_SPRITES_H
#include "sdl_xgb35.h"
#include "../../../xgb35-firmware/drivers/sgx540/wsegl-stream.h"

typedef struct {
    unsigned id, framebuffer;
    bool dirty;
    bool linear;
    SDL_Rect update;
    Xgb35StreamPixmap shared;
    void *image;
    unsigned used_frame;
} OgGpuTexture;

typedef struct {
    bool (*open)(int framebuffer);
    void (*close)(void);
    bool (*texture)(OgGpuTexture *, const void *, int, int, int, int, bool, bool);
    bool (*map)(OgGpuTexture *, int, int);
    void (*destroy)(OgGpuTexture *);
    void (*target)(const OgGpuTexture *, int, int);
    void (*clear)(float, float, float, float);
    void (*rect)(unsigned, uint32_t, SDL_Rect, SDL_FRect, SDL_FRect, SDL_FColor);
    void (*quad)(unsigned, uint32_t, SDL_Rect, const SDL_Vertex *);
    void (*geometry)(unsigned, uint32_t, SDL_Rect,
                     const SDL_Vertex *, int, const int *, int, bool, SDL_FRect, bool);
    bool (*present)(void);
} OgGpuSprites;
#endif
