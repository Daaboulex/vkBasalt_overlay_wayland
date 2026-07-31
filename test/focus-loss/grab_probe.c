#include <X11/Xlib.h>
#include <stdio.h>

/* Connects once and answers a line of stdin at a time, because opening a new
   connection to a server busy rendering starves for longer than a test can wait. */
int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        printf("no-display\n");
        fflush(stdout);
        return 2;
    }

    Window root = DefaultRootWindow(display);
    char line[64];

    printf("ready\n");
    fflush(stdout);

    while (fgets(line, sizeof line, stdin) != NULL)
    {
        int result = XGrabPointer(display, root, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

        if (result == GrabSuccess)
        {
            XUngrabPointer(display, CurrentTime);
            printf("free\n");
        }
        else
        {
            printf("%s\n", result == AlreadyGrabbed ? "held" : "unavailable");
        }

        fflush(stdout);
    }

    XCloseDisplay(display);
    return 0;
}
