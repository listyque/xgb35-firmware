#include "sgx_sprites.h"
#include "sgx_scene.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

enum { batch_quads=384 };
static EGLDisplay display;
static EGLContext context;
static EGLSurface surface;
static GLuint program, white, quad_indices;
static GLint scale, transform, flip_y;
static int screen_w, screen_h, viewport_x, viewport_y, viewport_width, viewport_height, logical_w, logical_h;
static SDL_Vertex quads[batch_quads*4];
static unsigned queued, key_texture;
static uint32_t key_blend;
static SDL_Rect key_clip;
static SDL_Rect active_clip;
static bool scissor_enabled;
static unsigned bound_texture;
static uint32_t active_blend;
static SDL_FRect active_transform;
static float active_flip;
static unsigned frame_serial;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind_image;
static OgGpuTexture shadow_mask;
static bool shadow_failed;
static void destroy_texture(OgGpuTexture *t);
static void destroy_models(void);

static void bind_texture(unsigned id) {
    if (id==bound_texture) return;
    glBindTexture(GL_TEXTURE_2D,id);
    bound_texture=id;
}

static void filter(bool linear) {
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);
}

static void blend(uint32_t mode) {
    if (mode==active_blend) return;
    active_blend=mode;
    if (!mode) glDisable(GL_BLEND);
    else {
        glEnable(GL_BLEND);
        switch (mode) {
        case SDL_BLENDMODE_ADD: glBlendFunc(GL_SRC_ALPHA,GL_ONE); break;
        case 0x10000001u: glBlendFunc(GL_ONE,GL_ONE); break;
        case 0x10000002u: glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR); break;
        case 0x10000003u: glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR); break;
        case 0x10000004u: glBlendFunc(GL_DST_COLOR,GL_ZERO); break;
        default: glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA); break;
        }
    }
}

static void unclip(void) {
    if (!scissor_enabled) return;
    glDisable(GL_SCISSOR_TEST);
    scissor_enabled=false;
}

static void state(unsigned tex, uint32_t mode, SDL_Rect clip) {
    bind_texture(tex?tex:white);
    blend(mode);
    if (scissor_enabled && !memcmp(&clip,&active_clip,sizeof(clip))) return;
    if (!scissor_enabled) glEnable(GL_SCISSOR_TEST);
    scissor_enabled=true;
    active_clip=clip;
    int x0=clip.x*viewport_width/logical_w, x1=(clip.x+clip.w)*viewport_width/logical_w;
    int y0=(logical_h-clip.y-clip.h)*viewport_height/logical_h, y1=(logical_h-clip.y)*viewport_height/logical_h;
    glScissor(viewport_x+x0,viewport_y+y0,x1-x0,y1-y0);
}

static void matrix(SDL_FRect rect,bool flip) {
    if (memcmp(&rect,&active_transform,sizeof(rect))) {
        glUniform4f(transform,rect.x,rect.y,rect.w,rect.h);
        active_transform=rect;
    }
    float direction=flip?-1:1;
    if (direction!=active_flip) {
        glUniform1f(flip_y,direction);
        active_flip=direction;
    }
}

static void arrays(const SDL_Vertex *v) {
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(*v),&v->position);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(*v),&v->tex_coord);
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(*v),&v->color);
}

static void flush(void) {
    if (!queued) return;
    state(key_texture,key_blend,key_clip);
    matrix((SDL_FRect){0,0,1,1},false);
    arrays(quads);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,quad_indices);
    glDrawElements(GL_TRIANGLES,queued*6,GL_UNSIGNED_SHORT,0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    queued=0;
}

static void close_renderer(void) {
    if (display) {
        if (context && eglGetCurrentContext()==context) {
            destroy_models();
            destroy_texture(&shadow_mask);
            eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        }
        if (surface) eglDestroySurface(display,surface);
        if (context) eglDestroyContext(display,context);
        eglTerminate(display);
    }
    queued=0;
    shadow_mask=(OgGpuTexture){0};
    shadow_failed=false;
    display=EGL_NO_DISPLAY;
    surface=EGL_NO_SURFACE;
    context=EGL_NO_CONTEXT;
}

static bool shader(GLuint target_program, GLenum kind, const char *source) {
    GLuint s=glCreateShader(kind);
    GLint ok=0;
    glShaderSource(s,1,&source,NULL);
    glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if (ok) glAttachShader(target_program,s);
    glDeleteShader(s);
    return ok;
}

static bool open_renderer(int index) {
    const EGLint cfg[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,5,EGL_GREEN_SIZE,6,EGL_BLUE_SIZE,5,EGL_NONE};
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLConfig config;
    EGLint n,w,h;
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!display || !eglInitialize(display,NULL,NULL) || !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(display,cfg,&config,1,&n) || !n) goto fail;
    surface=eglCreateWindowSurface(display,config,(EGLNativeWindowType)(uintptr_t)(index+1),NULL);
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);
    if (!surface || !context || !eglMakeCurrent(display,surface,surface,context) ||
        !eglQuerySurface(display,surface,EGL_WIDTH,&w) || !eglQuerySurface(display,surface,EGL_HEIGHT,&h))
        goto fail;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    if (!renderer || !strstr(renderer,"PowerVR SGX 540")) goto fail;
    screen_w=w; screen_h=h;
    active_blend=0; scissor_enabled=false; bound_texture=0;
    active_transform=(SDL_FRect){0}; active_flip=0;
    frame_serial=1;
    create_image=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    destroy_image=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    bind_image=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    program=glCreateProgram();
    if (!shader(program,GL_VERTEX_SHADER,"attribute vec2 p,t;attribute vec4 c;uniform vec2 scale;"
        "uniform vec4 transform;uniform float flipY;"
        "varying mediump vec2 uv;varying lowp vec4 color;"
        "void main(){vec2 a=p*transform.zw+transform.xy;uv=vec2(t.x,(1.-flipY)*.5+t.y*flipY);"
        "color=c;gl_Position=vec4(a.x*scale.x-1.,1.-a.y*scale.y,0.,1.);}") ||
        !shader(program,GL_FRAGMENT_SHADER,"precision mediump float;uniform sampler2D img;"
        "varying mediump vec2 uv;varying lowp vec4 color;"
        "void main(){gl_FragColor=texture2D(img,uv)*color;}")) goto fail;
    glBindAttribLocation(program,0,"p"); glBindAttribLocation(program,1,"t"); glBindAttribLocation(program,2,"c");
    glLinkProgram(program);
    GLint linked=0;
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if (!linked) goto fail;
    glUseProgram(program);
    scale=glGetUniformLocation(program,"scale");
    transform=glGetUniformLocation(program,"transform");
    flip_y=glGetUniformLocation(program,"flipY");
    for (int i=0;i<3;i++) glEnableVertexAttribArray(i);
    glDisable(GL_DITHER);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const uint8_t pixel[]={255,255,255,255};
    glGenTextures(1,&white); bind_texture(white); filter(false);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLushort indices[batch_quads*6];
    for (int i=0;i<batch_quads;i++) {
        int v=i*4, j=i*6;
        indices[j]=v; indices[j+1]=v+1; indices[j+2]=v+2;
        indices[j+3]=v+2; indices[j+4]=v+1; indices[j+5]=v+3;
    }
    glGenBuffers(1,&quad_indices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,quad_indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    if (glGetError()==GL_NO_ERROR) return true;
fail:
    close_renderer();
    return false;
}

static bool texture(OgGpuTexture *t, const void *data, int w, int h, int pitch, int format,
                    bool target, bool linear) {
    if (t->id && t->linear!=linear) {
        if (queued && key_texture==t->id) flush();
        bind_texture(t->id);
        filter(linear);
        t->linear=linear;
    }
    if (t->image) {
        t->used_frame=frame_serial;
        return true;
    }
    if (t->id && (!t->dirty || target)) return true;
    if (queued && key_texture==t->id && t->id) flush();
    GLenum fmt=format==1?GL_RGB:GL_RGBA;
    GLenum type=format==1?GL_UNSIGNED_SHORT_5_6_5:format==2?GL_UNSIGNED_SHORT_5_5_5_1:GL_UNSIGNED_BYTE;
    int bytes=format==3?4:2;
    if (!target && (!data || pitch!=w*bytes)) return false;
    bool new_texture=!t->id;
    SDL_Rect r=t->update;
    if (new_texture || r.w<=0 || r.h<=0) r=(SDL_Rect){0,0,w,h};
    void *packed=NULL;
    if (!target) {
        const uint8_t *start=(const uint8_t *)data+(size_t)r.y*pitch+r.x*bytes;
        if (format==2 || r.w*bytes!=pitch) {
            size_t row_bytes=(size_t)r.w*bytes;
            packed=malloc(row_bytes*r.h);
            if (!packed) return false;
            for (int y=0;y<r.h;y++) {
                const uint8_t *row=start+(size_t)y*pitch;
                if (format==2) {
                    uint16_t *out=(uint16_t *)((uint8_t *)packed+y*row_bytes);
                    const uint16_t *src=(const uint16_t *)row;
                    for (int x=0;x<r.w;x++) out[x]=(uint16_t)((src[x]<<1)|(src[x]>>15));
                } else memcpy((uint8_t *)packed+y*row_bytes,row,row_bytes);
            }
            data=packed;
        } else data=start;
    }
    if (new_texture) glGenTextures(1,&t->id);
    bind_texture(t->id);
    if (new_texture) {
        filter(linear);
        t->linear=linear;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    if (new_texture || target) glTexImage2D(GL_TEXTURE_2D,0,fmt,w,h,0,fmt,type,target?NULL:data);
    else glTexSubImage2D(GL_TEXTURE_2D,0,r.x,r.y,r.w,r.h,fmt,type,data);
    free(packed);
    if (target && !t->framebuffer) {
        GLint current=0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&current);
        glGenFramebuffers(1,&t->framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER,t->framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->id,0);
        GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER,current);
        if (status!=GL_FRAMEBUFFER_COMPLETE) return false;
    }
    t->dirty=false;
    t->update=(SDL_Rect){0};
    return glGetError()==GL_NO_ERROR;
}

static bool map_texture(OgGpuTexture *t,int w,int h) {
    if (t->image) {
        // fbdev swap completes rendering. Only draws since the last swap can still read this texture.
        if (t->used_frame==frame_serial) {
            flush();
            glFinish();
            t->used_frame=0;
        }
        return true;
    }
    if (t->id || t->dirty || t->shared.width || w>640 || h>480 ||
        !create_image || !destroy_image || !bind_image) return false;
    t->shared=(Xgb35StreamPixmap){.width=w,.height=h};
    EGLImageKHR image=create_image(display,EGL_NO_CONTEXT,EGL_NATIVE_PIXMAP_KHR,
                                   (EGLClientBuffer)&t->shared,NULL);
    if (image==EGL_NO_IMAGE_KHR) return false;
    if (!t->shared.pixels) {
        destroy_image(display,image);
        return false;
    }
    glGenTextures(1,&t->id);
    bind_texture(t->id);
    filter(false);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    bind_image(GL_TEXTURE_2D,image);
    if (glGetError()!=GL_NO_ERROR) {
        bound_texture=0;
        glDeleteTextures(1,&t->id);
        destroy_image(display,image);
        t->id=0;
        return false;
    }
    t->image=image;
    static bool reported;
    if (!reported) {
        fprintf(stderr,"OpenGorky: shared SGX streaming textures (%dx%d RGBA)\n",w,h);
        reported=true;
    }
    return true;
}

static void destroy_texture(OgGpuTexture *t) {
    if (!t->id) return;
    flush();
    if (t->framebuffer) glDeleteFramebuffers(1,&t->framebuffer);
    if (bound_texture==t->id) bound_texture=0;
    glDeleteTextures(1,&t->id);
    if (t->image) destroy_image(display,t->image);
    *t=(OgGpuTexture){0};
}

static void target(const OgGpuTexture *t, int w, int h) {
    flush();
    unclip();
    glBindFramebuffer(GL_FRAMEBUFFER,t?t->framebuffer:0);
    int vw=w, vh=h, vx=0, vy=0;
    if (!t) {
        vh=screen_h; vw=vh*w/h;
        if (vw>screen_w) { vw=screen_w; vh=vw*h/w; }
        vx=(screen_w-vw)/2; vy=(screen_h-vh)/2;
        if (vw!=screen_w || vh!=screen_h) {
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    viewport_x=vx; viewport_y=vy; viewport_width=vw; viewport_height=vh; logical_w=w; logical_h=h;
    glViewport(vx,vy,vw,vh);
    glUniform2f(scale,2.f/w,2.f/h);
}

static void clear(float r, float g, float b, float a) {
    flush(); unclip(); glClearColor(r,g,b,a); glClear(GL_COLOR_BUFFER_BIT);
}

static SDL_Vertex *reserve(unsigned tex, uint32_t blend, SDL_Rect clip) {
    if (clip.w<=0 || clip.h<=0) return NULL;
    if (queued==batch_quads || (queued && (tex!=key_texture || blend!=key_blend ||
        memcmp(&clip,&key_clip,sizeof(clip))))) flush();
    key_texture=tex; key_blend=blend; key_clip=clip;
    return quads+4*queued++;
}

static void quad(unsigned tex,uint32_t blend,SDL_Rect clip,const SDL_Vertex *input) {
    SDL_Vertex *v=reserve(tex,blend,clip);
    if (v) memcpy(v,input,4*sizeof(*v));
}

static void rect(unsigned tex,uint32_t blend,SDL_Rect clip,SDL_FRect p,SDL_FRect uv,SDL_FColor c) {
    SDL_Vertex *v=reserve(tex,blend,clip);
    if (!v) return;
    const float right=p.x+p.w, bottom=p.y+p.h, u=uv.x+uv.w, t=uv.y+uv.h;
    v[0]=(SDL_Vertex){{p.x,p.y},c,{uv.x,uv.y}};
    v[1]=(SDL_Vertex){{right,p.y},c,{u,uv.y}};
    v[2]=(SDL_Vertex){{p.x,bottom},c,{uv.x,t}};
    v[3]=(SDL_Vertex){{right,bottom},c,{u,t}};
}

static void geometry(unsigned tex,uint32_t blend,SDL_Rect clip,
                     const SDL_Vertex *v,int n,const int *ids,int count,bool lines,SDL_FRect rect,bool flip) {
    if (clip.w<=0 || clip.h<=0 || n<=0) return;
    flush(); state(tex,blend,clip); matrix(rect,flip); arrays(v);
    if (!ids) { glDrawArrays(lines?GL_LINE_STRIP:GL_TRIANGLES,0,n); return; }
    GLushort group[768];
    for (int i=0;i<count;) {
        int used=0;
        for (;i+2<count && used<768;i+=3) {
            if ((unsigned)ids[i]>=(unsigned)n || (unsigned)ids[i+1]>=(unsigned)n ||
                (unsigned)ids[i+2]>=(unsigned)n || ids[i]>65535 || ids[i+1]>65535 || ids[i+2]>65535) continue;
            group[used++]=ids[i]; group[used++]=ids[i+1]; group[used++]=ids[i+2];
        }
        if (used) glDrawElements(GL_TRIANGLES,used,GL_UNSIGNED_SHORT,group);
        if (i+2>=count) break;
    }
}

static uint64_t now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
}

static bool present(void) {
    static unsigned frames;
    bool sample=getenv("OG_SGX_PROFILE") && ++frames==10;
    uint64_t begin=sample?now():0;
    flush();
    uint64_t flushed=sample?now():0;
    bool ok=glGetError()==GL_NO_ERROR && eglSwapBuffers(display,surface);
    if (ok) frame_serial++;
    if (sample) fprintf(stderr,"SGX sprites flush %.3f ms swap %.3f ms window %dx%d\n",
                        (flushed-begin)/1e6,(now()-flushed)/1e6,screen_w,screen_h);
    return ok;
}

const OgGpuSprites *og_sgx_sprites_v3(void) {
    static const OgGpuSprites ops={open_renderer,close_renderer,texture,map_texture,destroy_texture,
                                  target,clear,rect,quad,geometry,present};
    return &ops;
}

bool og_sgx_direct_shadow(uint8_t *mask,int size,const float *points,int count) {
    if (shadow_failed || size<1 || size>480 || count<1 || count>INT32_MAX/6) return false;
    float x0=size,y0=size,x1=0,y1=0;
    for (int i=0;i<count*6;i+=2) {
        x0=fminf(x0,points[i]); x1=fmaxf(x1,points[i]);
        y0=fminf(y0,points[i+1]); y1=fmaxf(y1,points[i+1]);
    }
    int left=(int)fmaxf(0,x0), top=(int)fmaxf(0,y0);
    int right=(int)fminf(size,ceilf(x1)), bottom=(int)fminf(size,ceilf(y1));
    memset(mask,0,(size_t)size*size);
    if (left>=right || top>=bottom) return true;
    flush();
    GLint previous=0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);
    if (shadow_mask.shared.width!=size) destroy_texture(&shadow_mask);
    if (!shadow_mask.framebuffer) {
        if (!map_texture(&shadow_mask,size,size)) goto fail;
        glGenFramebuffers(1,&shadow_mask.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER,shadow_mask.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,shadow_mask.id,0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) goto fail;
    } else glBindFramebuffer(GL_FRAMEBUFFER,shadow_mask.framebuffer);
    unclip();
    glViewport(0,0,size,size);
    glUniform2f(scale,2.f/size,-2.f/size);
    matrix((SDL_FRect){0,-size,1,1},false);
    blend(0); bind_texture(white);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glVertexAttrib2f(1,0,0); glVertexAttrib4f(2,1,1,1,1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,points);
    glDrawArrays(GL_TRIANGLES,0,count*3);
    glFinish();
    glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    glViewport(viewport_x,viewport_y,viewport_width,viewport_height);
    glUniform2f(scale,2.f/logical_w,2.f/logical_h);
    if (glGetError()!=GL_NO_ERROR) goto fail;
    glBindFramebuffer(GL_FRAMEBUFFER,previous);
    const uint8_t *pixels=shadow_mask.shared.pixels;
    for (int y=top;y<bottom;y++) {
        const uint8_t *row=pixels+(size_t)y*shadow_mask.shared.pitch;
        for (int x=left;x<right;x++) mask[(size_t)y*size+x]=row[x*4]!=0;
    }
    return true;
fail:
    glBindFramebuffer(GL_FRAMEBUFFER,previous);
    destroy_texture(&shadow_mask);
    shadow_failed=true;
    return false;
}

/* Pack the winning fragment as RGB565 + depth so the CPU effects can continue
   against the same canvas. Both EGLImage and FBO live across frames. */
static struct {
    OgGpuTexture color;
    GLuint program, depth, background, depth_fbo, depth_program, depth_color;
    GLint dimensions, wrap;
    bool failed, resident, depth_texture;
} model;
static GLuint resident_program, resident_mask_program, resident_mask, resident_fbo;
enum { model_textures=32 };
static struct { const uint8_t *data; int w,h; GLuint id; } materials[model_textures];
static unsigned material_next;

static void destroy_models(void) {
    destroy_texture(&model.color);
    if (model.depth_texture) {
        if (bound_texture==model.depth) bound_texture=0;
        glDeleteTextures(1,&model.depth);
    } else glDeleteRenderbuffers(1,&model.depth);
    glDeleteTextures(1,&model.background);
    if (model.program) glDeleteProgram(model.program);
    if (model.depth_program) glDeleteProgram(model.depth_program);
    glDeleteFramebuffers(1,&model.depth_fbo);
    glDeleteRenderbuffers(1,&model.depth_color);
    glDeleteFramebuffers(1,&resident_fbo);
    if (bound_texture==resident_mask) bound_texture=0;
    glDeleteTextures(1,&resident_mask);
    if (resident_program) glDeleteProgram(resident_program);
    if (resident_mask_program) glDeleteProgram(resident_mask_program);
    resident_fbo=resident_mask=resident_program=resident_mask_program=0;
    memset(&model,0,sizeof(model));
    for (int i=0;i<model_textures;i++) {
        if (bound_texture==materials[i].id) bound_texture=0;
        glDeleteTextures(1,&materials[i].id);
    }
    memset(materials,0,sizeof(materials));
    material_next=0;
}

static bool model_target(int w,int h,bool resident) {
    if (model.color.framebuffer && model.color.shared.width==w &&
        model.color.shared.height==h && model.resident==resident) return true;
    destroy_models();
    GLint precision, range[2], linked;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,range,&precision);
    if (precision<16) return false;
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    model.resident=resident;
    model.depth_texture=resident && extensions && strstr(extensions,"GL_OES_depth_texture");
    if (resident && (!model.depth_texture || !strstr(extensions,"GL_OES_standard_derivatives"))) return false;
    if (resident) {
        model.color.shared.width=w; model.color.shared.height=h;
        glGenTextures(1,&model.color.id);
        bind_texture(model.color.id);
        filter(false);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    } else if (!map_texture(&model.color,w,h)) return false;
    model.program=glCreateProgram();
    if (!shader(model.program,GL_VERTEX_SHADER,
        "attribute vec3 p;attribute vec2 t;attribute vec3 c;uniform vec2 invSize;"
        "varying highp float z; varying mediump vec2 uv; varying mediump vec3 lit;"
        "void main(){uv=t;lit=c;z=p.z;gl_Position=vec4(p.xy*invSize*2.-1.,z*1.9999-.99995,1.);}") ||
        !shader(model.program,GL_FRAGMENT_SHADER,model.depth_texture?
        "#extension GL_OES_standard_derivatives : require\n"
        "precision highp float;uniform vec2 invSize;uniform sampler2D img,bg;"
        "uniform highp sampler2D actorZ;uniform bool wrapUV;"
        "varying highp float z; varying mediump vec2 uv; varying mediump vec3 lit;"
        "void main(){vec2 xy=gl_FragCoord.xy;vec2 coord=xy*invSize;"
        "float nearest=texture2D(actorZ,coord).r;"
        // A coarse sample can be half a full-size pixel away on either axis.
        "float slope=.5*(abs(dFdx(z))+abs(dFdy(z)));"
        "if(nearest<1. && (z-slope)*.99995+.000025>nearest+1./256.)discard;"
        "vec4 old=texture2D(bg,coord);"
        "if(z*65533.+1.25>=dot(old.ra,vec2(255.,65280.)))discard;"
        "gl_FragColor=vec4(texture2D(img,wrapUV?fract(uv):uv).rgb*lit,1.);}" :
        "precision highp float;uniform vec2 invSize;uniform sampler2D img,bg;uniform bool wrapUV;"
        "varying highp float z; varying mediump vec2 uv; varying mediump vec3 lit;"
        "void main(){vec4 old=texture2D(bg,gl_FragCoord.xy*invSize);"
        "float d=floor(z*65533.+1.5);if(d+.25>=dot(old.ra,vec2(255.,65280.)))discard;"
        "vec3 rgb=clamp(texture2D(img,wrapUV?fract(uv):uv).rgb*lit,0.,1.);"
        "vec3 q=floor(rgb*vec3(31.,63.,31.)+.5);float col=dot(q,vec3(2048.,32.,1.));"
        "gl_FragColor=vec4(mod(col,256.),floor(col/256.),mod(d,256.),floor(d/256.))/255.;}")) return false;
    glBindAttribLocation(model.program,0,"p");
    glBindAttribLocation(model.program,1,"t");
    glBindAttribLocation(model.program,2,"c");
    glLinkProgram(model.program);
    glGetProgramiv(model.program,GL_LINK_STATUS,&linked);
    if (!linked) return false;
    glUseProgram(model.program);
    model.dimensions=glGetUniformLocation(model.program,"invSize");
    model.wrap=glGetUniformLocation(model.program,"wrapUV");
    glUniform1i(glGetUniformLocation(model.program,"bg"),1);
    if (resident) glUniform1i(glGetUniformLocation(model.program,"actorZ"),3);
    glUniform2f(model.dimensions,1.f/w,1.f/h);
    glGenFramebuffers(1,&model.color.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,model.color.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,model.color.id,0);
    if (model.depth_texture) {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return false;
        model.depth_program=glCreateProgram();
        if (!shader(model.depth_program,GL_VERTEX_SHADER,
                    "attribute vec3 p;uniform vec2 invSize;"
                    "void main(){gl_Position=vec4(p.xy*invSize*2.-1.,p.z*1.9999-.99995,1.);}") ||
            !shader(model.depth_program,GL_FRAGMENT_SHADER,"void main(){gl_FragColor=vec4(0.);}")) return false;
        glBindAttribLocation(model.depth_program,0,"p");
        glLinkProgram(model.depth_program);
        glGetProgramiv(model.depth_program,GL_LINK_STATUS,&linked);
        if (!linked) return false;
        glUseProgram(model.depth_program);
        glUniform2f(glGetUniformLocation(model.depth_program,"invSize"),1.f/w,1.f/h);
        glGenFramebuffers(1,&model.depth_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER,model.depth_fbo);
        glGenTextures(1,&model.depth);
        bind_texture(model.depth);
        filter(false);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,w/2,h/2,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_SHORT,NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,model.depth,0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
            glGenRenderbuffers(1,&model.depth_color);
            glBindRenderbuffer(GL_RENDERBUFFER,model.depth_color);
            glRenderbufferStorage(GL_RENDERBUFFER,GL_RGB565,w/2,h/2);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,model.depth_color);
        }
    } else {
        glGenRenderbuffers(1,&model.depth);
        glBindRenderbuffer(GL_RENDERBUFFER,model.depth);
        glRenderbufferStorage(GL_RENDERBUFFER,extensions && strstr(extensions,"GL_OES_depth24")?
                             GL_DEPTH_COMPONENT24_OES:GL_DEPTH_COMPONENT16,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,model.depth);
    }
    glGenTextures(1,&model.background);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,model.background);
    filter(false);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE_ALPHA,resident?w/2:w,resident?h/2:h,0,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,NULL);
    glActiveTexture(GL_TEXTURE0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
}

static GLuint material_texture(const SceneTriangle *t) {
    if (!t->pixels) return white;
    for (int i=0;i<model_textures;i++)
        if (materials[i].data==t->pixels && materials[i].w==t->tw && materials[i].h==t->th) return materials[i].id;
    unsigned i=material_next++%model_textures;
    glDeleteTextures(1,&materials[i].id);
    if (materials[i].id==bound_texture) bound_texture=0;
    glGenTextures(1,&materials[i].id);
    bind_texture(materials[i].id);
    filter(true);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t->tw,t->th,0,GL_RGBA,GL_UNSIGNED_BYTE,t->pixels);
    materials[i].data=t->pixels; materials[i].w=t->tw; materials[i].h=t->th;
    return materials[i].id;
}

typedef struct { const SceneTriangle *tri; int index; } ModelOrder;

static int order_models(const void *left,const void *right) {
    const ModelOrder *a=left,*b=right;
    uintptr_t x=(uintptr_t)a->tri->pixels,y=(uintptr_t)b->tri->pixels;
    if (x!=y) return x<y?-1:1;
    if (a->tri->flags!=b->tri->flags) return a->tri->flags-b->tri->flags;
    return a->index-b->index;
}

static bool resident_setup(void) {
    if (resident_fbo) return true;
    resident_program=glCreateProgram();
    char fragment[2400];
    snprintf(fragment,sizeof(fragment),"%s%s",
        model.depth_texture?"#define NATIVE_DEPTH\n":"",
        "precision highp float;varying highp vec4 pixel;uniform sampler2D actors,bg,mask;uniform highp sampler2D zbuf;"
        "uniform vec2 invSize,invView,origin,depthRange;uniform float bias,intensity;"
        "uniform bool perspective;"
        "float unpack(vec2 b){return dot(floor(b*255.+.5),vec2(1.,256.));}"
        "void main(){vec2 uv=(pixel.xy-origin)*invSize;"
        "vec4 a=texture2D(actors,uv);float opacity=0.;"
        "\n#ifdef NATIVE_DEPTH\n"
        "bool actor=a.a>0.;"
        "\n#else\n"
        "bool actor=a.b+a.a>0.;"
        "\n#endif\n"
        "if(pixel.w>.5){float shade=texture2D(mask,(1.+pixel.xy*invView*254.)/256.).r;"
        "if(shade>0.){"
        "\n#ifdef NATIVE_DEPTH\n"
        "float d=actor?((texture2D(zbuf,uv).r-.000025)/.99995*65533.+1.):"
        "dot(texture2D(bg,uv).ra,vec2(255.,65280.));"
        "\n#else\n"
        "float d=dot(actor?a.ba:texture2D(bg,uv).ra,vec2(255.,65280.));"
        "\n#endif\n"
        "float depth=depthRange.x+(d-1.)*depthRange.y+bias;"
        "float p=pixel.z;bool lit=perspective?"
        "(abs(p)>=.00000001 && (p>0.?depth*p>=1.:depth*p<=1.)):depth>=p;"
        "if((!perspective || abs(p)>=.00000001) && (d>65534.5 || lit))opacity=shade*intensity;}}"
        "if(actor){"
        "\n#ifdef NATIVE_DEPTH\n"
        "vec3 rgb=a.rgb;"
        "\n#else\n"
        "float c=unpack(a.rg);"
        "vec3 rgb=vec3(floor(c/2048.)/31.,mod(floor(c/32.),64.)/63.,mod(c,32.)/31.);"
        "\n#endif\n"
        "gl_FragColor=vec4(rgb*(1.-opacity),1.);}"
        "else{if(opacity==0.)discard;gl_FragColor=vec4(0.,0.,0.,opacity);}}");
    if (!shader(resident_program,GL_VERTEX_SHADER,
        "attribute vec4 point;uniform vec2 invView;varying highp vec4 pixel;"
        "void main(){pixel=point;gl_Position=vec4(point.x*invView.x*2.-1.,1.-point.y*invView.y*2.,0.,1.);}") ||
        !shader(resident_program,GL_FRAGMENT_SHADER,fragment)) return false;
    glBindAttribLocation(resident_program,0,"point");
    glLinkProgram(resident_program);
    GLint linked;
    glGetProgramiv(resident_program,GL_LINK_STATUS,&linked);
    if (!linked) return false;
    resident_mask_program=glCreateProgram();
    if (!shader(resident_mask_program,GL_VERTEX_SHADER,
        "attribute vec2 point;void main(){gl_Position=vec4(point/128.-1.,0.,1.);}") ||
        !shader(resident_mask_program,GL_FRAGMENT_SHADER,
        "precision lowp float;void main(){gl_FragColor=vec4(1.);}")) return false;
    glBindAttribLocation(resident_mask_program,0,"point");
    glLinkProgram(resident_mask_program);
    glGetProgramiv(resident_mask_program,GL_LINK_STATUS,&linked);
    if (!linked) return false;
    glGenTextures(1,&resident_mask);
    bind_texture(resident_mask);
    filter(true);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,512,512,0,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,NULL);
    glGenFramebuffers(1,&resident_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,resident_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,resident_mask,0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
}

typedef struct { float x,y,plane,shadow; } CompositeVertex;

static void composite_rect(CompositeVertex *v,int x0,int y0,int x1,int y1) {
    const CompositeVertex a={x0,y0,0,0}, b={x1,y0,0,0}, c={x0,y1,0,0}, d={x1,y1,0,0};
    v[0]=a; v[1]=b; v[2]=c; v[3]=c; v[4]=b; v[5]=d;
}

static int composite_mesh(CompositeVertex *v,const SceneResident *s,int left,int top,int right,int bottom) {
    int n=0;
    for (int i=0;i<s->tile_count;i++) {
        const SceneShadowVertex p=s->tiles[i];
        v[n++]=(CompositeVertex){p.x,p.y,p.plane,1};
    }
    // Fill only the gaps around the non-overlapping, row-major shadow tiles.
    for (int y=top;y<bottom;) {
        int to=bottom, x=left;
        for (int i=0;i<s->tile_count;i+=6) {
            const int start=s->tiles[i].y, end=s->tiles[i+2].y;
            if (start>y && start<to) to=start;
            if (end>y && end<to) to=end;
        }
        for (int i=0;i<s->tile_count;i+=6) {
            const SceneShadowVertex *p=s->tiles+i;
            if (y<p[0].y || y>=p[2].y) continue;
            if (x<p[0].x) { composite_rect(v+n,x,y,p[0].x,to); n+=6; }
            x=p[1].x;
        }
        if (x<right) { composite_rect(v+n,x,y,right,to); n+=6; }
        y=to;
    }
    return n;
}

static void resident_present(GLuint frame,const SceneResident *s,int left,int top,int right,int bottom,
                             float lo,float step,bool breakdown) {
    uint64_t started=breakdown?now():0;
    if (s->tile_count) {
        glBindFramebuffer(GL_FRAMEBUFFER,resident_fbo);
        glUseProgram(resident_mask_program);
        glViewport(0,0,512,512);
        glClearColor(0,0,0,0);
        glClear(GL_COLOR_BUFFER_BIT);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,s->mask);
        glDrawArrays(GL_TRIANGLES,0,s->mask_count*3);
    }
    if (breakdown) glFinish();
    uint64_t masked=breakdown?now():0;
    glBindFramebuffer(GL_FRAMEBUFFER,frame);
    glViewport(viewport_x,viewport_y,viewport_width,viewport_height);
    glUseProgram(resident_program);
    glUniform2f(glGetUniformLocation(resident_program,"invView"),1.f/logical_w,1.f/logical_h);
    glUniform2f(glGetUniformLocation(resident_program,"invSize"),
                1.f/model.color.shared.width,1.f/model.color.shared.height);
    glUniform2f(glGetUniformLocation(resident_program,"origin"),left,top);
    glUniform2f(glGetUniformLocation(resident_program,"depthRange"),lo,step);
    glUniform1f(glGetUniformLocation(resident_program,"bias"),s->bias);
    glUniform1f(glGetUniformLocation(resident_program,"intensity"),s->intensity);
    glUniform1i(glGetUniformLocation(resident_program,"perspective"),s->perspective);
    glUniform1i(glGetUniformLocation(resident_program,"bg"),1);
    glUniform1i(glGetUniformLocation(resident_program,"mask"),2);
    if (model.depth_texture) {
        glUniform1i(glGetUniformLocation(resident_program,"zbuf"),3);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,model.depth);
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D,resident_mask);
    glActiveTexture(GL_TEXTURE0);
    bind_texture(model.color.id);
    // Eight tile rows plus the clipped top/bottom of the mask and the actor-only gaps.
    CompositeVertex mesh[6*(64+11*9)];
    const int count=composite_mesh(mesh,s,left,top,right,bottom);
    blend(SDL_BLENDMODE_BLEND);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,0,mesh);
    glDrawArrays(GL_TRIANGLES,0,count);
    if (breakdown) {
        glFinish();
        fprintf(stderr,"SGX synchronized shadow mask %.3f ms, composition + queued screen work %.3f ms\n",
                (masked-started)/1e6,(now()-masked)/1e6);
    }
    glUseProgram(program);
}

bool og_sgx_direct_models(const SceneTriangle *tris,const float *z,int n,int w,int h,
                          uint8_t *rgba,float *depth,uint8_t *glow,const SceneResident *resident) {
    if (model.failed || n<1 || n>8192 || w<1 || w>640 || h<1 || h>480) return false;
    static unsigned sample_count;
    const bool timed=getenv("OG_SGX_PROFILE")!=NULL;
    const bool profile=timed && ++sample_count==10;
    const uint64_t started=timed?now():0;
    const bool skip_bg=resident && getenv("OG_DEBUG_SKIP_BG_Z");
    float lo=z[0],hi=z[0];
    int left=w,top=h,right=0,bottom=0;
    for (int i=0;i<n;i++) {
        const SceneTriangle *t=tris+i;
        if (t->x0<0 || t->y0<0 || t->x1>w || t->y1>h ||
            t->x0>=t->x1 || t->y0>=t->y1) return false;
        if (t->x0<left) left=t->x0;
        if (t->y0<top) top=t->y0;
        if (t->x1>right) right=t->x1;
        if (t->y1>bottom) bottom=t->y1;
        for (int j=0;j<3;j++) {
            float d=z[i*3+j];
            if (!isfinite(d)) return false;
            lo=fminf(lo,d); hi=fmaxf(hi,d);
        }
    }
    if (resident) for (int i=0;i<resident->tile_count;i++) {
        const SceneShadowVertex p=resident->tiles[i];
        const float d=(resident->perspective?1.f/p.plane:p.plane)-resident->bias;
        if (!isfinite(d)) return false;
        lo=fminf(lo,d); hi=fmaxf(hi,d);
        if ((int)p.x<left) left=(int)p.x;
        if ((int)p.y<top) top=(int)p.y;
        if ((int)p.x>right) right=(int)p.x;
        if ((int)p.y>bottom) bottom=(int)p.y;
    }
    if (left>=right || top>=bottom) return false;
    const int step=resident?2:1;
    if (resident) {
        if ((w|h)&1) return false;
        // Anchor each 2x2 depth sample to the screen as the actor bounds move.
        left &= ~1; top &= ~1;
        right=(right+1)&~1; bottom=(bottom+1)&~1;
    }
    const int rw=right-left,rh=bottom-top;
    const int zw=rw/step,zh=rh/step;
    int target_w=(rw+127)&~127,target_h=(rh+127)&~127;
    if (target_h>480) target_h=480;
    if (model.resident==(resident!=NULL)) {
        if (target_w<model.color.shared.width) target_w=model.color.shared.width;
        if (target_h<model.color.shared.height) target_h=model.color.shared.height;
    }
    const float span=fmaxf(hi-lo,1e-6f), inverse=1.f/span;
    struct Vertex { float x,y,z,u,v,r,g,b; } *v=malloc((size_t)n*3*sizeof(*v));
    uint16_t *packed=skip_bg?NULL:calloc((size_t)zw*zh,sizeof(*packed));
    ModelOrder *order=malloc((size_t)n*sizeof(*order));
    if (!v || (!skip_bg && !packed) || !order) { free(v); free(packed); free(order); return false; }
    enum { tile_w=32, tile_h=16 };
    uint32_t tiles[480/tile_h]={0};
    for (int i=0;i<n;i++) {
        const SceneTriangle *t=tris+i;
        order[i]=(ModelOrder){t,i};
        int a=(t->x0-left)/tile_w, b=(t->x1-1-left)/tile_w;
        uint32_t occupied=((2u<<b)-1) & ~((1u<<a)-1);
        for (int y=(t->y0-top)/tile_h;y<=(t->y1-1-top)/tile_h;y++) tiles[y]|=occupied;
    }
    if (resident) for (int i=0;i<resident->tile_count;i+=6) {
        const SceneShadowVertex *p=resident->tiles+i;
        int a=((int)p[0].x-left)/tile_w, b=((int)p[1].x-1-left)/tile_w;
        uint32_t occupied=((2u<<b)-1) & ~((1u<<a)-1);
        for (int y=((int)p[0].y-top)/tile_h;y<=((int)p[2].y-1-top)/tile_h;y++) tiles[y]|=occupied;
    }
    qsort(order,n,sizeof(*order),order_models);
    for (int i=0;i<n;i++) for (int j=0;j<3;j++) {
        const SceneVertex p=order[i].tri->v[j];
        v[i*3+j]=(struct Vertex){p.x-left,p.y-top,
            (z[order[i].index*3+j]-lo)*inverse,p.u,p.v,p.r,p.g,p.b};
    }
    const uint64_t sorted=timed?now():0;
    for (int y=0;!skip_bg && y<zh;y++) {
        const float *src=depth+(step*y+top)*w+left;
        uint16_t *dst=packed+y*zw;
        for (uint32_t active=tiles[step*y/tile_h];active;active&=active-1) {
            const int cols=tile_w/step;
            int x=__builtin_ctz(active)*cols;
            const int end=x+cols<zw?x+cols:zw;
#if defined(__ARM_NEON)
            const float32x4_t min_depth=vdupq_n_f32(lo), max_depth=vdupq_n_f32(hi);
            const float32x4_t factor=vdupq_n_f32(inverse*65533.f), round_up=vdupq_n_f32(1.5f);
            for (;x+4<=end;x+=4) {
                float32x4_t d;
                if (resident) {
                    const float *p=src+2*x;
                    const float32x4_t a=vminq_f32(vld1q_f32(p),vld1q_f32(p+w));
                    const float32x4_t b=vminq_f32(vld1q_f32(p+4),vld1q_f32(p+w+4));
                    d=vcombine_f32(vpmin_f32(vget_low_f32(a),vget_high_f32(a)),
                                   vpmin_f32(vget_low_f32(b),vget_high_f32(b)));
                } else d=vld1q_f32(src+x);
                const float32x4_t scaled=vmlaq_f32(round_up,vsubq_f32(d,min_depth),factor);
                uint32x4_t q=vcvtq_u32_f32(scaled);
                q=vbslq_u32(vcleq_f32(d,min_depth),vdupq_n_u32(1),q);
                q=vbslq_u32(vcleq_f32(d,max_depth),q,vdupq_n_u32(65535));
                vst1_u16(dst+x,vmovn_u32(q));
            }
#endif
            for (;x<end;x++) {
                const float *p=src+step*x;
                const float d=resident?fminf(fminf(p[0],p[1]),fminf(p[w],p[w+1])):p[0];
                dst[x]=!(d<=hi)?65535:d<=lo?1:(unsigned)((d-lo)*inverse*65533+1.5f);
            }
        }
    }
    const uint64_t packed_at=timed?now():0;
    flush();
    const uint64_t prepared=timed?now():0;
    GLint previous=0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);
    bool ok=false;
    if (!model_target(target_w,target_h,resident!=NULL) || (resident && !resident_setup())) goto restore_models;
    glBindFramebuffer(GL_FRAMEBUFFER,model.color.framebuffer);
    glUseProgram(model.program);
    unclip(); blend(0);
    glViewport(0,0,target_w,target_h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,skip_bg?white:model.background);
    const uint64_t setup=timed?now():0;
    if (!skip_bg) glTexSubImage2D(GL_TEXTURE_2D,0,0,0,zw,zh,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,packed);
    const uint64_t uploaded=timed?now():0;
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(*v),&v->x);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(*v),&v->u);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(*v),&v->r);
    if (resident) {
        glBindFramebuffer(GL_FRAMEBUFFER,model.depth_fbo);
        glViewport(0,0,target_w/2,target_h/2);
        glUseProgram(model.depth_program);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES,0,3*n);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER,model.color.framebuffer);
        glViewport(0,0,target_w,target_h);
        glUseProgram(model.program);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,model.depth);
        glActiveTexture(GL_TEXTURE0);
    }
    glClearColor(0,0,0,0);
    glClear(resident?GL_COLOR_BUFFER_BIT:GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    int batches=0;
    for (int i=0;i<n;) {
        const SceneTriangle *t=order[i].tri;
        int end=i+1;
        while (end<n && order[end].tri->pixels==t->pixels && order[end].tri->tw==t->tw &&
               order[end].tri->th==t->th && order[end].tri->flags==t->flags) end++;
        bind_texture(material_texture(t));
        glUniform1i(model.wrap,t->flags&1);
        glDrawArrays(GL_TRIANGLES,3*i,3*(end-i));
        i=end;
        batches++;
    }
    const uint64_t submitted=timed?now():0;
    if (resident) {
        ok=glGetError()==GL_NO_ERROR;
        if (!ok) goto restore_models;
        const bool breakdown=profile && getenv("OG_SGX_GPU_BREAKDOWN");
        if (breakdown) {
            glFinish();
            fprintf(stderr,"SGX synchronized model pass + queued background %.3f ms; hardware depth texture %s\n",
                    (now()-submitted)/1e6,
                    model.depth_texture?"active":"unavailable");
        }
        glDisable(GL_DEPTH_TEST);
        resident_present(previous,resident,left,top,right,bottom,lo,span/65533.f,breakdown);
        free(v); free(packed); free(order);
        if (profile) fprintf(stderr,"SGX actor color %dx%d, hardware Z %dx%d, background Z upload %dx%d for %dx%d screen pixels\n",
                             target_w,target_h,target_w/2,target_h/2,skip_bg?0:zw,skip_bg?0:zh,rw,rh);
        if (timed && !breakdown) {
            static uint64_t sums[7];
            static unsigned frames;
            const uint64_t marks[]={started,sorted,packed_at,prepared,setup,uploaded,submitted,now()};
            for (unsigned i=0;i<7;i++) sums[i]+=marks[i+1]-marks[i];
            if (++frames==120) {
                fprintf(stderr,"SGX mean 120: geometry %.2f pack Z %.2f sprite flush %.2f FBO setup %.2f upload Z %.2f model submit %.2f compose %.2f ms\n",
                    sums[0]/120e6,sums[1]/120e6,sums[2]/120e6,sums[3]/120e6,sums[4]/120e6,sums[5]/120e6,sums[6]/120e6);
                memset(sums,0,sizeof(sums));
                frames=0;
            }
        }
        if (profile) fprintf(stderr,"SGX resident models draws %d, preparation %.3f, background upload and setup %.3f, model submit %.3f, composition submit %.3f ms\n",
            batches,(prepared-started)/1e6,(uploaded-prepared)/1e6,
            (submitted-uploaded)/1e6,(now()-submitted)/1e6);
        return true;
    }
    glFinish();
    ok=glGetError()==GL_NO_ERROR;
    if (profile) fprintf(stderr,"SGX models target %dx%d, draws %d, preparation %.3f, submit %.3f, wait %.3f ms\n",
        target_w,target_h,batches,(prepared-started)/1e6,(submitted-prepared)/1e6,(now()-submitted)/1e6);
restore_models:
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,previous);
    glUseProgram(program);
    glViewport(viewport_x,viewport_y,viewport_width,viewport_height);
    free(v); free(packed); free(order);
    if (!ok) {
        destroy_models();
        model.failed=true;
        return false;
    }
    const uint64_t resolve=profile?now():0;
    const uint32_t *pixels=model.color.shared.pixels;
    for (int y=0;y<rh;y++) {
        const uint32_t *row=pixels+(size_t)y*(model.color.shared.pitch/4);
        unsigned base=(unsigned)((y+top)*w+left);
        for (uint32_t active=tiles[y/tile_h];active;active&=active-1) {
            int x=__builtin_ctz(active)*tile_w;
            const int end=x+tile_w<rw?x+tile_w:rw;
            for (;x<end;x++) {
                const uint32_t fragment=row[x];
                const unsigned q=fragment>>16;
                if (!q) continue;
                const unsigned rgb=fragment&65535u, i=base+x;
                uint8_t *dst=rgba+4*i;
                dst[0]=(rgb>>11)*255/31; dst[1]=((rgb>>5)&63)*255/63;
                dst[2]=(rgb&31)*255/31; dst[3]=255;
                depth[i]=lo+(q-1)*(span/65533.f);
                if (glow) memset(glow+4*i,0,4);
            }
        }
    }
    if (profile) fprintf(stderr,"SGX models resolve %.3f ms\n",(now()-resolve)/1e6);
    return true;
}
