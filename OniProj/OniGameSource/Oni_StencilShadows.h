#ifndef ONI_STENCIL_SHADOWS_H
#define ONI_STENCIL_SHADOWS_H
#include "BFW.h"
#include "BFW_Totoro.h"
#include "Oni_ExperimentalLights.h"
#if defined(__linux__) && UUmSDL
void ONrStencilShadows_Begin(void);
void ONrStencilShadows_Queue(const void *character, const TRtBody *body, const M3tMatrix4x3 *pose, const M3tPoint3D *position);
UUtBool ONrStencilShadows_Queued(const void *character);
void ONrStencilShadows_Draw(void);
/* 0 = normal rendering, 1 = base replay, 2 = one shadowed light. */
UUtUns32 ONrStencilShadows_RenderPass(void);
UUtUns32 ONrStencilShadows_MarkLights(OniExperimentalLight *lights,UUtUns32 count);
#endif
#endif
