#ifndef OG_SGX_SCENE_H
#define OG_SGX_SCENE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x,y,u,v,r,g,b; } SceneVertex;
typedef struct {
    SceneVertex v[3];
    const uint8_t *pixels;
    int tw,th,x0,y0,x1,y1,flags;
    float k;
} SceneTriangle;
typedef struct { int x,y; } SceneSlot;
typedef int ScenePage(const SceneTriangle *,int,int,int,SceneSlot *,const uint8_t **,int *,bool *);
typedef bool SceneShadow(uint8_t *,int,const float *,int);
typedef struct { float x,y,plane; } SceneShadowVertex;
typedef struct {
    const float *mask;
    const SceneShadowVertex *tiles;
    int mask_count, tile_count;
    float intensity, bias;
    bool perspective;
} SceneResident;
typedef bool SceneModels(const SceneTriangle *,const float *,int,int,int,uint8_t *,float *,uint8_t *,const SceneResident *);
ScenePage og_sgx_scene_page;
SceneShadow og_sgx_scene_shadow;
void og_sgx_scene_close(bool last);
#endif
