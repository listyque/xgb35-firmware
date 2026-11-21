#include "sgx_scene.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <dlfcn.h>

/* Bound the scratch surface on the 212 MiB console. The engine submits clipped triangles. */
enum { atlas_w=1024, atlas_h=512, max_tri=256 };
static EGLDisplay display;
static EGLContext context;
static EGLSurface surface;
static GLuint program, white, shadow_fbo, shadow_color;
static int shadow_size;
static GLint dimensions, material;
static bool bgra, failed;
static uint8_t *readback;
enum { texture_slots=48 };
static struct { const uint8_t *pixels; int w,h; GLuint id; } textures[texture_slots];
static int cached;
static bool new_batch;
void og_sgx_scene_begin(void) { new_batch=true; }

typedef struct { EGLDisplay d; EGLSurface read, draw; EGLContext ctx; } Binding;
static Binding binding(void) {
    return (Binding){eglGetCurrentDisplay(),eglGetCurrentSurface(EGL_READ),eglGetCurrentSurface(EGL_DRAW),eglGetCurrentContext()};
}
static void restore(Binding b) {
    if (display && eglGetCurrentContext()!=b.ctx)
        eglMakeCurrent(b.d ? b.d : display,b.draw,b.read,b.ctx);
}
void og_sgx_scene_close(bool last) {
    if (display && context && eglGetCurrentContext()==context)
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if (context) eglDestroyContext(display,context);
    if (surface) eglDestroySurface(display,surface);
    if (last && display) eglTerminate(display);
    context=EGL_NO_CONTEXT; surface=EGL_NO_SURFACE; display=EGL_NO_DISPLAY;
    shadow_fbo=shadow_color=0; shadow_size=0;
    free(readback); readback=NULL; failed=false; cached=0;
}
static bool attach(GLenum type,const char *src) {
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,1,&src,NULL); glCompileShader(shader);
    GLint ok=0;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if (ok) glAttachShader(program,shader);
    else { char log[256]={0}; glGetShaderInfoLog(shader,sizeof(log),NULL,log); fprintf(stderr,"SGX scene shader: %s\n",log); }
    glDeleteShader(shader);
    return ok;
}
static GLuint texture(const void *pixels,int w,int h) {
    GLuint id;
    glGenTextures(1,&id); glBindTexture(GL_TEXTURE_2D,id);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    return id;
}
static bool activate(void) {
    if (failed) return false;
    if (context && readback) return eglMakeCurrent(display,surface,surface,context);
    failed=true;
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    const EGLint attr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    const EGLint size[]={EGL_WIDTH,atlas_w,EGL_HEIGHT,atlas_h,EGL_NONE};
    const EGLint ver[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLConfig cfg; EGLint n;
    if (!display || !eglInitialize(display,NULL,NULL) || !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(display,attr,&cfg,1,&n) || !n) return false;
    surface=eglCreatePbufferSurface(display,cfg,size);
    context=eglCreateContext(display,cfg,EGL_NO_CONTEXT,ver);
    if (!surface || !context || !eglMakeCurrent(display,surface,surface,context)) return false;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    if (!renderer || !strstr(renderer,"PowerVR SGX 540")) return false;
    program=glCreateProgram();
    if (!attach(GL_VERTEX_SHADER,"attribute vec2 p,t;attribute vec3 c;attribute float z;uniform vec2 dim;"
        "varying mediump vec2 uv;varying mediump vec3 lit;void main(){uv=t;lit=c;gl_Position=vec4(p*dim-1.,z,1.);}") ||
        !attach(GL_FRAGMENT_SHADER,"precision mediump float;uniform sampler2D img;uniform vec4 m;"
        "varying mediump vec2 uv;varying mediump vec3 lit;void main(){vec2 t=uv;"
        "if(m.z>1.5)t.x+=sin(t.y*25.+m.w)*.03;if(m.x>.5)t=fract(t);vec4 s=texture2D(img,t);"
        "if(m.z>.5 && m.z<1.5)s.rgb=mix(s.rgb,vec3(dot(s.rgb,vec3(.299,.587,.114))),m.w);"
        "gl_FragColor=vec4(s.rgb*lit,m.y>.5?s.a:1.);}")) return false;
    glBindAttribLocation(program,0,"p"); glBindAttribLocation(program,1,"t"); glBindAttribLocation(program,2,"c");
    glBindAttribLocation(program,3,"z");
    glLinkProgram(program);
    GLint ok=0, format=0; glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if (!ok) return false;
    glUseProgram(program); dimensions=glGetUniformLocation(program,"dim"); material=glGetUniformLocation(program,"m");
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT,&format); bgra=format==GL_BGRA_EXT;
    glDisable(GL_DITHER); glClearColor(0,0,0,0); glEnableVertexAttribArray(0);
    const uint8_t pixel[]={255,255,255,255}; white=texture(pixel,1,1);
    readback=malloc((size_t)atlas_w*atlas_h*4);
    if (!readback || glGetError()!=GL_NO_ERROR) return false;
    failed=false; return true;
}

typedef struct { const SceneTriangle *tri; int index; } MaterialEntry;

static int compare_material(const SceneTriangle *a,const SceneTriangle *b) {
    uintptr_t ap=(uintptr_t)a->pixels,bp=(uintptr_t)b->pixels;
    if (ap!=bp) return ap<bp?-1:1;
    if (a->tw!=b->tw) return a->tw<b->tw?-1:1;
    if (a->th!=b->th) return a->th<b->th?-1:1;
    if (a->flags!=b->flags) return a->flags<b->flags?-1:1;
    if ((a->flags&12) && a->k!=b->k) return a->k<b->k?-1:1;
    return 0;
}

static int compare_entry(const void *left,const void *right) {
    const MaterialEntry *a=left,*b=right;
    int material_order=compare_material(a->tri,b->tri);
    return material_order?material_order:a->index-b->index;
}

static bool touches_edge(const SceneTriangle *t,int w,int h) {
    return t->x0==0 || t->y0==0 || t->x1==w || t->y1==h;
}

int og_sgx_scene_page(const SceneTriangle *tris,int count,int w,int h,SceneSlot *slots,
                      const uint8_t **out,int *stride,bool *out_bgra) {
    // shortcut: the engine supplies up to max_tri at once; a full chunk would
    // require synchronously reading more atlas pages. Revisit with direct GPU presentation.
    if (count>=max_tri) return 0;
    unsigned covered=0;
    for (int i=0;i<count;i++)
        covered+=(unsigned)(tris[i].x1-tris[i].x0)*(unsigned)(tris[i].y1-tris[i].y0);
    if (covered>(unsigned)w*h/2) return 0;
    static SceneVertex verts[max_tri*3];
    MaterialEntry order[max_tri];
    GLushort indices[max_tri*3];
    int x=0,y=0,shelf=0,right=0,n=0;
    for (;n<count && n<max_tri;n++) {
        const SceneTriangle *t=tris+n;
        int tw=t->x1-t->x0,th=t->y1-t->y0;
        if (tw<=0 || th<=0 || t->x0<0 || t->y0<0 || t->x1>w || t->y1>h || tw>atlas_w) break;
        if (x+tw>atlas_w) { x=0; y+=shelf; shelf=0; }
        if (y+th>atlas_h) break;
        slots[n]=(SceneSlot){x,y};
        for (int j=0;j<3;j++) {
            SceneVertex *v=verts+3*n+j;
            *v=t->v[j]; v->x+=x-t->x0; v->y+=y-t->y0;
        }
        x+=tw; if (right<x) right=x; if (shelf<th) shelf=th;
    }
    if (!n) return 0;
    // Slots stay in input order for the CPU depth and blend resolve.
    for (int i=0;i<n;i++) order[i]=(MaterialEntry){tris+i,i};
    qsort(order,n,sizeof(*order),compare_entry);
    for (int i=0;i<n;i++) for (int j=0;j<3;j++) indices[3*i+j]=3*order[i].index+j;

    int bottom=0;
    for (int i=0;i<n;i++) { int end=slots[i].y+tris[i].y1-tris[i].y0; if (bottom<end) bottom=end; }
    Binding prev=binding();
    if (!activate()) { restore(prev); return 0; }
    glViewport(0,0,atlas_w,atlas_h); glUniform2f(dimensions,2.f/atlas_w,2.f/atlas_h);
    glDisable(GL_SCISSOR_TEST); glClear(GL_COLOR_BUFFER_BIT);
    glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(*verts),&verts[0].x);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(*verts),&verts[0].u);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(*verts),&verts[0].r);
    if (new_batch) {
        new_batch=false;
        for (int i=0;i<cached;i++) glDeleteTextures(1,&textures[i].id);
        cached=0;
    }
    for (int i=0;i<n;) {
        const SceneTriangle *t=order[i].tri;
        GLuint id=white;
        if (t->pixels) {
            int j=0;
            while (j<cached && (textures[j].pixels!=t->pixels || textures[j].w!=t->tw || textures[j].h!=t->th)) j++;
            if (j==cached) {
                if (cached==texture_slots) { j=texture_slots-1; glDeleteTextures(1,&textures[j].id); }
                else cached++;
                textures[j].pixels=t->pixels; textures[j].w=t->tw; textures[j].h=t->th;
                textures[j].id=texture(t->pixels,t->tw,t->th);
            }
            id=textures[j].id;
        }
        glBindTexture(GL_TEXTURE_2D,id);
        glUniform4f(material,t->flags&1,(t->flags>>1)&1,(t->flags&4)?2:(t->flags&8)?1:0,t->k);
        int end=i+1;
        if (touches_edge(t,w,h)) {
            SceneSlot slot=slots[order[i].index];
            glEnable(GL_SCISSOR_TEST); glScissor(slot.x,slot.y,t->x1-t->x0,t->y1-t->y0);
            glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,indices+3*i);
            glDisable(GL_SCISSOR_TEST);
        } else {
            while (end<n && !touches_edge(order[end].tri,w,h) && !compare_material(t,order[end].tri)) end++;
            glDrawElements(GL_TRIANGLES,3*(end-i),GL_UNSIGNED_SHORT,indices+3*i);
        }
        i=end;
    }
    glReadPixels(0,0,right,bottom,bgra?GL_BGRA_EXT:GL_RGBA,GL_UNSIGNED_BYTE,readback);
    bool ok=glGetError()==GL_NO_ERROR;
    restore(prev);
    if (!ok) return 0;
    *out=readback; *stride=right*4; *out_bgra=bgra;
    return n;
}

bool og_sgx_scene_shadow(uint8_t *mask,int size,const float *tris,int count) {
    if (size<=0 || size>atlas_w || size>atlas_h || count<=0 || count>INT32_MAX/3) return false;
    Binding prev=binding();
    if (!activate()) { restore(prev); return false; }
    if (!shadow_fbo) {
        glGenFramebuffers(1,&shadow_fbo);
        glGenRenderbuffers(1,&shadow_color);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,shadow_fbo);
    if (shadow_size!=size) {
        glBindRenderbuffer(GL_RENDERBUFFER,shadow_color);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_RGB565,size,size);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,shadow_color);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER,0); restore(prev); return false;
        }
        shadow_size=size;
    }
    float left=size,top=size,right=0,bottom=0;
    for (int i=0;i<count*3;i++) {
        float x=tris[i*2],y=tris[i*2+1];
        if (x<left) left=x; if (x>right) right=x;
        if (y<top) top=y; if (y>bottom) bottom=y;
    }
    int x0=(int)fmaxf(0,fminf(size,left)),y0=(int)fmaxf(0,fminf(size,top));
    int x1=(int)fmaxf(0,fminf(size,right+1)),y1=(int)fmaxf(0,fminf(size,bottom+1));
    int w=x1-x0,h=y1-y0;
    if (w<=0 || h<=0) {
        glBindFramebuffer(GL_FRAMEBUFFER,0); restore(prev);
        memset(mask,0,(size_t)size*size); return true;
    }
    glViewport(0,0,size,size); glUniform2f(dimensions,2.f/size,2.f/size);
    glDisable(GL_SCISSOR_TEST); glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D,white); glUniform4f(material,0,0,0,0);
    glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glVertexAttrib2f(1,0,0); glVertexAttrib3f(2,1,1,1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,tris); glDrawArrays(GL_TRIANGLES,0,count*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(x0,y0,w,h,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,readback);
    glPixelStorei(GL_PACK_ALIGNMENT,4);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    bool ok=glGetError()==GL_NO_ERROR;
    restore(prev);
    if (ok) {
        memset(mask,0,(size_t)size*size);
        const uint16_t *src=(const uint16_t *)readback;
        for (int y=0;y<h;y++)
            for (int x=0;x<w;x++) mask[(y+y0)*size+x+x0]=src[y*w+x]!=0;
    }
    return ok;
}

static uint64_t probe_time(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
}

void og_sgx_probe_models(const SceneTriangle *tris,const float *z,int n,int w,int h) {
    if (n<1 || n>8192 || w<1 || h<1) return;
    struct Point { SceneVertex vertex; float depth; };
    struct Draw { int start,count; GLuint tex; const SceneTriangle *material; };
    struct Point *v=malloc((size_t)n*3*sizeof(*v));
    struct Draw *draw=malloc((size_t)n*sizeof(*draw));
    GLuint *ids=calloc((size_t)n,sizeof(*ids));
    MaterialEntry *order=malloc((size_t)n*sizeof(*order));
    if (!v || !draw || !ids || !order) { free(v); free(draw); free(ids); free(order); return; }
    for (int i=0;i<n;i++) order[i]=(MaterialEntry){tris+i,i};
    if (getenv("OG_MODEL_SORT")) qsort(order,n,sizeof(*order),compare_entry);
    Binding prev=binding();
    GLuint fbo=0,colors=0,depth=0,vbo=0;
    EGLImageKHR image=EGL_NO_IMAGE_KHR;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image=NULL;
    int batches=0;
    if (!activate()) goto out;
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&colors); glBindRenderbuffer(GL_RENDERBUFFER,colors);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGB565,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,colors);
    glGenRenderbuffers(1,&depth); glBindRenderbuffer(GL_RENDERBUFFER,depth);
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    bool depth24=extensions && strstr(extensions,"GL_OES_depth24");
    glRenderbufferStorage(GL_RENDERBUFFER,depth24?GL_DEPTH_COMPONENT24_OES:GL_DEPTH_COMPONENT16,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) goto release;
    PFNEGLCREATEIMAGEKHRPROC create_image=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    destroy_image=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (create_image && destroy_image)
        image=create_image(display,context,EGL_GL_RENDERBUFFER_KHR,(EGLClientBuffer)(uintptr_t)colors,NULL);
    fprintf(stderr,"model-probe synchronous-image=%d\n",image!=EGL_NO_IMAGE_KHR);
    float zmin=z[0],zmax=z[0];
    for (int i=0;i<n*3;i++) {
        if (!isfinite(z[i])) goto release;
        zmin=fminf(zmin,z[i]); zmax=fmaxf(zmax,z[i]);
    }
    float scale=1.9f/fmaxf(1.f,zmax-zmin);
    for (int i=0;i<n;i++) {
        const SceneTriangle *t=order[i].tri;
        int original=order[i].index;
        for (int j=0;j<3;j++) v[3*i+j]=(struct Point){t->v[j],(z[3*original+j]-zmin)*scale-.95f};
        int k=0;
        GLuint tex=white;
        if (t->pixels) {
            while (k<i && (order[k].tri->pixels!=t->pixels || order[k].tri->tw!=t->tw || order[k].tri->th!=t->th)) k++;
            if (k==i) ids[i]=texture(t->pixels,t->tw,t->th);
            tex=ids[k];
        }
        if (batches && draw[batches-1].tex==tex && !compare_material(draw[batches-1].material,t))
            draw[batches-1].count+=3;
        else draw[batches++]=(struct Draw){i*3,3,tex,t};
    }
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,(size_t)n*3*sizeof(*v),v,GL_STATIC_DRAW);
    glViewport(0,0,w,h); glUniform2f(dimensions,2.f/w,2.f/h); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    for (int i=0;i<4;i++) glEnableVertexAttribArray(i);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(*v),(void *)0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(*v),(void *)(2*sizeof(float)));
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(*v),(void *)(4*sizeof(float)));
    glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,sizeof(*v),(void *)sizeof(SceneVertex));
    void *hw=dlopen("model-hwperf.so",RTLD_NOW|RTLD_LOCAL);
    int (*hw_begin)(void)=hw?(int (*)(void))dlsym(hw,"model_hw_begin"):NULL;
    void (*hw_end)(void)=hw?(void (*)(void))dlsym(hw,"model_hw_end"):NULL;
    void (*hw_sample)(int)=hw?(void (*)(int))dlsym(hw,"model_hw_sample"):NULL;
    bool hw_active=false;
    for (int sample=-3;sample<12;sample++) {
        if (sample==0 && hw_begin && hw_end) hw_active=hw_begin()!=0;
        uint64_t start=probe_time();
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        for (int i=0;i<batches;i++) {
            const SceneTriangle *t=draw[i].material;
            glBindTexture(GL_TEXTURE_2D,draw[i].tex);
            glUniform4f(material,t->flags&1,0,(t->flags&4)?2:(t->flags&8)?1:0,t->k);
            glDrawArrays(GL_TRIANGLES,draw[i].start,draw[i].count);
        }
        uint64_t sent=probe_time();
        glFinish();
        uint64_t end=probe_time();
        GLenum err=glGetError();
        if (err!=GL_NO_ERROR) { fprintf(stderr,"model-probe GL error 0x%x\n",err); break; }
        if (sample==0) fprintf(stderr,"model-probe triangles=%d draws=%d depth=%d size=%dx%d\n",n,batches,depth24?24:16,w,h);
        if (sample>=0) fprintf(stderr,"model-probe submit=%.3f finish=%.3f total=%.3f ms\n",
            (sent-start)/1e6,(end-sent)/1e6,(end-start)/1e6);
        if (sample>=0 && hw_active && hw_sample) hw_sample(sample);
    }
    if (hw_active) hw_end();
    if (hw) dlclose(hw);
    const char *path=getenv("OG_MODEL_IMAGE");
    if (path && w<=atlas_w && h<=atlas_h) {
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,readback);
        glPixelStorei(GL_PACK_ALIGNMENT,4);
        FILE *image_file=glGetError()==GL_NO_ERROR?fopen(path,"wb"):NULL;
        if (image_file) {
            fprintf(image_file,"P6\n%d %d\n255\n",w,h);
            size_t colored=0;
            for (int i=0;i<w*h;i++) {
                uint16_t pixel=((const uint16_t *)readback)[i];
                const unsigned char rgb[]={ (pixel>>11)*255/31, ((pixel>>5)&63)*255/63, (pixel&31)*255/31 };
                colored+=pixel!=0;
                fwrite(rgb,1,3,image_file);
            }
            fclose(image_file);
            fprintf(stderr,"model-probe visible=%zu\n",colored);
        }
    }
release:
    if (image!=EGL_NO_IMAGE_KHR) destroy_image(display,image);
    glDisable(GL_DEPTH_TEST); glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER,0); glDeleteBuffers(1,&vbo);
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&fbo);
    glDeleteRenderbuffers(1,&colors); glDeleteRenderbuffers(1,&depth);
    glDeleteTextures(n,ids);
out:
    restore(prev);
    free(v); free(draw); free(ids); free(order);
}
