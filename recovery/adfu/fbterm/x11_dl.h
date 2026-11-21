/* Runtime X11 + XTest loader via musl dlopen (avoids cross-stub DSO ABI issues). */
#ifndef _XGB_X11_DL_H_
#define _XGB_X11_DL_H_

#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

/* Exact musl arch/arm/crt_arch.h _start so ld.lld links purely dynamically against libc.so (zero static libc.a) */
__asm__(
".text\n"
".globl _start\n"
".type _start,%function\n"
"_start:\n"
"    mov fp, #0\n"
"    mov lr, #0\n"
"    ldr r1, [sp]\n"
"    add r2, sp, #4\n"
"    mov r3, #0\n"
"    bic sp, sp, #15\n"
"    push {r3}\n"
"    push {r3}\n"
"    movw r0, #:lower16:main\n"
"    movt r0, #:upper16:main\n"
"    b __libc_start_main\n"
);

static Display *(*p_XOpenDisplay)(const char *);
static int (*p_XCloseDisplay)(Display *);
static int (*p_XFlush)(Display *);
static KeyCode (*p_XKeysymToKeycode)(Display *, KeySym);
static Window (*p_XCreateSimpleWindow)(Display *, Window, int, int, unsigned int, unsigned int, unsigned int, unsigned long, unsigned long);
static void (*p_XSetWMNormalHints)(Display *, Window, XSizeHints *);
static int (*p_XStoreName)(Display *, Window, const char *);
static Atom (*p_XInternAtom)(Display *, const char *, Bool);
static Status (*p_XSetWMProtocols)(Display *, Window, Atom *, int);
static int (*p_XSelectInput)(Display *, Window, long);
static int (*p_XMapWindow)(Display *, Window);
static GC (*p_XCreateGC)(Display *, Drawable, unsigned long, void *);
static int (*p_XFreeGC)(Display *, GC);
static XImage *(*p_XCreateImage)(Display *, Visual *, unsigned int, int, int, char *, unsigned int, unsigned int, int, int);
static int (*p_XPutImage)(Display *, Drawable, GC, XImage *, int, int, int, int, unsigned int, unsigned int);
static int (*p_XPending)(Display *);
static int (*p_XNextEvent)(Display *, XEvent *);
static int (*p_XDestroyWindow)(Display *, Window);
static int (*p_XTestFakeKeyEvent)(Display *, unsigned int, Bool, unsigned long);
static int (*p_XTestFakeButtonEvent)(Display *, unsigned int, Bool, unsigned long);
static int (*p_XTestFakeRelativeMotionEvent)(Display *, int, int, unsigned long);
static int (*p_XTestGrabControl)(Display *, Bool);

static inline int xgb_load_x11(int need_xtst)
{
    void *hx = dlopen("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL);
    if (!hx) hx = dlopen("/usr/lib/libX11.so.6", RTLD_LAZY | RTLD_GLOBAL);
    if (!hx) return 0;
    p_XOpenDisplay        = (Display *(*)(const char *))dlsym(hx, "XOpenDisplay");
    p_XCloseDisplay       = (int (*)(Display *))dlsym(hx, "XCloseDisplay");
    p_XFlush              = (int (*)(Display *))dlsym(hx, "XFlush");
    p_XKeysymToKeycode    = (KeyCode (*)(Display *, KeySym))dlsym(hx, "XKeysymToKeycode");
    p_XCreateSimpleWindow = (Window (*)(Display *, Window, int, int, unsigned int, unsigned int, unsigned int, unsigned long, unsigned long))dlsym(hx, "XCreateSimpleWindow");
    p_XSetWMNormalHints   = (void (*)(Display *, Window, XSizeHints *))dlsym(hx, "XSetWMNormalHints");
    p_XStoreName          = (int (*)(Display *, Window, const char *))dlsym(hx, "XStoreName");
    p_XInternAtom         = (Atom (*)(Display *, const char *, Bool))dlsym(hx, "XInternAtom");
    p_XSetWMProtocols     = (Status (*)(Display *, Window, Atom *, int))dlsym(hx, "XSetWMProtocols");
    p_XSelectInput        = (int (*)(Display *, Window, long))dlsym(hx, "XSelectInput");
    p_XMapWindow          = (int (*)(Display *, Window))dlsym(hx, "XMapWindow");
    p_XCreateGC           = (GC (*)(Display *, Drawable, unsigned long, void *))dlsym(hx, "XCreateGC");
    p_XFreeGC             = (int (*)(Display *, GC))dlsym(hx, "XFreeGC");
    p_XCreateImage        = (XImage *(*)(Display *, Visual *, unsigned int, int, int, char *, unsigned int, unsigned int, int, int))dlsym(hx, "XCreateImage");
    p_XPutImage           = (int (*)(Display *, Drawable, GC, XImage *, int, int, int, int, unsigned int, unsigned int))dlsym(hx, "XPutImage");
    p_XPending            = (int (*)(Display *))dlsym(hx, "XPending");
    p_XNextEvent          = (int (*)(Display *, XEvent *))dlsym(hx, "XNextEvent");
    p_XDestroyWindow      = (int (*)(Display *, Window))dlsym(hx, "XDestroyWindow");
    if (!p_XOpenDisplay || !p_XFlush) return 0;

    if (need_xtst) {
        void *ht = dlopen("libXtst.so.6", RTLD_LAZY);
        if (!ht) ht = dlopen("/usr/lib/libXtst.so.6", RTLD_LAZY);
        if (!ht) return 0;
        p_XTestFakeKeyEvent            = (int (*)(Display *, unsigned int, Bool, unsigned long))dlsym(ht, "XTestFakeKeyEvent");
        p_XTestFakeButtonEvent         = (int (*)(Display *, unsigned int, Bool, unsigned long))dlsym(ht, "XTestFakeButtonEvent");
        p_XTestFakeRelativeMotionEvent = (int (*)(Display *, int, int, unsigned long))dlsym(ht, "XTestFakeRelativeMotionEvent");
        p_XTestGrabControl             = (int (*)(Display *, Bool))dlsym(ht, "XTestGrabControl");
        if (!p_XTestFakeKeyEvent || !p_XTestFakeButtonEvent || !p_XTestFakeRelativeMotionEvent) return 0;
    }
    return 1;
}

#define XOpenDisplay        p_XOpenDisplay
#define XCloseDisplay       p_XCloseDisplay
#define XFlush              p_XFlush
#define XKeysymToKeycode    p_XKeysymToKeycode
#define XCreateSimpleWindow p_XCreateSimpleWindow
#define XSetWMNormalHints   p_XSetWMNormalHints
#define XStoreName          p_XStoreName
#define XInternAtom         p_XInternAtom
#define XSetWMProtocols     p_XSetWMProtocols
#define XSelectInput        p_XSelectInput
#define XMapWindow          p_XMapWindow
#define XCreateGC           p_XCreateGC
#define XFreeGC             p_XFreeGC
#define XCreateImage        p_XCreateImage
#define XPutImage           p_XPutImage
#define XPending            p_XPending
#define XNextEvent          p_XNextEvent
#define XDestroyWindow      p_XDestroyWindow
#define XTestFakeKeyEvent            p_XTestFakeKeyEvent
#define XTestFakeButtonEvent         p_XTestFakeButtonEvent
#define XTestFakeRelativeMotionEvent p_XTestFakeRelativeMotionEvent
#define XTestGrabControl             p_XTestGrabControl

#endif
