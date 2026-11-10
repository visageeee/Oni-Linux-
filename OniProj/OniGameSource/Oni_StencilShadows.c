/* Experimental animated-character stencil shadow volumes for SDL OpenGL. */
#include "Oni_StencilShadows.h"
#if defined(__linux__) && UUmSDL
#include "Oni_ExperimentalLights.h"
#include "Oni_Character.h"
#include "BFW_TemplateManager.h"
#include "BFW_MathLib.h"
#include "../../BungieFrameWork/BFW_Source/BFW_Motoko/Manager/Motoko_Manager.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ONI_SHADOW_MAX_GROUPS 4
#define ONI_SHADOW_MAX_CHARACTERS 16
#define ONI_SHADOW_LIGHTS_PER_CHARACTER 2
#define ONI_SHADOW_MAX_VOLUMES (ONI_SHADOW_MAX_CHARACTERS * ONI_SHADOW_LIGHTS_PER_CHARACTER)
/* Select up to two local sources per character and share a stencil mask
   between all characters shadowed by the same physical source. */
typedef struct OniShadowCaster {
    const void *character;
    const TRtBody *body;
    M3tMatrix4x3 pose[ONcNumCharacterParts];
    M3tPoint3D light;
    float radius;
    UUtInt32 group; /* -1 is contact fallback, -2 exceeds the light budget. */
} OniShadowCaster;
static OniShadowCaster oniCasters[ONI_SHADOW_MAX_VOLUMES];
static UUtUns32 oniCasterCount,oniShadowCharacters,oniGroupCount,oniRenderPass,oniActiveGroup;
static M3tPoint3D oniGroups[ONI_SHADOW_MAX_GROUPS];
extern UUtError ONrStencilShadows_ReplayWorld(void);
extern UUtBool ONrStencilShadows_WorldShaderReady(void);
static UUtBool ONiShadowSameSource(M3tPoint3D a,M3tPoint3D b) {
    float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z<.01f;
}
UUtUns32 ONrStencilShadows_RenderPass(void) {return oniRenderPass;}
// Keep every light for the common brightness calculation. A negative
// intensity marks contributions omitted by this particular replay pass.
UUtUns32 ONrStencilShadows_MarkLights(OniExperimentalLight *lights,UUtUns32 count) {
    if(!oniRenderPass)return count;
    UUtUns32 included=0;
    for(UUtUns32 i=0;i<count;++i) {
        UUtBool match=UUcFalse;
        if(oniRenderPass==1) {
            for(UUtUns32 g=0;g<oniGroupCount;++g)if(ONiShadowSameSource(lights[i].position,oniGroups[g])){match=UUcTrue;break;}
            match=!match;
        } else {
            match=oniActiveGroup<oniGroupCount && ONiShadowSameSource(lights[i].position,oniGroups[oniActiveGroup]);
        }
        if(match)++included;else lights[i].intensity=-fabsf(lights[i].intensity);
    }
    return included;
}
static int oniShadowEnabled=-1;
static UUtBool ONiStencilEnabled(void) {
    if(oniShadowEnabled<0) {const char *v=getenv("ONI_STENCIL_SHADOWS");oniShadowEnabled=v && !strcmp(v,"1");}
    return oniShadowEnabled;
}
static UUtBool ONiStencilAvailable(void) {
    static SDL_GLContext checkedContext=NULL;static UUtBool available=UUcFalse;
    SDL_GLContext context=SDL_GL_GetCurrentContext();
    if(!ONiStencilEnabled() || !context)return UUcFalse;
    if(context!=checkedContext) {
        GLint bits=0;int major=0,minor=0;checkedContext=context;
        glGetIntegerv(GL_STENCIL_BITS,&bits);
        const char *version=(const char *)glGetString(GL_VERSION),*ext=(const char *)glGetString(GL_EXTENSIONS);
        if(version)sscanf(version,"%d.%d",&major,&minor);
        available=bits>=8 && SDL_GL_GetProcAddress("glStencilOpSeparate") && SDL_GL_GetProcAddress("glUseProgram") &&
            (major>3 || (major==3 && minor>=2) || (ext && strstr(ext,"GL_ARB_depth_clamp")));
        if(!available)UUrStartupMessage("[stencil-shadow] unavailable; retaining original character shadows");
    }
    return available;
}
void ONrStencilShadows_Begin(void) {oniCasterCount=0;oniShadowCharacters=0;oniGroupCount=0;oniRenderPass=0;}
UUtBool ONrStencilShadows_Queued(const void *character) {
    if(!character)return UUcFalse;
    for(UUtUns32 i=0;i<oniCasterCount;++i)if(oniCasters[i].character==character)return UUcTrue;
    return UUcFalse;
}
void ONrStencilShadows_Queue(const void *character,const TRtBody *body,const M3tMatrix4x3 *pose,const M3tPoint3D *position) {
    if(!character || !ONiStencilAvailable() || !body || !pose || !position || oniShadowCharacters>=ONI_SHADOW_MAX_CHARACTERS ||
       !body->geometries || body->numParts>ONcNumCharacterParts)return;
    M3tPoint3D center=*position;center.y+=8.f;
    OniExperimentalLight lights[ONI_MAX_EXPERIMENTAL_LIGHTS];
    UUtUns32 n=ONrStencilShadows_WorldShaderReady()?ONrExperimentalLights_GetAt(&center,lights,ONI_MAX_EXPERIMENTAL_LIGHTS):0;
    typedef struct OniShadowSource { UUtUns32 index; float contribution; } OniShadowSource;
    OniShadowSource sources[ONI_MAX_EXPERIMENTAL_LIGHTS];UUtUns32 sourceCount=0;
    for(UUtUns32 i=0;i<n;++i) {
        float x=center.x-lights[i].position.x,y=center.y-lights[i].position.y,z=center.z-lights[i].position.z;
        float d=sqrtf(x*x+y*y+z*z);if(d<1.f || d>=lights[i].radius)continue;
        float t=UUmPin((d-6.f)/UUmMax(.01f,lights[i].radius-6.f),0.f,1.f);
        float falloff=1.f-t*t*(3.f-2.f*t);
        float value=lights[i].intensity*falloff*falloff*UUmMax(lights[i].red,UUmMax(lights[i].green,lights[i].blue));
        if(lights[i].spinning) {
            float phase=lights[i].position.x*.013f+lights[i].position.y*.017f+lights[i].position.z*.019f;
            float angle=ONrExperimentalLights_AnimationTime()*2.6f+phase+(lights[i].green>.25f?3.14159265f:0.f);
            float beam=UUmPin(((x*cosf(angle)+z*sinf(angle))/d-.05f)/.65f,0.f,1.f);
            value*=.03f+.97f*beam*beam*(3.f-2.f*beam);
        }
        if(value<=0.f)continue;
        // Multiple colour components at one beacon represent the same
        // physical shadow direction, not separate overlapping shadows.
        UUtUns32 j;
        for(j=0;j<sourceCount;++j) {
            M3tPoint3D p=lights[sources[j].index].position;
            float dx=p.x-lights[i].position.x,dy=p.y-lights[i].position.y,dz=p.z-lights[i].position.z;
            if(dx*dx+dy*dy+dz*dz<1.f)break;
        }
        if(j==sourceCount){sources[j].index=i;sources[j].contribution=0.f;++sourceCount;}
        sources[j].contribution+=value;
    }
    float strongest=0.f;for(UUtUns32 i=0;i<sourceCount;++i)if(sources[i].contribution>strongest)strongest=sources[i].contribution;
    UUtBool queued=UUcFalse;
    for(UUtUns32 slot=0;slot<ONI_SHADOW_LIGHTS_PER_CHARACTER && oniCasterCount<ONI_SHADOW_MAX_VOLUMES;++slot) {
        UUtInt32 selected=-1;float best=0.f;
        for(UUtUns32 i=0;i<sourceCount;++i)if(sources[i].contribution>best){best=sources[i].contribution;selected=(UUtInt32)i;}
        if(selected<0 || best<.01f)break;
        UUtUns32 light=sources[selected].index;sources[selected].contribution=0.f;
        UUtUns32 group=0;while(group<oniGroupCount && !ONiShadowSameSource(oniGroups[group],lights[light].position))++group;
        if(group==oniGroupCount) {if(oniGroupCount>=ONI_SHADOW_MAX_GROUPS)continue;oniGroups[oniGroupCount++]=lights[light].position;}
        OniShadowCaster *c=&oniCasters[oniCasterCount++];c->character=character;c->body=body;c->group=(UUtInt32)group;
        memcpy(c->pose,pose,body->numParts*sizeof(*pose));c->light=lights[light].position;
        c->radius=lights[light].radius;queued=UUcTrue;
    }
    if(!queued && oniCasterCount<ONI_SHADOW_MAX_VOLUMES) {
        // Neutral overhead light keeps an actual mesh-shaped contact shadow
        // in areas where no supported dynamic light reaches the character.
        OniShadowCaster *c=&oniCasters[oniCasterCount++];c->character=character;c->body=body;c->group=-1;
        memcpy(c->pose,pose,body->numParts*sizeof(*pose));
        c->light=center;c->light.x-=8.f;c->light.y+=80.f;c->light.z-=8.f;
        c->radius=105.f;
        // If real lights exceed the global budget, keep them unshadowed
        // instead of falsely darkening their combined illumination.
        if(strongest>=.01f)c->group=-2;
        queued=UUcTrue;
    }
    if(queued)++oniShadowCharacters;
}
static M3tPoint3D ONiShadowExtrude(M3tPoint3D p,M3tPoint3D light,float radius) {
    float x=p.x-light.x,y=p.y-light.y,z=p.z-light.z,d=sqrtf(x*x+y*y+z*z);
    if(d<.001f)return p;
    // Vertices already beyond the requested extrusion remain in place.
    float scale=UUmMax(1.f,radius/d);
    M3tPoint3D q={light.x+x*scale,light.y+y*scale,light.z+z*scale};return q;
}
static void ONiShadowVertex(const M3tMatrix4x4 *m,M3tPoint3D p) {
    float x=m->m[0][0]*p.x+m->m[1][0]*p.y+m->m[2][0]*p.z+m->m[3][0];
    float y=m->m[0][1]*p.x+m->m[1][1]*p.y+m->m[2][1]*p.z+m->m[3][1];
    float z=m->m[0][2]*p.x+m->m[1][2]*p.y+m->m[2][2]*p.z+m->m[3][2];
    float w=m->m[0][3]*p.x+m->m[1][3]*p.y+m->m[2][3]*p.z+m->m[3][3];
    /* Oni's existing depth buffer uses z/w in [0,1]. */
    glVertex4f(x,y,2.f*z-w,w);
}
static void ONiShadowTri(const M3tMatrix4x4 *m,M3tPoint3D a,M3tPoint3D b,M3tPoint3D c) {
    ONiShadowVertex(m,a);ONiShadowVertex(m,b);ONiShadowVertex(m,c);
}
static void ONiShadowVolume(const OniShadowCaster *caster,const M3tMatrix4x4 *camera) {
    for(UUtUns32 part=0;part<caster->body->numParts;++part) {
        const M3tGeometry *g=caster->body->geometries[part];
        if(!g || !g->pointArray || !g->triStripArray || !g->triNormalIndexArray || !g->triNormalArray)continue;
        UUtUns32 n=g->triNormalIndexArray->numIndices, pos=0,a=0,b=0;
        for(UUtUns32 i=0;i<n && pos<g->triStripArray->numIndices;++i) {
            UUtUns32 c=g->triStripArray->indices[pos++];
            if(c&0x80000000u) {a=c&0x7fffffffu;if(pos+1>=g->triStripArray->numIndices)break;b=g->triStripArray->indices[pos++];c=g->triStripArray->indices[pos++];}
            UUtUns32 ia=a,ib=b,ic=c;a=b;b=c;
            if(ia>=g->pointArray->numPoints || ib>=g->pointArray->numPoints || ic>=g->pointArray->numPoints)continue;
            UUtUns32 ni=g->triNormalIndexArray->indices[i];if(ni>=g->triNormalArray->numVectors)continue;
            M3tPoint3D p0,p1,p2;M3tVector3D normal;
            MUrMatrix_MultiplyPoint(&g->pointArray->points[ia],&caster->pose[part],&p0);
            MUrMatrix_MultiplyPoint(&g->pointArray->points[ib],&caster->pose[part],&p1);
            MUrMatrix_MultiplyPoint(&g->pointArray->points[ic],&caster->pose[part],&p2);
            MUrMatrix_MultiplyNormal(&g->triNormalArray->vectors[ni],&caster->pose[part],&normal);
            if(normal.x*(caster->light.x-p0.x)+normal.y*(caster->light.y-p0.y)+normal.z*(caster->light.z-p0.z)<=0.f)continue;
            float ux=p1.x-p0.x,uy=p1.y-p0.y,uz=p1.z-p0.z,vx=p2.x-p0.x,vy=p2.y-p0.y,vz=p2.z-p0.z;
            float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            if(nx*nx+ny*ny+nz*nz<1e-10f)continue;
            if(nx*normal.x+ny*normal.y+nz*normal.z<0.f){M3tPoint3D tmp=p1;p1=p2;p2=tmp;}
            // Extend real-light volumes well beyond the light radius. The
            // lighting shader fades their contribution to zero at the radius;
            // a near cap would cut shadows off before that smooth fade.
            float farRadius=caster->group>=0?caster->radius*4.f:caster->radius;
            M3tPoint3D e0=ONiShadowExtrude(p0,caster->light,farRadius),e1=ONiShadowExtrude(p1,caster->light,farRadius),e2=ONiShadowExtrude(p2,caster->light,farRadius);
            ONiShadowTri(camera,p0,p1,p2);ONiShadowTri(camera,e2,e1,e0);
            ONiShadowTri(camera,p1,p0,e0);ONiShadowTri(camera,p1,e0,e1);
            ONiShadowTri(camera,p2,p1,e1);ONiShadowTri(camera,p2,e1,e2);
            ONiShadowTri(camera,p0,p2,e2);ONiShadowTri(camera,p0,e2,e0);
        }
    }
}
// Construct one mask for all casters affected by the same physical source.
static void ONiShadowMask(UUtInt32 group,const M3tMatrix4x4 *camera,PFNGLSTENCILOPSEPARATEPROC op,PFNGLUSEPROGRAMPROC useProgram) {
    GLint oldProgram=0,oldMatrix=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_MATRIX_MODE,&oldMatrix);
    glPushAttrib(GL_ALL_ATTRIB_BITS);useProgram(0);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glDisable(GL_TEXTURE_2D);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glEnable(GL_STENCIL_TEST);glEnable(GL_DEPTH_CLAMP);
    glStencilMask(0xff);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glStencilFunc(GL_ALWAYS,0,0xff);op(GL_FRONT,GL_KEEP,GL_DECR_WRAP,GL_KEEP);op(GL_BACK,GL_KEEP,GL_INCR_WRAP,GL_KEEP);
    glBegin(GL_TRIANGLES);for(UUtUns32 i=0;i<oniCasterCount;++i)if(oniCasters[i].group==group)ONiShadowVolume(&oniCasters[i],camera);glEnd();
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode((GLenum)oldMatrix);glPopAttrib();useProgram((GLuint)oldProgram);
}
void ONrStencilShadows_Draw(void) {
    if(!ONiStencilAvailable() || !oniCasterCount)return;
    static int logged=0;
    PFNGLSTENCILOPSEPARATEPROC op=(PFNGLSTENCILOPSEPARATEPROC)SDL_GL_GetProcAddress("glStencilOpSeparate");
    PFNGLUSEPROGRAMPROC useProgram=(PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    M3tGeomCamera *camera=NULL;M3rCamera_GetActive(&camera);if(!camera || !op || !useProgram)return;
    M3tManager_GeomCamera *cam=(M3tManager_GeomCamera *)camera;M3rManager_Camera_UpdateMatrices(cam);
    PFNGLACTIVETEXTUREPROC activeTexture=(PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    GLint oldTexture=GL_TEXTURE0,textureUnits=1;
    if(activeTexture){glGetIntegerv(GL_ACTIVE_TEXTURE,&oldTexture);glGetIntegerv(GL_MAX_TEXTURE_UNITS,&textureUnits);}
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    if(oniGroupCount && ONrStencilShadows_WorldShaderReady()) {
        // Recompose depth-visible world surfaces without the selected sources.
        // Unselected lights and the original base remain in this pass.
        M3rMatrixStack_Push();M3rMatrixStack_Identity();
        oniRenderPass=1;ONrStencilShadows_ReplayWorld();
        for(UUtUns32 g=0;g<oniGroupCount;++g) {
            ONiShadowMask((UUtInt32)g,&cam->matrix_worldToFrustum,op,useProgram);
            glEnable(GL_STENCIL_TEST);glStencilMask(0);glStencilFunc(GL_EQUAL,0,0xff);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
            oniActiveGroup=g;oniRenderPass=2;ONrStencilShadows_ReplayWorld();
        }
        oniRenderPass=0;M3rMatrixStack_Pop();
    }
    // The requested no-light contact shadow has no real light contribution to
    // mask. Retain a separate, faint fallback solely for those characters.
    UUtBool fallback=UUcFalse;for(UUtUns32 i=0;i<oniCasterCount;++i)if(oniCasters[i].group<0){fallback=UUcTrue;break;}
    if(fallback) {
        ONiShadowMask(-1,&cam->matrix_worldToFrustum,op,useProgram);
        GLint oldProgram=0,oldMatrix=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_MATRIX_MODE,&oldMatrix);useProgram(0);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        if(activeTexture)for(GLint i=0;i<textureUnits;++i){activeTexture(GL_TEXTURE0+i);glDisable(GL_TEXTURE_2D);}
        glDisable(GL_TEXTURE_2D);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_DEPTH_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_SCISSOR_TEST);
        glEnable(GL_STENCIL_TEST);glStencilMask(0);glStencilFunc(GL_NOTEQUAL,0,0xff);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0.f,0.f,0.f,.22f);
        glBegin(GL_QUADS);glVertex2f(-1.f,-1.f);glVertex2f(1.f,-1.f);glVertex2f(1.f,1.f);glVertex2f(-1.f,1.f);glEnd();
        glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode((GLenum)oldMatrix);useProgram((GLuint)oldProgram);
    }
    oniRenderPass=0;glPopAttrib();if(activeTexture)activeTexture((GLenum)oldTexture);
    if(!logged){UUrStartupMessage("[stencil-shadow] per-light world replay enabled (up to %u lights)",(unsigned)ONI_SHADOW_MAX_GROUPS);logged=1;}
}
#endif
