#include <X11/Xlib.h>

int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;
    int s = DefaultScreen(d);
    XSetWindowAttributes attrs = {
        .override_redirect = True,
        .background_pixel = BlackPixel(d, s),
    };
    Window cover = XCreateWindow(d, RootWindow(d, s), 0, 0,
        DisplayWidth(d, s), DisplayHeight(d, s), 0, CopyFromParent,
        InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel, &attrs);
    XMapRaised(d, cover);
    XSync(d, False);
    XDestroyWindow(d, cover);
    XSync(d, False);
    XCloseDisplay(d);
    return 0;
}
