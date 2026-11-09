/* Optional Linux compatibility-profile post-processing. The original renderer
 * still draws the complete frame; this GPU-only pass copies and grades it. */
#ifndef ONI_POSTPROCESS_SDL_H
#define ONI_POSTPROCESS_SDL_H
#if defined(__linux__)
#include <SDL2/SDL_opengl_glext.h>
#include <stdlib.h>
#include <string.h>

typedef struct OniPostprocess {
    SDL_GLContext context;
    int attempted, mode, width, height, maxSize;
    GLint sceneLocation, gradeLocation;
    GLuint program, texture;
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
    PFNGLGETUNIFORMLOCATIONPROC getUniformLocation;
    PFNGLUNIFORM1IPROC uniform1i;
    PFNGLUNIFORM1FPROC uniform1f;
    PFNGLACTIVETEXTUREPROC activeTexture;
} OniPostprocess;
static OniPostprocess oni_post;

static GLuint oni_post_shader(GLenum type, const char *source)
{
    GLuint shader=oni_post.createShader(type);
    GLint ok=0;
    if (!shader) return 0;
    oni_post.shaderSource(shader,1,&source,NULL);
    oni_post.compileShader(shader);
    oni_post.getShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if (!ok) {
        char log[1024]={0};
        oni_post.getShaderInfoLog(shader,sizeof(log),NULL,log);
        UUrStartupMessage("[shader] compile failed: %s",log);
        oni_post.deleteShader(shader);
        return 0;
    }
    return shader;
}

static int oni_post_init(void)
{
    const char *setting=getenv("ONI_SHADER");
    if (!setting || !*setting || !strcmp(setting,"0") || !strcmp(setting,"off")) return 0;
    if (strcmp(setting,"1") && strcmp(setting,"warm") && strcmp(setting,"neutral")) {
        UUrStartupMessage("[shader] unknown ONI_SHADER value; use warm, neutral or off");
        return 0;
    }
    oni_post.mode=strcmp(setting,"neutral")!=0;
#define ONI_GL_LOAD(field, type, name) do { \
    oni_post.field=(type)SDL_GL_GetProcAddress(name); \
    if (!oni_post.field) { UUrStartupMessage("[shader] %s unavailable; using original renderer",name); return 0; } \
} while(0)
    ONI_GL_LOAD(createShader,PFNGLCREATESHADERPROC,"glCreateShader");
    ONI_GL_LOAD(shaderSource,PFNGLSHADERSOURCEPROC,"glShaderSource");
    ONI_GL_LOAD(compileShader,PFNGLCOMPILESHADERPROC,"glCompileShader");
    ONI_GL_LOAD(getShaderiv,PFNGLGETSHADERIVPROC,"glGetShaderiv");
    ONI_GL_LOAD(getShaderInfoLog,PFNGLGETSHADERINFOLOGPROC,"glGetShaderInfoLog");
    ONI_GL_LOAD(deleteShader,PFNGLDELETESHADERPROC,"glDeleteShader");
    ONI_GL_LOAD(createProgram,PFNGLCREATEPROGRAMPROC,"glCreateProgram");
    ONI_GL_LOAD(attachShader,PFNGLATTACHSHADERPROC,"glAttachShader");
    ONI_GL_LOAD(linkProgram,PFNGLLINKPROGRAMPROC,"glLinkProgram");
    ONI_GL_LOAD(getProgramiv,PFNGLGETPROGRAMIVPROC,"glGetProgramiv");
    ONI_GL_LOAD(getProgramInfoLog,PFNGLGETPROGRAMINFOLOGPROC,"glGetProgramInfoLog");
    ONI_GL_LOAD(deleteProgram,PFNGLDELETEPROGRAMPROC,"glDeleteProgram");
    ONI_GL_LOAD(useProgram,PFNGLUSEPROGRAMPROC,"glUseProgram");
    ONI_GL_LOAD(getUniformLocation,PFNGLGETUNIFORMLOCATIONPROC,"glGetUniformLocation");
    ONI_GL_LOAD(uniform1i,PFNGLUNIFORM1IPROC,"glUniform1i");
    ONI_GL_LOAD(uniform1f,PFNGLUNIFORM1FPROC,"glUniform1f");
    ONI_GL_LOAD(activeTexture,PFNGLACTIVETEXTUREPROC,"glActiveTexture");
#undef ONI_GL_LOAD
    const char *vertex=
        "#version 120\n"
        "varying vec2 uv;\n"
        "void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}\n";
    const char *fragment=
        "#version 120\n"
        "uniform sampler2D scene;uniform float grade;varying vec2 uv;\n"
        "void main(){vec4 c=texture2D(scene,uv);"
        "vec3 warm=clamp((c.rgb-0.5)*1.08+0.5,0.0,1.0)*vec3(1.035,1.0,0.965);"
        "gl_FragColor=vec4(mix(c.rgb,clamp(warm,0.0,1.0),grade),c.a);}\n";
    GLuint vs=oni_post_shader(GL_VERTEX_SHADER,vertex), fs=oni_post_shader(GL_FRAGMENT_SHADER,fragment);
    if (!vs || !fs) {
        if (vs) oni_post.deleteShader(vs);
        if (fs) oni_post.deleteShader(fs);
        return 0;
    }
    oni_post.program=oni_post.createProgram();
    if (!oni_post.program) { oni_post.deleteShader(vs); oni_post.deleteShader(fs); return 0; }
    oni_post.attachShader(oni_post.program,vs); oni_post.attachShader(oni_post.program,fs);
    oni_post.linkProgram(oni_post.program);
    oni_post.deleteShader(vs); oni_post.deleteShader(fs);
    GLint ok=0; oni_post.getProgramiv(oni_post.program,GL_LINK_STATUS,&ok);
    if (!ok) {
        char log[1024]={0}; oni_post.getProgramInfoLog(oni_post.program,sizeof(log),NULL,log);
        UUrStartupMessage("[shader] link failed: %s",log);
        oni_post.deleteProgram(oni_post.program); oni_post.program=0; return 0;
    }
    oni_post.sceneLocation=oni_post.getUniformLocation(oni_post.program,"scene");
    oni_post.gradeLocation=oni_post.getUniformLocation(oni_post.program,"grade");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&oni_post.maxSize);
    glGenTextures(1,&oni_post.texture);
    if (!oni_post.texture) { oni_post.deleteProgram(oni_post.program); oni_post.program=0; return 0; }
    UUrStartupMessage("[shader] experimental post-processing enabled: %s",oni_post.mode?"warm":"neutral");
    return 1;
}

static void oni_post_draw(SDL_Window *window)
{
    SDL_GLContext context=SDL_GL_GetCurrentContext();
    if (!context) return;
    if (oni_post.context!=context) { memset(&oni_post,0,sizeof(oni_post)); oni_post.context=context; }
    if (!oni_post.attempted) { oni_post.attempted=1; if (!oni_post_init()) return; }
    if (!oni_post.program || !oni_post.texture) return;
    int width=0,height=0; SDL_GL_GetDrawableSize(window,&width,&height);
    if (width<=0 || height<=0 || width>oni_post.maxSize || height>oni_post.maxSize) return;
    GLint oldProgram=0,oldActive=0; glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram); glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    oni_post.activeTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,oni_post.texture);
    if (oni_post.width!=width || oni_post.height!=height) {
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        GLint allocated=0; glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&allocated);
        if (allocated!=width) { glPopAttrib(); oni_post.activeTexture(oldActive); return; }
        oni_post.width=width; oni_post.height=height;
    }
    glReadBuffer(GL_BACK); glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
    glViewport(0,0,width,height);
    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_CLIP_PLANE0); glDisable(GL_CLIP_PLANE1); glDisable(GL_CLIP_PLANE2);
    glDisable(GL_CLIP_PLANE3); glDisable(GL_CLIP_PLANE4); glDisable(GL_CLIP_PLANE5);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_FALSE);
    oni_post.useProgram(oni_post.program);
    oni_post.uniform1i(oni_post.sceneLocation,0);
    oni_post.uniform1f(oni_post.gradeLocation,oni_post.mode?1.0f:0.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(-1,-1); glTexCoord2f(1,0); glVertex2f(1,-1);
    glTexCoord2f(1,1); glVertex2f(1,1); glTexCoord2f(0,1); glVertex2f(-1,1);
    glEnd();
    oni_post.useProgram((GLuint)oldProgram); glPopAttrib(); oni_post.activeTexture((GLenum)oldActive);
}

static void oni_post_dispose(void)
{
    if (oni_post.context==SDL_GL_GetCurrentContext()) {
        if (oni_post.texture) glDeleteTextures(1,&oni_post.texture);
        if (oni_post.program && oni_post.deleteProgram) oni_post.deleteProgram(oni_post.program);
    }
    memset(&oni_post,0,sizeof(oni_post));
}
#endif
#endif
