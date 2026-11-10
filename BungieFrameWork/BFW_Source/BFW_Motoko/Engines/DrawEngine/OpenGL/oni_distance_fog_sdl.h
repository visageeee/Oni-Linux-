/* Optional depth-based fog and ambient occlusion, applied before the HUD. */
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
    GLint ao_location, pixel_location, projection_location;
    float start, end, strength, ao_radius, ao_strength, ao_bias;
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
        UUrStartupMessage("[scene-depth] invalid %s; using default",name);
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
        "uniform sampler2D scene,depthMap;uniform vec2 nearFar,pixelSize,projection;"
        "uniform vec3 params,ao;varying vec2 uv;"
        "float linearDepth(float z){float n=nearFar.x,f=nearFar.y;return (2.0*n*f)/max(f+n-z*(f-n),0.00001);}"
        "vec3 viewPos(vec2 p,float z){float d=linearDepth(z);return vec3((p*2.0-1.0)*projection*d,-d);}"
        "float sampleAO(vec2 p,vec3 center,vec3 normal){"
        "if(p.x<0.0||p.y<0.0||p.x>1.0||p.y>1.0)return 0.0;"
        "float z=texture2D(depthMap,p).r;if(z>=0.999999)return 0.0;"
        "vec3 delta=viewPos(p,z)-center;float d=length(delta);"
        "if(d<0.001||d>=ao.x)return 0.0;"
        "float facing=max(dot(normal,delta/d)-ao.z,0.0);"
        "return facing*(1.0-smoothstep(ao.x*0.3,ao.x,d));}"
        "void main(){vec4 c=texture2D(scene,uv);float z=texture2D(depthMap,uv).r;"
        "if(z>=0.999999){gl_FragColor=vec4(mix(c.rgb,vec3(0.30,0.33,0.38),params.z),c.a);return;}"
        "float d=linearDepth(z);"
        "if(ao.y>0.0){vec3 p=viewPos(uv,z);"
        "vec2 dx=vec2(pixelSize.x,0.0),dy=vec2(0.0,pixelSize.y);"
        "float zl=texture2D(depthMap,uv-dx).r,zr=texture2D(depthMap,uv+dx).r;"
        "float zd=texture2D(depthMap,uv-dy).r,zu=texture2D(depthMap,uv+dy).r;"
        "vec3 vl=viewPos(uv-dx,zl),vr=viewPos(uv+dx,zr),vd=viewPos(uv-dy,zd),vu=viewPos(uv+dy,zu);"
        "vec3 tx=abs(zl-z)<abs(zr-z)?p-vl:vr-p;vec3 ty=abs(zd-z)<abs(zu-z)?p-vd:vu-p;"
        "vec3 n=cross(tx,ty);float nl=length(n);if(nl>0.00001){n/=nl;if(dot(n,-p)<0.0)n=-n;"
        "float pixels=ao.x/(2.0*max(d,0.001)*projection.y*pixelSize.y);"
        "float rad=min(pixels,48.0);float visible=smoothstep(0.5,2.0,pixels);"
        "float phase=0.30*sin(gl_FragCoord.x*0.025)*sin(gl_FragCoord.y*0.025);float sum=0.0;"
        "for(int i=0;i<12;i++){float fi=float(i);float a=phase+mod(fi,6.0)*1.04719755;"
        "float ring=fi<6.0?0.45:1.0;vec2 offset=vec2(cos(a),sin(a))*rad*ring*pixelSize;"
        "sum+=sampleAO(uv+offset,p,n);}"
        "float occlusion=clamp(sum*(ao.y/12.0)*visible,0.0,0.65);c.rgb*=1.0-occlusion;}}"
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
    oni_fog.ao_location=oni_fog.uniform_location(oni_fog.program,"ao");
    oni_fog.pixel_location=oni_fog.uniform_location(oni_fog.program,"pixelSize");
    oni_fog.projection_location=oni_fog.uniform_location(oni_fog.program,"projection");
    oni_fog.start=oni_fog_setting("ONI_FOG_START",180.f,0.f,100000.f);
    oni_fog.end=oni_fog_setting("ONI_FOG_END",1400.f,1.f,100000.f);
    oni_fog.strength=oni_fog_setting("ONI_FOG_STRENGTH",0.55f,0.f,1.f);
    oni_fog.ao_radius=oni_fog_setting("ONI_AO_RADIUS",12.f,1.f,60.f);
    oni_fog.ao_strength=oni_fog_setting("ONI_AO_STRENGTH",0.9f,0.f,2.f);
    oni_fog.ao_bias=oni_fog_setting("ONI_AO_BIAS",0.08f,0.f,0.5f);
    if(oni_fog.end<=oni_fog.start){UUrStartupMessage("[distance-fog] end must exceed start; using defaults");oni_fog.start=180.f;oni_fog.end=1400.f;}
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&oni_fog.max_size);
    glGenTextures(1,&oni_fog.color);glGenTextures(1,&oni_fog.depth);
    if(!oni_fog.color || !oni_fog.depth) return 0;
    UUrStartupMessage("[scene-depth] effects initialized; AO radius %.1f, strength %.2f, bias %.2f",oni_fog.ao_radius,oni_fog.ao_strength,oni_fog.ao_bias);
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
static void oni_fog_draw(float near_plane, float far_plane, float fov_y, float aspect)
{
    static int enabled=-1,ao_enabled=-1;
    if(enabled<0){const char *v=getenv("ONI_DISTANCE_FOG");enabled=v && !strcmp(v,"1");}
    if(ao_enabled<0){const char *v=getenv("ONI_AO");ao_enabled=v && !strcmp(v,"1");}
    if((!enabled && !ao_enabled) || near_plane<=0.f || far_plane<=near_plane ||
       !isfinite(fov_y) || fov_y<=0.f || fov_y>=3.13f || !isfinite(aspect) || aspect<=0.f)return;
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
    float half_tan=tanf(fov_y*.5f);
    oni_fog.uniform2f(oni_fog.near_far_location,near_plane,far_plane);
    oni_fog.uniform2f(oni_fog.pixel_location,1.f/width,1.f/height);
    oni_fog.uniform2f(oni_fog.projection_location,half_tan*aspect,half_tan);
    oni_fog.uniform3f(oni_fog.params_location,oni_fog.start,oni_fog.end,enabled?oni_fog.strength:0.f);
    oni_fog.uniform3f(oni_fog.ao_location,oni_fog.ao_radius,ao_enabled?oni_fog.ao_strength:0.f,oni_fog.ao_bias);
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
