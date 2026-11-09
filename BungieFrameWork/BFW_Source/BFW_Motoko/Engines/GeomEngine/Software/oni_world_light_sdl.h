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
#include <string.h>

typedef struct OniWorldLight {
 SDL_GLContext context;
 int attempted;
 GLuint program;
 GLint oldProgram, screen0, screen1, screen2, invw, world0, world1, world2, lights, lightColors, lightCount, relight, fogEnabled, fogMode;
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
static int oni_world_light_init(void){
 const char *setting=getenv("ONI_WORLD_LIGHT");
 const char *warehouse=getenv("ONI_WAREHOUSE_LIGHT");
 if((!setting || strcmp(setting,"1")) && (!warehouse || strcmp(warehouse,"1")))return 0;
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
 const char *fragment=
  "#version 120\n"
  "uniform sampler2D base;uniform vec2 s0,s1,s2;uniform vec3 invw,p0,p1,p2;uniform vec4 lights[8];uniform vec4 lightColors[8];uniform int lightCount;uniform float relight;"
  "uniform int fogEnabled,fogMode;varying vec2 screen;varying vec4 uv;varying vec4 colour;varying float fogDepth;\n"
  "void main(){vec2 a=s1-s0,b=s2-s0,c=screen-s0;float det=a.x*b.y-a.y*b.x;"
  "float y=(c.x*b.y-c.y*b.x)/det,z=(a.x*c.y-a.y*c.x)/det;"
  "vec3 w=vec3(1.0-y-z,y,z)*invw;float sum=w.x+w.y+w.z;"
  "vec4 sample=texture2DProj(base,uv);vec4 tex=sample*colour;tex.rgb=sample.rgb*mix(colour.rgb,colour.rgb*0.40+vec3(0.08),relight);"
  "if(abs(sum)>0.000001){vec3 pos=(p0*w.x+p1*w.y+p2*w.z)/sum;"
  "vec3 rawN=cross(p1-p0,p2-p0);float nlen=length(rawN);vec3 amount=vec3(0.0);"
  "if(nlen>0.00001){vec3 normal=rawN/nlen;for(int i=0;i<8;++i){if(i>=lightCount)break;"
  "vec3 delta=lights[i].xyz-pos;float d=length(delta);if(d>0.00001){"
  "float facing=abs(dot(normal,delta/d));float falloff=1.0-smoothstep(6.0,max(6.01,lightColors[i].a),d);"
  "amount+=2.0*lights[i].w*facing*falloff*falloff*lightColors[i].rgb;}}}"
  "tex.rgb=clamp(tex.rgb+sample.rgb*amount*mix(colour.rgb,vec3(0.70),relight),0.0,1.0);}"
  "if(fogEnabled!=0){float f=1.0;if(fogMode==2048)f=exp(-gl_Fog.density*fogDepth);"
  "else if(fogMode==2049){float d=gl_Fog.density*fogDepth;f=exp(-d*d);}"
  "else f=(gl_Fog.end-fogDepth)/max(gl_Fog.end-gl_Fog.start,0.00001);"
  "tex.rgb=mix(gl_Fog.color.rgb,tex.rgb,clamp(f,0.0,1.0));}gl_FragColor=tex;}\n";
 GLuint vs=oni_world_light_shader(GL_VERTEX_SHADER,vertex),fs=oni_world_light_shader(GL_FRAGMENT_SHADER,fragment);
 if(!vs || !fs){if(vs)oni_world_light.deleteShader(vs);if(fs)oni_world_light.deleteShader(fs);return 0;}
 oni_world_light.program=oni_world_light.createProgram();
 if(!oni_world_light.program){oni_world_light.deleteShader(vs);oni_world_light.deleteShader(fs);return 0;}
 oni_world_light.attachShader(oni_world_light.program,vs);oni_world_light.attachShader(oni_world_light.program,fs);
 oni_world_light.linkProgram(oni_world_light.program);oni_world_light.deleteShader(vs);oni_world_light.deleteShader(fs);
 GLint ok=0;oni_world_light.getProgramiv(oni_world_light.program,GL_LINK_STATUS,&ok);
 if(!ok){char log[1024]={0};oni_world_light.getProgramInfoLog(oni_world_light.program,sizeof(log),NULL,log);
  UUrStartupMessage("[world-light] shader link failed: %s",log);
  oni_world_light.deleteProgram(oni_world_light.program);oni_world_light.program=0;return 0;}
#define LOC(name) oni_world_light.uniformLocation(oni_world_light.program,name)
 oni_world_light.screen0=LOC("s0");oni_world_light.screen1=LOC("s1");oni_world_light.screen2=LOC("s2");
 oni_world_light.invw=LOC("invw");oni_world_light.world0=LOC("p0");oni_world_light.world1=LOC("p1");
 oni_world_light.world2=LOC("p2");oni_world_light.lights=LOC("lights[0]");oni_world_light.lightColors=LOC("lightColors[0]");oni_world_light.relight=LOC("relight");oni_world_light.lightCount=LOC("lightCount");oni_world_light.fogEnabled=LOC("fogEnabled");
 oni_world_light.fogMode=LOC("fogMode");
 GLint old=0;glGetIntegerv(GL_CURRENT_PROGRAM,&old);oni_world_light.useProgram(oni_world_light.program);
 oni_world_light.uniform1i(LOC("base"),0);oni_world_light.useProgram((GLuint)old);
#undef LOC
 UUrStartupMessage("[world-light] experimental weapon lights enabled (opaque world surfaces)");
 return 1;
}
static int oni_world_light_begin(const M3tPointScreen *s0,const M3tPointScreen *s1,const M3tPointScreen *s2,
 const M3tPoint3D *p0,const M3tPoint3D *p1,const M3tPoint3D *p2,
 const OniExperimentalLight *lights,UUtUns32 lightCount){
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
 if(lightCount>ONI_MAX_EXPERIMENTAL_LIGHTS)lightCount=ONI_MAX_EXPERIMENTAL_LIGHTS;
 for(UUtUns32 i=0;i<lightCount;++i){packed[i*4]=lights[i].position.x;packed[i*4+1]=lights[i].position.y;
  packed[i*4+2]=lights[i].position.z;packed[i*4+3]=lights[i].intensity;
  colors[i*4]=lights[i].red;colors[i*4+1]=lights[i].green;colors[i*4+2]=lights[i].blue;colors[i*4+3]=lights[i].radius;}
 oni_world_light.uniform1i(oni_world_light.lightCount,(GLint)lightCount);
 oni_world_light.uniform4fv(oni_world_light.lights,(GLsizei)lightCount,packed);
 oni_world_light.uniform4fv(oni_world_light.lightColors,(GLsizei)lightCount,colors);
 oni_world_light.uniform1f(oni_world_light.relight,ONrExperimentalLights_WarehouseEnabled()?1.0f:0.0f);
 oni_world_light.uniform1i(oni_world_light.fogEnabled,glIsEnabled(GL_FOG)?1:0);
 glGetIntegerv(GL_FOG_MODE,&mode);oni_world_light.uniform1i(oni_world_light.fogMode,mode);
 return 1;
}
static void oni_world_light_end(void){oni_world_light.useProgram((GLuint)oni_world_light.oldProgram);}
#endif
#endif
