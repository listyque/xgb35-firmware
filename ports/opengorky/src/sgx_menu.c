#include "sgx_menu.h"
#include "sgx_scene.h"
#include "menu_pixels.h"
#include <math.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EGLDisplay display;
static EGLContext context;
static EGLSurface surface;
static GLuint program;
static bool read565, read_bgra, profiling;
static uint64_t stamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000 + ts.tv_nsec;
}
static uint8_t *readback;
static GLushort *elements;
static int capacity, width, height;

void og_sgx_close(void) {
    og_sgx_scene_close(!display);
    if (display && context && eglGetCurrentContext() == context)
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface) eglDestroySurface(display, surface);
    if (context) eglDestroyContext(display, context);
    if (display) eglTerminate(display);
    display = EGL_NO_DISPLAY; surface = EGL_NO_SURFACE; context = EGL_NO_CONTEXT;
    free(readback); readback = NULL;
    free(elements); elements = NULL; capacity = 0;
}

static bool shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    GLint ok;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (ok) glAttachShader(program, s);
    glDeleteShader(s);
    return ok;
}

static bool open_surface(int w, int h) {
    const EGLint attr[] = { EGL_SURFACE_TYPE,EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE,5, EGL_GREEN_SIZE,6, EGL_BLUE_SIZE,5, EGL_NONE };
    const EGLint size[] = { EGL_WIDTH,8, EGL_HEIGHT,8, EGL_NONE };
    const EGLint ver[] = { EGL_CONTEXT_CLIENT_VERSION,2, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!display || !eglInitialize(display, NULL, NULL) || !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(display, attr, &cfg, 1, &n) || !n) return false;
    surface = eglCreatePbufferSurface(display, cfg, size);
    context = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ver);
    if (!surface || !context || !eglMakeCurrent(display, surface, surface, context)) return false;
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    if (!renderer || !strstr(renderer,"PowerVR SGX 540")) return false;
    program = glCreateProgram();
    if (!shader(GL_VERTEX_SHADER, "attribute vec2 p,t; attribute vec4 c; uniform vec2 scale;"
                "varying mediump vec2 uv; varying lowp vec4 col; void main(){ uv=t; col=c;"
                "gl_Position=vec4(p*scale-1.0,0.0,1.0); }") ||
        !shader(GL_FRAGMENT_SHADER, "precision mediump float; uniform sampler2D img; varying mediump vec2 uv;"
                "varying lowp vec4 col; void main(){gl_FragColor=texture2D(img,uv)*col;}")) return false;
    glBindAttribLocation(program,0,"p"); glBindAttribLocation(program,1,"t"); glBindAttribLocation(program,2,"c");
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    glUseProgram(program);
    GLint dimensions = glGetUniformLocation(program, "scale");
    glUniform2f(dimensions, 2.f/w, 2.f/h);
    glViewport(0,0,w,h);
    glDisable(GL_DITHER);
    GLuint fbo, color;
    glGenFramebuffers(1,&fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&color);
    glBindRenderbuffer(GL_RENDERBUFFER,color);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGB565,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    GLint format, type;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &format);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &type);
    read565 = format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5;
    read_bgra = format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE;
    profiling = getenv("OG_MENU_BENCH") != NULL;
    if (profiling) fprintf(stderr,"OpenGorky SGX read format=0x%x type=0x%x\n",format,type);
    glClearColor(1,1,1,1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR,GL_ZERO);
    glEnableVertexAttribArray(2);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    readback = malloc((size_t)w*h*(read565 ? 2 : 4));
    width = w; height = h;
    return readback && glGetError() == GL_NO_ERROR;
}

void og_sgx_forget(unsigned id) {
    glDeleteTextures(1, &id);
}

static void upload(const void *pixels, int w, int h, int rgb565, int linear) {
    GLenum fmt = rgb565 ? GL_RGB : GL_RGBA;
    GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
    glTexImage2D(GL_TEXTURE_2D,0,fmt,w,h,0,fmt,rgb565 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE,pixels);
}

bool og_sgx_draw(uint16_t *target, int w, int h, const OgSgxMesh *mesh, int count) {
    if (!display && !open_surface(w,h)) return false;
    if (w != width || h != height) return false;
    float left=w, top=h, right=0, bottom=0;
    uint64_t start=profiling ? stamp() : 0;
    glClear(GL_COLOR_BUFFER_BIT);
    for (int i = 0; i < count; ++i) {
        const OgSgxMesh *m = &mesh[i];
        const SDL_Vertex *v = m->verts;
        if (m->count > capacity) {
            GLushort *b = realloc(elements,(size_t)m->count*sizeof(*elements));
            if (!b) return false;
            capacity = m->count; elements = b;
        }
        for (int j=0; j<m->count; j++) {
            int k=m->indices[j];
            if (k<0 || k>=m->vertices || k>65535) return false;
            elements[j]=(GLushort)k;
        }
        for (int j=0; j<m->vertices; j++) {
            float x=v[j].position.x, y=v[j].position.y;
            if (!isfinite(x) || !isfinite(y)) return false;
            if (x<left) left=x;
            if (x>right) right=x;
            if (y<top) top=y;
            if (y>bottom) bottom=y;
        }
        if (!*m->texture_id) {
            glGenTextures(1,m->texture_id);
            glBindTexture(GL_TEXTURE_2D,*m->texture_id);
            upload(m->pixels,m->width,m->height,m->rgb565,m->linear);
        } else {
            glBindTexture(GL_TEXTURE_2D,*m->texture_id);
        }
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(*v),&v->position);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(*v),&v->tex_coord);
        glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(*v),&v->color);
        glDrawElements(GL_TRIANGLES,m->count,GL_UNSIGNED_SHORT,elements);
    }
    if (left>=w || right<=0 || top>=h || bottom<=0) return glGetError()==GL_NO_ERROR;
    int x0=left>0 ? (int)left : 0, y0=top>0 ? (int)top : 0;
    int x1=right<w ? (int)right+1 : w, y1=bottom<h ? (int)bottom+1 : h;
    int rw=x1-x0, rh=y1-y0;
    uint64_t submitted=profiling ? stamp() : 0;
    glReadPixels(x0,y0,rw,rh,read565 ? GL_RGB : read_bgra ? GL_BGRA_EXT : GL_RGBA,
                 read565 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE,readback);
    if (glGetError() != GL_NO_ERROR) return false;
    uint64_t received=profiling ? stamp() : 0;
    for (int y=0; y<rh; y++) {
        uint16_t *row=target+(y0+y)*w+x0;
        int x=0;
#if defined(__ARM_NEON)
        if (read565) {
            const uint16_t *mask=(const uint16_t *)readback+y*rw;
            for (; x+8<=rw; x+=8) {
                uint16x8_t m=vld1q_u16(mask+x);
                uint16x8_t dst=vld1q_u16(row+x);
                vst1q_u16(row+x,multiply_channels565x8(dst,red565x8(m),green565x8(m),blue565x8(m)));
            }
        }
#endif
        for (; x<rw; x++) {
            int i=y*rw+x;
            uint16_t mask;
            if (read565) {
                mask=((uint16_t *)readback)[i];
            } else {
                const uint8_t *p=readback+4*i;
                mask=(uint16_t)(((p[read_bgra ? 2 : 0]&248)<<8)|((p[1]&252)<<3)|(p[read_bgra ? 0 : 2]>>3));
            }
            if (mask != 0xffff) row[x]=multiply565(row[x],mask);
        }
    }
    if (profiling) {
        static unsigned frames;
        if (++frames == 12)
            fprintf(stderr,"OpenGorky SGX region=%dx%d submit=%.2f read=%.2f composite=%.2f ms\n",
                    rw,rh,(submitted-start)/1e6,(received-submitted)/1e6,(stamp()-received)/1e6);
    }
    return true;
}
