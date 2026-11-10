/* Opt-in weapon-triggered per-pixel lights for the SDL OpenGL world path. */
#ifndef ONI_WORLD_LIGHT_SDL_H
#define ONI_WORLD_LIGHT_SDL_H
#if defined(__linux__) && UUmSDL
#include "Oni_ExperimentalLights.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct OniWorldLight {
 SDL_GLContext context;
 int attempted;
 UUtUns32 capacity;
 GLuint program;
 GLint oldProgram, screen0, screen1, screen2, invw, world0, world1, world2, lights, lightColors, lightCount, relight, animationTime, fogEnabled, fogMode, renderPass;
 PFNGLCREATESHADERPROC createShader;
 PFNGLSHADERSOURCEPROC shaderSource;
 PFNGLCOMPILESHADERPROC compileShader;
 PFNGLGETSHADERIVPROC getShaderiv;
 PFNGLGETSHADERINFOLOGPROC getShaderInfoLog;
 PFNGLDELETESHADERPROC deleteShader;
 PFNGLCREATEPROGRAMPROC createProgram;
 PFNGLATTACHSHADERPROC attachShader;
 PFNGLLINKPROGRAMPROC linkProgram;
 PFNGLGETPROGRAMIVPROC getProgramiv;
 PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog;
 PFNGLDELETEPROGRAMPROC deleteProgram;
 PFNGLUSEPROGRAMPROC useProgram;
 PFNGLGETUNIFORMLOCATIONPROC uniformLocation;
 PFNGLUNIFORM1IPROC uniform1i;
 PFNGLUNIFORM1FPROC uniform1f;
 PFNGLUNIFORM2FPROC uniform2f;
 PFNGLUNIFORM3FPROC uniform3f;
 PFNGLUNIFORM4FVPROC uniform4fv;
} OniWorldLight;
static OniWorldLight oni_world_light;

static GLuint oni_world_light_shader(GLenum type,const char *source){
 GLuint s=oni_world_light.createShader(type);GLint ok=0;
 if(!s)return 0;
 oni_world_light.shaderSource(s,1,&source,NULL);oni_world_light.compileShader(s);
 oni_world_light.getShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[1024]={0};oni_world_light.getShaderInfoLog(s,sizeof(log),NULL,log);
  UUrStartupMessage("[world-light] shader compilation failed: %s",log);
  oni_world_light.deleteShader(s);return 0;}
 return s;
}
// Choose a bounded GLSL light array once per context. Older implementations
// may expose too few fragment uniform components for the larger variant.
static UUtUns32 oni_world_light_capacity(void) {
 SDL_GLContext context=SDL_GL_GetCurrentContext();
 if(!context)return 8;
 if(oni_world_light.context!=context){memset(&oni_world_light,0,sizeof(oni_world_light));oni_world_light.context=context;}
 if(!oni_world_light.capacity){
  GLint components=0;glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS,&components);
  oni_world_light.capacity=components>=192?ONI_MAX_EXPERIMENTAL_LIGHTS:8;
 }
 return oni_world_light.capacity;
}
static int oni_world_light_try_program(UUtUns32 capacity,const char *vertex,const char *fragmentBody) {
 char fragment[4096];
 int length=snprintf(fragment,sizeof(fragment),"#version 120\n#define ONI_LIGHT_COUNT %u\n%s",(unsigned)capacity,fragmentBody);
 if(length<0 || (size_t)length>=sizeof(fragment))return 0;
 GLuint vs=oni_world_light_shader(GL_VERTEX_SHADER,vertex),fs=oni_world_light_shader(GL_FRAGMENT_SHADER,fragment);
 if(!vs || !fs){if(vs)oni_world_light.deleteShader(vs);if(fs)oni_world_light.deleteShader(fs);return 0;}
 GLuint program=oni_world_light.createProgram();
 if(!program){oni_world_light.deleteShader(vs);oni_world_light.deleteShader(fs);return 0;}
 oni_world_light.attachShader(program,vs);oni_world_light.attachShader(program,fs);
 oni_world_light.linkProgram(program);oni_world_light.deleteShader(vs);oni_world_light.deleteShader(fs);
 GLint ok=0;oni_world_light.getProgramiv(program,GL_LINK_STATUS,&ok);
 if(!ok){char log[1024]={0};oni_world_light.getProgramInfoLog(program,sizeof(log),NULL,log);
  UUrStartupMessage("[world-light] %u-light shader link failed: %s",(unsigned)capacity,log);
  oni_world_light.deleteProgram(program);return 0;}
 oni_world_light.program=program;return 1;
}
static int oni_world_light_init(void){
 const char *setting=getenv("ONI_WORLD_LIGHT");
 const char *warehouse=getenv("ONI_WAREHOUSE_LIGHT"), *relight=getenv("ONI_RELIGHT");
 if((!setting || strcmp(setting,"1")) && (!warehouse || strcmp(warehouse,"1")) && (!relight || strcmp(relight,"1")))return 0;
#define LOAD_GL(field,type,name) do { oni_world_light.field=(type)SDL_GL_GetProcAddress(name); \
 if(!oni_world_light.field){UUrStartupMessage("[world-light] %s unavailable",name);return 0;} }while(0)
 LOAD_GL(createShader,PFNGLCREATESHADERPROC,"glCreateShader");
 LOAD_GL(shaderSource,PFNGLSHADERSOURCEPROC,"glShaderSource");
 LOAD_GL(compileShader,PFNGLCOMPILESHADERPROC,"glCompileShader");
 LOAD_GL(getShaderiv,PFNGLGETSHADERIVPROC,"glGetShaderiv");
 LOAD_GL(getShaderInfoLog,PFNGLGETSHADERINFOLOGPROC,"glGetShaderInfoLog");
 LOAD_GL(deleteShader,PFNGLDELETESHADERPROC,"glDeleteShader");
 LOAD_GL(createProgram,PFNGLCREATEPROGRAMPROC,"glCreateProgram");
 LOAD_GL(attachShader,PFNGLATTACHSHADERPROC,"glAttachShader");
 LOAD_GL(linkProgram,PFNGLLINKPROGRAMPROC,"glLinkProgram");
 LOAD_GL(getProgramiv,PFNGLGETPROGRAMIVPROC,"glGetProgramiv");
 LOAD_GL(getProgramInfoLog,PFNGLGETPROGRAMINFOLOGPROC,"glGetProgramInfoLog");
 LOAD_GL(deleteProgram,PFNGLDELETEPROGRAMPROC,"glDeleteProgram");
 LOAD_GL(useProgram,PFNGLUSEPROGRAMPROC,"glUseProgram");
 LOAD_GL(uniformLocation,PFNGLGETUNIFORMLOCATIONPROC,"glGetUniformLocation");
 LOAD_GL(uniform1i,PFNGLUNIFORM1IPROC,"glUniform1i");
 LOAD_GL(uniform1f,PFNGLUNIFORM1FPROC,"glUniform1f");
 LOAD_GL(uniform2f,PFNGLUNIFORM2FPROC,"glUniform2f");
 LOAD_GL(uniform3f,PFNGLUNIFORM3FPROC,"glUniform3f");
 LOAD_GL(uniform4fv,PFNGLUNIFORM4FVPROC,"glUniform4fv");
#undef LOAD_GL
 const char *vertex=
  "#version 120\n"
  "varying vec2 screen;varying vec4 uv;varying vec4 colour;varying float fogDepth;\n"
  "void main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;screen=gl_Vertex.xy;"
  "uv=gl_TextureMatrix[0]*gl_MultiTexCoord0;colour=gl_Color;fogDepth=abs((gl_ModelViewMatrix*gl_Vertex).z);}\n";
 const char *fragmentBody=
  "uniform sampler2D base;uniform vec2 s0,s1,s2;uniform vec3 invw,p0,p1,p2;uniform vec4 lights[ONI_LIGHT_COUNT];uniform vec4 lightColors[ONI_LIGHT_COUNT];uniform int lightCount;uniform float relight;"
  "uniform int fogEnabled,fogMode,renderPass;uniform float animationTime;varying vec2 screen;varying vec4 uv;varying vec4 colour;varying float fogDepth;\n"
  "void main(){vec2 a=s1-s0,b=s2-s0,c=screen-s0;float det=a.x*b.y-a.y*b.x;"
  "float y=(c.x*b.y-c.y*b.x)/det,z=(a.x*c.y-a.y*c.x)/det;"
  "vec3 w=vec3(1.0-y-z,y,z)*invw;float sum=w.x+w.y+w.z;"
  "vec4 sample=texture2DProj(base,uv);vec4 tex=sample*colour;tex.rgb=sample.rgb*mix(colour.rgb,colour.rgb*0.40+vec3(0.08),relight);"
  "if(abs(sum)>0.000001){vec3 pos=(p0*w.x+p1*w.y+p2*w.z)/sum;"
  "vec3 rawN=cross(p1-p0,p2-p0);float nlen=length(rawN);vec3 amount=vec3(0.0),totalAmount=vec3(0.0);"
  "if(nlen>0.00001){vec3 normal=rawN/nlen;for(int i=0;i<ONI_LIGHT_COUNT;++i){if(i>=lightCount)break;"
  "vec3 delta=lights[i].xyz-pos;float d=length(delta);if(d>0.00001){"
  "float radius=abs(lightColors[i].a);float beam=1.0;"
  "if(lightColors[i].a<0.0){float phase=dot(lights[i].xyz,vec3(.013,.017,.019));float angle=animationTime*2.6+phase+(lightColors[i].g>0.25?3.14159265:0.0);"
  "vec3 dir=vec3(cos(angle),0.0,sin(angle));beam=0.03+0.97*smoothstep(0.05,0.70,dot(-delta/d,dir));}"
  "float facing=abs(dot(normal,delta/d));float falloff=1.0-smoothstep(6.0,max(6.01,radius),d);"
  "vec3 contribution=2.0*abs(lights[i].w)*facing*falloff*falloff*beam*lightColors[i].rgb;"
  "totalAmount+=contribution;if(renderPass==0||lights[i].w>=0.0)amount+=contribution;}}}"
  "vec3 multiplier=sample.rgb*mix(colour.rgb,vec3(0.70),relight);vec3 added=amount*multiplier;"
  "vec3 headroom=max(vec3(1.0)-tex.rgb,vec3(0.001));vec3 ratios=totalAmount*multiplier/headroom;"
  "float peak=max(ratios.r,max(ratios.g,ratios.b));"
  "if(peak>0.5){float soft=0.5+0.45*(1.0-exp(-(peak-0.5)/0.45));added*=soft/peak;}"
  "if(renderPass==2)tex.rgb=added;else tex.rgb=clamp(tex.rgb+added,0.0,1.0);}"
  "else if(renderPass==2)tex.rgb=vec3(0.0);"
  "if(fogEnabled!=0){float f=1.0;if(fogMode==2048)f=exp(-gl_Fog.density*fogDepth);"
  "else if(fogMode==2049){float d=gl_Fog.density*fogDepth;f=exp(-d*d);}"
  "else f=(gl_Fog.end-fogDepth)/max(gl_Fog.end-gl_Fog.start,0.00001);"
  "if(renderPass==2)tex.rgb*=clamp(f,0.0,1.0);else tex.rgb=mix(gl_Fog.color.rgb,tex.rgb,clamp(f,0.0,1.0));}gl_FragColor=tex;}\n";
 UUtUns32 capacity=oni_world_light_capacity();
 if(!oni_world_light_try_program(capacity,vertex,fragmentBody)) {
  if(capacity<=8)return 0;
  UUrStartupMessage("[world-light] falling back to 8 lights per surface");
  oni_world_light.capacity=capacity=8;
  if(!oni_world_light_try_program(capacity,vertex,fragmentBody))return 0;
 }
#define LOC(name) oni_world_light.uniformLocation(oni_world_light.program,name)
 oni_world_light.screen0=LOC("s0");oni_world_light.screen1=LOC("s1");oni_world_light.screen2=LOC("s2");
 oni_world_light.invw=LOC("invw");oni_world_light.world0=LOC("p0");oni_world_light.world1=LOC("p1");
 oni_world_light.world2=LOC("p2");oni_world_light.lights=LOC("lights[0]");oni_world_light.lightColors=LOC("lightColors[0]");oni_world_light.relight=LOC("relight");oni_world_light.lightCount=LOC("lightCount");oni_world_light.fogEnabled=LOC("fogEnabled");
 oni_world_light.fogMode=LOC("fogMode");oni_world_light.animationTime=LOC("animationTime");oni_world_light.renderPass=LOC("renderPass");
 GLint old=0;glGetIntegerv(GL_CURRENT_PROGRAM,&old);oni_world_light.useProgram(oni_world_light.program);
 oni_world_light.uniform1i(LOC("base"),0);oni_world_light.useProgram((GLuint)old);
#undef LOC
 UUrStartupMessage("[world-light] experimental lighting enabled (up to %u lights per surface)",(unsigned)oni_world_light.capacity);
 return 1;
}
static int oni_world_light_begin(const M3tPointScreen *s0,const M3tPointScreen *s1,const M3tPointScreen *s2,
 const M3tPoint3D *p0,const M3tPoint3D *p1,const M3tPoint3D *p2,
 const OniExperimentalLight *lights,UUtUns32 lightCount,UUtUns32 renderPass){
 SDL_GLContext context=SDL_GL_GetCurrentContext();if(!context)return 0;
 if(oni_world_light.context!=context){memset(&oni_world_light,0,sizeof(oni_world_light));oni_world_light.context=context;}
 if(!oni_world_light.attempted){oni_world_light.attempted=1;if(!oni_world_light_init())return 0;}
 if(!oni_world_light.program)return 0;
 float ax=s1->x-s0->x,ay=s1->y-s0->y,bx=s2->x-s0->x,by=s2->y-s0->y;
 // Vertices outside the viewport, including behind the eye, still carry
 // signed reciprocal W. The clipped polygon remains on the same plane.
 if(!isfinite(s0->x)||!isfinite(s0->y)||!isfinite(s0->invW) ||
    !isfinite(s1->x)||!isfinite(s1->y)||!isfinite(s1->invW) ||
    !isfinite(s2->x)||!isfinite(s2->y)||!isfinite(s2->invW) ||
    fabsf(ax*by-ay*bx)<0.001f || fabsf(s0->invW)<1e-12f ||
    fabsf(s1->invW)<1e-12f || fabsf(s2->invW)<1e-12f)return 0;
 GLint mode=GL_LINEAR;glGetIntegerv(GL_CURRENT_PROGRAM,&oni_world_light.oldProgram);
 oni_world_light.useProgram(oni_world_light.program);
 oni_world_light.uniform2f(oni_world_light.screen0,s0->x,s0->y);
 oni_world_light.uniform2f(oni_world_light.screen1,s1->x,s1->y);
 oni_world_light.uniform2f(oni_world_light.screen2,s2->x,s2->y);
 oni_world_light.uniform3f(oni_world_light.invw,s0->invW,s1->invW,s2->invW);
 oni_world_light.uniform3f(oni_world_light.world0,p0->x,p0->y,p0->z);
 oni_world_light.uniform3f(oni_world_light.world1,p1->x,p1->y,p1->z);
 oni_world_light.uniform3f(oni_world_light.world2,p2->x,p2->y,p2->z);
 GLfloat packed[ONI_MAX_EXPERIMENTAL_LIGHTS*4]={0}, colors[ONI_MAX_EXPERIMENTAL_LIGHTS*4]={0};
 if(lightCount>oni_world_light.capacity)lightCount=oni_world_light.capacity;
 for(UUtUns32 i=0;i<lightCount;++i){packed[i*4]=lights[i].position.x;packed[i*4+1]=lights[i].position.y;
  packed[i*4+2]=lights[i].position.z;packed[i*4+3]=lights[i].intensity;
  colors[i*4]=lights[i].red;colors[i*4+1]=lights[i].green;colors[i*4+2]=lights[i].blue;colors[i*4+3]=lights[i].spinning?-lights[i].radius:lights[i].radius;}
 oni_world_light.uniform1f(oni_world_light.animationTime,ONrExperimentalLights_AnimationTime());
 oni_world_light.uniform1i(oni_world_light.lightCount,(GLint)lightCount);
 oni_world_light.uniform1i(oni_world_light.renderPass,(GLint)renderPass);
 oni_world_light.uniform4fv(oni_world_light.lights,(GLsizei)lightCount,packed);
 oni_world_light.uniform4fv(oni_world_light.lightColors,(GLsizei)lightCount,colors);
 oni_world_light.uniform1f(oni_world_light.relight,ONrExperimentalLights_SceneEnabled()?1.0f:0.0f);
 oni_world_light.uniform1i(oni_world_light.fogEnabled,glIsEnabled(GL_FOG)?1:0);
 glGetIntegerv(GL_FOG_MODE,&mode);oni_world_light.uniform1i(oni_world_light.fogMode,mode);
 return 1;
}
static void oni_world_light_end(void){oni_world_light.useProgram((GLuint)oni_world_light.oldProgram);}
#endif
#endif
