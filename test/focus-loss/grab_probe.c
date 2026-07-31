#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>

/* Connects once and answers a line of stdin at a time, because opening a new
   connection to a server busy rendering starves for longer than a test can wait. */
static void answer_grab(Display *display, Window root)
{
    int result = XGrabPointer(display, root, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    if (result == GrabSuccess)
    {
        XUngrabPointer(display, CurrentTime);
        printf("free\n");
        return;
    }

    printf("%s\n", result == AlreadyGrabbed ? "held" : "unavailable");
}

/* Reports the key state the layer itself reads, so a key the test cannot deliver
   is told apart from a key the layer fails to notice. */
static void answer_key(Display *display)
{
    char keys[32];
    KeyCode code = XKeysymToKeycode(display, XK_Home);

    if (code == 0)
    {
        printf("no-keycode\n");
        return;
    }

    XQueryKeymap(display, keys);
    printf("%s\n", (keys[code >> 3] & (1 << (code & 7))) ? "down" : "up");
}

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
        if (strncmp(line, "key", 3) == 0)
            answer_key(display);
        else
            answer_grab(display, root);

        fflush(stdout);
    }

    XCloseDisplay(display);
    return 0;
}
