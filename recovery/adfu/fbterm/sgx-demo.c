/* sgx-demo: Live X11 window rendering 15 rotating 3D lit cubes on PowerVR SGX540 (GLES2 pbuffer -> X11 RGB565). */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

enum { W = 480, H = 320 };

typedef int32_t EGLint;
typedef unsigned int EGLBoolean, EGLenum;
typedef void *EGLDisplay, *EGLConfig, *EGLSurface, *EGLContext;
typedef unsigned int GLenum, GLuint, GLsizei;
typedef int GLint, GLsizeiptr;
typedef float GLfloat;
typedef unsigned char GLubyte;

#define EGL_DEFAULT_DISPLAY ((void *)0)
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_DEPTH_TEST 0x0B71
#define GL_DITHER 0x0BD0
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_TRIANGLES 0x0004
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RENDERER 0x1F01

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static const char *VS =
    "attribute vec3 pos, normal; attribute vec2 uv; uniform vec3 offset, paint;"
    "uniform vec2 rot; varying mediump vec3 color; varying mediump vec2 tex;"
    "vec3 turn(vec3 v){vec2 xz=mat2(rot.x,rot.y,-rot.y,rot.x)*v.xz;"
    "return vec3(xz.x,0.94*v.y-0.342*xz.y,0.342*v.y+0.94*xz.y);}"
    "void main(){vec3 p=turn(pos)*0.54+offset; float d=7.0-p.z;"
    "gl_Position=vec4(1.70*p.x,2.55*p.y,d-1.6,d);"
    "color=paint*(0.30+0.70*max(dot(turn(normal),vec3(-0.36,0.48,0.80)),0.0)); tex=uv;}";

static const char *FS =
    "precision mediump float; varying vec3 color; varying vec2 tex;"
    "void main(){vec2 tile=floor(tex*4.0); float k=mod(tile.x+tile.y,2.0);"
    "gl_FragColor=vec4(color*(0.75+0.25*k),1.0);}";

int main(void) {
    void *hx = dlopen("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL);
    void *he = dlopen("libEGL.so", RTLD_LAZY | RTLD_GLOBAL);
    void *hg = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!hx || !he || !hg) {
        fprintf(stderr, "sgx-demo: dlopen failed (X11=%p EGL=%p GLES2=%p) - run via sgx-run!\n", hx, he, hg);
        return 1;
    }

    #define SYM(h, r, n, args) r (*p_##n) args = (r (*) args)dlsym(h, #n)
    SYM(hx, Display*, XOpenDisplay, (const char*));
    SYM(hx, int, XCloseDisplay, (Display*));
    SYM(hx, Window, XCreateSimpleWindow, (Display*,Window,int,int,unsigned,unsigned,unsigned,unsigned long,unsigned long));
    SYM(hx, int, XStoreName, (Display*,Window,const char*));
    SYM(hx, Atom, XInternAtom, (Display*,const char*,Bool));
    SYM(hx, Status, XSetWMProtocols, (Display*,Window,Atom*,int));
    SYM(hx, int, XSelectInput, (Display*,Window,long));
    SYM(hx, int, XMapWindow, (Display*,Window));
    SYM(hx, GC, XCreateGC, (Display*,Drawable,unsigned long,void*));
    SYM(hx, XImage*, XCreateImage, (Display*,Visual*,unsigned,int,int,char*,unsigned,unsigned,int,int));
    SYM(hx, int, XPutImage, (Display*,Drawable,GC,XImage*,int,int,int,int,unsigned,unsigned));
    SYM(hx, int, XFlush, (Display*));
    SYM(hx, int, XPending, (Display*));
    SYM(hx, int, XNextEvent, (Display*,XEvent*));
    SYM(hx, KeySym, XLookupKeysym, (XKeyEvent*,int));

    SYM(he, EGLDisplay, eglGetDisplay, (void*));
    SYM(he, EGLBoolean, eglInitialize, (EGLDisplay,EGLint*,EGLint*));
    SYM(he, EGLBoolean, eglBindAPI, (EGLenum));
    SYM(he, EGLBoolean, eglChooseConfig, (EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*));
    SYM(he, EGLSurface, eglCreatePbufferSurface, (EGLDisplay,EGLConfig,const EGLint*));
    SYM(he, EGLContext, eglCreateContext, (EGLDisplay,EGLConfig,EGLContext,const EGLint*));
    SYM(he, EGLBoolean, eglMakeCurrent, (EGLDisplay,EGLSurface,EGLSurface,EGLContext));
    SYM(he, EGLBoolean, eglTerminate, (EGLDisplay));

    SYM(hg, GLuint, glCreateShader, (GLenum));
    SYM(hg, void, glShaderSource, (GLuint,GLsizei,const char*const*,const GLint*));
    SYM(hg, void, glCompileShader, (GLuint));
    SYM(hg, GLuint, glCreateProgram, (void));
    SYM(hg, void, glAttachShader, (GLuint,GLuint));
    SYM(hg, void, glBindAttribLocation, (GLuint,GLuint,const char*));
    SYM(hg, void, glLinkProgram, (GLuint));
    SYM(hg, void, glUseProgram, (GLuint));
    SYM(hg, GLint, glGetUniformLocation, (GLuint,const char*));
    SYM(hg, void, glGenBuffers, (GLsizei,GLuint*));
    SYM(hg, void, glBindBuffer, (GLenum,GLuint));
    SYM(hg, void, glBufferData, (GLenum,GLsizeiptr,const void*,GLenum));
    SYM(hg, void, glVertexAttribPointer, (GLuint,GLint,GLenum,unsigned char,GLsizei,const void*));
    SYM(hg, void, glEnableVertexAttribArray, (GLuint));
    SYM(hg, void, glEnable, (GLenum));
    SYM(hg, void, glDisable, (GLenum));
    SYM(hg, void, glViewport, (GLint,GLint,GLsizei,GLsizei));
    SYM(hg, void, glClearColor, (GLfloat,GLfloat,GLfloat,GLfloat));
    SYM(hg, void, glClear, (unsigned int));
    SYM(hg, void, glUniform2f, (GLint,GLfloat,GLfloat));
    SYM(hg, void, glUniform3f, (GLint,GLfloat,GLfloat,GLfloat));
    SYM(hg, void, glUniform3fv, (GLint,GLsizei,const GLfloat*));
    SYM(hg, void, glDrawArrays, (GLenum,GLint,GLsizei));
    SYM(hg, void, glReadPixels, (GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*));
    SYM(hg, const GLubyte*, glGetString, (GLenum));

    Display *xdpy = p_XOpenDisplay(NULL);
    if (!xdpy) { fprintf(stderr, "sgx-demo: cannot open X11 display\n"); return 1; }
    int scr = DefaultScreen(xdpy);
    Window win = p_XCreateSimpleWindow(xdpy, RootWindow(xdpy, scr), 120, 80, W, H, 0, 0, 0x091018);
    Atom wm_del = p_XInternAtom(xdpy, "WM_DELETE_WINDOW", False);
    p_XSetWMProtocols(xdpy, win, &wm_del, 1);
    p_XSelectInput(xdpy, win, KeyPressMask | ExposureMask);
    p_XStoreName(xdpy, win, "PowerVR SGX540 3D Demo");
    p_XMapWindow(xdpy, win);
    GC gc = p_XCreateGC(xdpy, win, 0, NULL);

    EGLDisplay edpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!p_eglInitialize(edpy, NULL, NULL) || !p_eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "sgx-demo: eglInitialize failed\n"); return 2;
    }
    const EGLint attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE
    };
    EGLConfig cfg; EGLint ncfg = 0;
    p_eglChooseConfig(edpy, attr, &cfg, 1, &ncfg);
    const EGLint pba[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    const EGLint ctxa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLSurface surf = p_eglCreatePbufferSurface(edpy, cfg, pba);
    EGLContext ctx = p_eglCreateContext(edpy, cfg, NULL, ctxa);
    p_eglMakeCurrent(edpy, surf, surf, ctx);

    const char *rend = (const char *)p_glGetString(GL_RENDERER);
    if (!rend) rend = "GLES2";

    GLuint vs = p_glCreateShader(GL_VERTEX_SHADER), fs = p_glCreateShader(GL_FRAGMENT_SHADER);
    p_glShaderSource(vs, 1, &VS, NULL); p_glCompileShader(vs);
    p_glShaderSource(fs, 1, &FS, NULL); p_glCompileShader(fs);
    GLuint prog = p_glCreateProgram();
    p_glAttachShader(prog, vs); p_glAttachShader(prog, fs);
    p_glBindAttribLocation(prog, 0, "pos");
    p_glBindAttribLocation(prog, 1, "normal");
    p_glBindAttribLocation(prog, 2, "uv");
    p_glLinkProgram(prog); p_glUseProgram(prog);
    GLint u_off = p_glGetUniformLocation(prog, "offset");
    GLint u_rot = p_glGetUniformLocation(prog, "rot");
    GLint u_col = p_glGetUniformLocation(prog, "paint");

    static const int faces[6][4] = {{4,5,7,6},{1,0,2,3},{5,1,3,7},{0,4,6,2},{6,7,3,2},{0,1,5,4}};
    static const int norms[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    static const int corns[6] = {0,1,2,0,2,3};
    GLfloat verts[36][8];
    for (int f = 0; f < 6; f++) for (int i = 0; i < 6; i++) {
        int c = corns[i], pt = faces[f][c];
        GLfloat *v = verts[6*f + i];
        for (int a = 0; a < 3; a++) { v[a] = (pt & (1<<a)) ? 1.f : -1.f; v[3+a] = (GLfloat)norms[f][a]; }
        v[6] = (GLfloat)(c == 1 || c == 2); v[7] = (GLfloat)(c >= 2);
    }
    GLuint vbo; p_glGenBuffers(1, &vbo); p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(verts[0]), (void*)0);
    p_glVertexAttribPointer(1, 3, GL_FLOAT, 0, sizeof(verts[0]), (void*)(3*sizeof(GLfloat)));
    p_glVertexAttribPointer(2, 2, GL_FLOAT, 0, sizeof(verts[0]), (void*)(6*sizeof(GLfloat)));
    for (int i = 0; i < 3; i++) p_glEnableVertexAttribArray(i);
    p_glEnable(GL_DEPTH_TEST); p_glDisable(GL_DITHER);
    p_glViewport(0, 0, W, H);
    p_glClearColor(0.035f, 0.055f, 0.09f, 1.f);

    uint8_t *rgba = malloc(W * H * 4);
    int depth = DefaultDepth(xdpy, scr);
    int bpp = (depth <= 16) ? 2 : 4;
    uint8_t *xbuf = malloc(W * H * bpp);
    XImage *img = p_XCreateImage(xdpy, DefaultVisual(xdpy, scr), depth, ZPixmap, 0, (char*)xbuf, W, H, bpp*8, W*bpp);

    static const GLfloat paints[][3] = {{0.12f,0.85f,0.90f},{1.f,0.53f,0.12f},{0.32f,0.58f,1.f}};
    double t0 = now_s(), fps_t = t0;
    int fps_n = 0, running = 1;

    while (running) {
        while (p_XPending(xdpy)) {
            XEvent ev; p_XNextEvent(xdpy, &ev);
            if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_del) running = 0;
            else if (ev.type == KeyPress) {
                KeySym ks = p_XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_Escape || ks == XK_q || ks == XK_Q) running = 0;
            }
        }
        double t = now_s() - t0;
        p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (int y = 0; y < 3; y++) for (int x = 0; x < 5; x++) {
            float a = (float)(t * 1.6) + 0.43f*x + 0.67f*y;
            p_glUniform2f(u_rot, cosf(a), sinf(a));
            p_glUniform3f(u_off, (x-2)*1.48f, (y-1)*1.48f, (y-1)*0.25f);
            p_glUniform3fv(u_col, 1, paints[(x+y)%3]);
            p_glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        p_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        if (bpp == 2) {
            uint16_t *dst = (uint16_t *)xbuf;
            for (int y = 0; y < H; y++) {
                const uint8_t *src = rgba + (H - 1 - y) * W * 4;
                uint16_t *drow = dst + y * W;
                for (int x = 0; x < W; x++) {
                    drow[x] = (uint16_t)(((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | (src[2] >> 3));
                    src += 4;
                }
            }
        } else {
            uint32_t *dst = (uint32_t *)xbuf;
            for (int y = 0; y < H; y++) {
                const uint8_t *src = rgba + (H - 1 - y) * W * 4;
                uint32_t *drow = dst + y * W;
                for (int x = 0; x < W; x++) {
                    drow[x] = ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | src[2];
                    src += 4;
                }
            }
        }
        p_XPutImage(xdpy, win, gc, img, 0, 0, 0, 0, W, H);
        p_XFlush(xdpy);

        fps_n++;
        double tn = now_s();
        if (tn - fps_t >= 1.0) {
            char title[128];
            snprintf(title, sizeof(title), "%s (360 MHz) - %.1f FPS [ESC to quit]", rend, fps_n / (tn - fps_t));
            p_XStoreName(xdpy, win, title);
            fps_n = 0; fps_t = tn;
        }
    }
    p_eglTerminate(edpy);
    p_XCloseDisplay(xdpy);
    return 0;
}
