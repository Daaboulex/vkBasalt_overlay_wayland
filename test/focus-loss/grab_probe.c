#include <X11/Xlib.h>
#include <stdio.h>

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        printf("no-display\n");
        return 2;
    }

    Window root = DefaultRootWindow(display);
    int result = XGrabPointer(display, root, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    if (result == GrabSuccess)
    {
        XUngrabPointer(display, CurrentTime);
        XCloseDisplay(display);
        printf("free\n");
        return 0;
    }

    XCloseDisplay(display);
    printf("%s\n", result == AlreadyGrabbed ? "held" : "unavailable");
    return 1;
}
