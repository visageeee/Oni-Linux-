/* Optional scene-depth fog, applied before the game's HUD. */
#ifndef ONI_DISTANCE_FOG_SDL_H
#define ONI_DISTANCE_FOG_SDL_H
#if defined(__linux__) && UUmSDL
#include <SDL2/SDL_opengl_glext.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct OniDistanceFog {
    SDL_GLContext context;
    int attempted, width, height, max_size;
    GLuint program, color, depth;
    GLint scene_location, depth_location, near_far_location, params_location;
    float start, end, strength;
    PFNGLCREATESHADERPROC create_shader;
    PFNGLSHADERSOURCEPROC shader_source;
    PFNGLCOMPILESHADERPROC compile_shader;
    PFNGLGETSHADERIVPROC get_shader_iv;
    PFNGLGETSHADERINFOLOGPROC get_shader_log;
    PFNGLDELETESHADERPROC delete_shader;
    PFNGLCREATEPROGRAMPROC create_program;
    PFNGLATTACHSHADERPROC attach_shader;
    PFNGLLINKPROGRAMPROC link_program;
    PFNGLGETPROGRAMIVPROC get_program_iv;
    PFNGLGETPROGRAMINFOLOGPROC get_program_log;
    PFNGLDELETEPROGRAMPROC delete_program;
    PFNGLUSEPROGRAMPROC use_program;
    PFNGLGETUNIFORMLOCATIONPROC uniform_location;
    PFNGLUNIFORM1IPROC uniform1i;
    PFNGLUNIFORM2FPROC uniform2f;
    PFNGLUNIFORM3FPROC uniform3f;
    PFNGLACTIVETEXTUREPROC active_texture;
} OniDistanceFog;
static OniDistanceFog oni_fog;

static float oni_fog_setting(const char *name, float fallback, float min, float max)
{
    const char *value=getenv(name); char *end=NULL;
    if (!value || !*value) return fallback;
    float result=strtof(value,&end);
    if (end==value || *end || !isfinite(result) || result<min || result>max) {
        UUrStartupMessage("[distance-fog] invalid %s; using default",name);
        return fallback;
    }
    return result;
}
static GLuint oni_fog_shader(GLenum type, const char *source)
{
    GLuint shader=oni_fog.create_shader(type); GLint ok=0;
    if (!shader) return 0;
    oni_fog.shader_source(shader,1,&source,NULL); oni_fog.compile_shader(shader);
    oni_fog.get_shader_iv(shader,GL_COMPILE_STATUS,&ok);
    if (!ok) {
        char log[1024]={0}; oni_fog.get_shader_log(shader,sizeof(log),NULL,log);
        UUrStartupMessage("[distance-fog] shader compilation failed: %s",log);
        oni_fog.delete_shader(shader); return 0;
    }
    return shader;
}
static int oni_fog_init(void)
{
#define FOG_LOAD(field,type,name) do { oni_fog.field=(type)SDL_GL_GetProcAddress(name); \
    if (!oni_fog.field) { UUrStartupMessage("[distance-fog] %s unavailable",name); return 0; } } while (0)
    FOG_LOAD(create_shader,PFNGLCREATESHADERPROC,"glCreateShader");
    FOG_LOAD(shader_source,PFNGLSHADERSOURCEPROC,"glShaderSource");
    FOG_LOAD(compile_shader,PFNGLCOMPILESHADERPROC,"glCompileShader");
    FOG_LOAD(get_shader_iv,PFNGLGETSHADERIVPROC,"glGetShaderiv");
    FOG_LOAD(get_shader_log,PFNGLGETSHADERINFOLOGPROC,"glGetShaderInfoLog");
    FOG_LOAD(delete_shader,PFNGLDELETESHADERPROC,"glDeleteShader");
    FOG_LOAD(create_program,PFNGLCREATEPROGRAMPROC,"glCreateProgram");
    FOG_LOAD(attach_shader,PFNGLATTACHSHADERPROC,"glAttachShader");
    FOG_LOAD(link_program,PFNGLLINKPROGRAMPROC,"glLinkProgram");
    FOG_LOAD(get_program_iv,PFNGLGETPROGRAMIVPROC,"glGetProgramiv");
    FOG_LOAD(get_program_log,PFNGLGETPROGRAMINFOLOGPROC,"glGetProgramInfoLog");
    FOG_LOAD(delete_program,PFNGLDELETEPROGRAMPROC,"glDeleteProgram");
    FOG_LOAD(use_program,PFNGLUSEPROGRAMPROC,"glUseProgram");
    FOG_LOAD(uniform_location,PFNGLGETUNIFORMLOCATIONPROC,"glGetUniformLocation");
    FOG_LOAD(uniform1i,PFNGLUNIFORM1IPROC,"glUniform1i");
    FOG_LOAD(uniform2f,PFNGLUNIFORM2FPROC,"glUniform2f");
    FOG_LOAD(uniform3f,PFNGLUNIFORM3FPROC,"glUniform3f");
    FOG_LOAD(active_texture,PFNGLACTIVETEXTUREPROC,"glActiveTexture");
#undef FOG_LOAD
    const char *vertex="#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}\n";
    /* Oni stores projected z/w directly in the GL depth buffer. It is not
       OpenGL's usual window-depth remapping: reconstruct from that NDC z. */
    const char *fragment="#version 120\n"
        "uniform sampler2D scene,depthMap;uniform vec2 nearFar;uniform vec3 params;varying vec2 uv;"
        "void main(){vec4 c=texture2D(scene,uv);float z=texture2D(depthMap,uv).r;"
        "if(z>=0.999999){gl_FragColor=vec4(mix(c.rgb,vec3(0.30,0.33,0.38),params.z),c.a);return;}"
        "float n=nearFar.x,f=nearFar.y;float d=(2.0*n*f)/max(f+n-z*(f-n),0.00001);"
        "float haze=smoothstep(params.x,params.y,d)*params.z;"
        "gl_FragColor=vec4(mix(c.rgb,vec3(0.30,0.33,0.38),haze),c.a);}\n";
    GLuint vs=oni_fog_shader(GL_VERTEX_SHADER,vertex),fs=oni_fog_shader(GL_FRAGMENT_SHADER,fragment);
    if (!vs || !fs) { if(vs)oni_fog.delete_shader(vs);if(fs)oni_fog.delete_shader(fs);return 0; }
    oni_fog.program=oni_fog.create_program();
    if (!oni_fog.program) {oni_fog.delete_shader(vs);oni_fog.delete_shader(fs);return 0;}
    oni_fog.attach_shader(oni_fog.program,vs);oni_fog.attach_shader(oni_fog.program,fs);
    oni_fog.link_program(oni_fog.program);oni_fog.delete_shader(vs);oni_fog.delete_shader(fs);
    GLint ok=0;oni_fog.get_program_iv(oni_fog.program,GL_LINK_STATUS,&ok);
    if (!ok) {char log[1024]={0};oni_fog.get_program_log(oni_fog.program,sizeof(log),NULL,log);
        UUrStartupMessage("[distance-fog] shader link failed: %s",log);
        oni_fog.delete_program(oni_fog.program);oni_fog.program=0;return 0;}
    oni_fog.scene_location=oni_fog.uniform_location(oni_fog.program,"scene");
    oni_fog.depth_location=oni_fog.uniform_location(oni_fog.program,"depthMap");
    oni_fog.near_far_location=oni_fog.uniform_location(oni_fog.program,"nearFar");
    oni_fog.params_location=oni_fog.uniform_location(oni_fog.program,"params");
    oni_fog.start=oni_fog_setting("ONI_FOG_START",180.f,0.f,100000.f);
    oni_fog.end=oni_fog_setting("ONI_FOG_END",1400.f,1.f,100000.f);
    oni_fog.strength=oni_fog_setting("ONI_FOG_STRENGTH",0.55f,0.f,1.f);
    if(oni_fog.end<=oni_fog.start){UUrStartupMessage("[distance-fog] end must exceed start; using defaults");oni_fog.start=180.f;oni_fog.end=1400.f;}
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&oni_fog.max_size);
    glGenTextures(1,&oni_fog.color);glGenTextures(1,&oni_fog.depth);
    if(!oni_fog.color || !oni_fog.depth) return 0;
    UUrStartupMessage("[distance-fog] enabled: start %.0f, end %.0f, strength %.2f",oni_fog.start,oni_fog.end,oni_fog.strength);
    return 1;
}
static void oni_fog_dispose(void)
{
    if (oni_fog.context==SDL_GL_GetCurrentContext()) {
        if(oni_fog.color)glDeleteTextures(1,&oni_fog.color);
        if(oni_fog.depth)glDeleteTextures(1,&oni_fog.depth);
        if(oni_fog.program && oni_fog.delete_program)oni_fog.delete_program(oni_fog.program);
    }
    memset(&oni_fog,0,sizeof(oni_fog));
}
static void oni_fog_draw(float near_plane, float far_plane)
{
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("ONI_DISTANCE_FOG");enabled=v && !strcmp(v,"1");}
    if(!enabled || near_plane<=0.f || far_plane<=near_plane) return;
    SDL_GLContext context=SDL_GL_GetCurrentContext();if(!context)return;
    if(oni_fog.context!=context){memset(&oni_fog,0,sizeof(oni_fog));oni_fog.context=context;}
    if(!oni_fog.attempted){oni_fog.attempted=1;if(!oni_fog_init())return;}
    if(!oni_fog.program || !oni_fog.color || !oni_fog.depth)return;
    GLint viewport[4],old_program=0,old_active=0,old0=0,old1=0;
    glGetIntegerv(GL_VIEWPORT,viewport);int width=viewport[2],height=viewport[3];
    if(width<=0 || height<=0 || width>oni_fog.max_size || height>oni_fog.max_size)return;
    glGetIntegerv(GL_CURRENT_PROGRAM,&old_program);glGetIntegerv(GL_ACTIVE_TEXTURE,&old_active);
    oni_fog.active_texture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&old0);
    oni_fog.active_texture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&old1);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    oni_fog.active_texture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,oni_fog.color);
    if(oni_fog.width!=width || oni_fog.height!=height){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        oni_fog.active_texture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,oni_fog.depth);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
        GLint cw=0,dw=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&dw);
        oni_fog.active_texture(GL_TEXTURE0);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&cw);
        if(cw!=width || dw!=width){UUrStartupMessage("[distance-fog] texture allocation failed");goto restore;}
        oni_fog.width=width;oni_fog.height=height;
    }
    GLboolean double_buffer=GL_FALSE;glGetBooleanv(GL_DOUBLEBUFFER,&double_buffer);glReadBuffer(double_buffer?GL_BACK:GL_FRONT);
    oni_fog.active_texture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,oni_fog.color);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],width,height);
    oni_fog.active_texture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,oni_fog.depth);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],width,height);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glDisable(GL_FOG);
    glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_CLIP_PLANE2);
    glDisable(GL_CLIP_PLANE3);glDisable(GL_CLIP_PLANE4);glDisable(GL_CLIP_PLANE5);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_FALSE);
    oni_fog.use_program(oni_fog.program);oni_fog.uniform1i(oni_fog.scene_location,0);oni_fog.uniform1i(oni_fog.depth_location,1);
    oni_fog.uniform2f(oni_fog.near_far_location,near_plane,far_plane);oni_fog.uniform3f(oni_fog.params_location,oni_fog.start,oni_fog.end,oni_fog.strength);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    oni_fog.use_program((GLuint)old_program);
restore:
    glPopAttrib();
    oni_fog.active_texture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,(GLuint)old0);
    oni_fog.active_texture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,(GLuint)old1);
    oni_fog.active_texture((GLenum)old_active);
}
#endif
#endif
