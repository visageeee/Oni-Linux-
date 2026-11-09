#ifndef ONI_EXPERIMENTAL_LIGHTS_H
#define ONI_EXPERIMENTAL_LIGHTS_H
#include "BFW.h"
#include "BFW_Motoko.h"
#define ONI_MAX_EXPERIMENTAL_LIGHTS 8
typedef struct OniExperimentalLight {
    M3tPoint3D position;
    float intensity;
    float radius;
    float red, green, blue;
} OniExperimentalLight;
#if defined(__linux__) && UUmSDL
UUtBool ONrExperimentalLights_WarehouseEnabled(void);
UUtUns32 ONrExperimentalLights_Get(OniExperimentalLight *out, UUtUns32 capacity);
UUtUns32 ONrExperimentalLights_GetAt(const M3tPoint3D *position, OniExperimentalLight *out, UUtUns32 capacity);
#endif
#endif
