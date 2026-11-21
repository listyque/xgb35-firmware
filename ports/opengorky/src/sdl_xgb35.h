#ifndef OG_SDL_XGB35_H
#define OG_SDL_XGB35_H

#include <stddef.h>
#include "sdl2_compat.h"

enum {
    OG_HIDE_ACTOR_LAYER = 1, OG_HIDE_SHADOWS = 2, OG_HIDE_BACKDROP = 4,
    OG_HIDE_ACTOR_DRAW = 8, OG_HIDE_BACKGROUND_Z = 16
};
unsigned og_xgb35_HiddenPasses(void);
bool og_xgb35_TogglePass(SDL_Keycode key);

/* The caller keeps textures, vertices and indices alive through End. */
void og_xgb35_BeginMenuGeometry(SDL_Renderer *renderer);
void og_xgb35_EndMenuGeometry(void);

/* Frames stay alive and immutable until loading ends. */
void og_xgb35_BeginLoading(SDL_Renderer *renderer, SDL_Texture *const *frames, size_t count,
                           unsigned ticks_per_second, unsigned first_frame);
void og_xgb35_EndLoading(void);

typedef void (*OgMixAudio)(void *userdata, float *samples, size_t count);
SDL_AudioStream *og_xgb35_OpenMixer(const SDL_AudioSpec *spec, OgMixAudio mix, void *userdata);

#endif
