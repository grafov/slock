/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <giblib/gib_imlib.h>

#include <X11/X.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

// eclipse (4.2.1) doesn't recognize the following cpp flags from config.mk
#ifndef COLOR1
#define COLOR1 "black"
#endif

#ifndef COLOR2
#define COLOR2 "#005577"
#endif

#ifndef COLOR3
#define COLOR3 "#ff0000"
#endif

#ifndef VERSION
#define VERSION "1.0-tip"
#endif

typedef struct
{
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[3];
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;
static Bool spy_mode = False;

static void die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

#ifndef HAVE_BSD_AUTH
static const char * getpw(void)
{ /* only run as root */
	const char *rval;
	struct passwd *pw;

	pw = getpwuid(getuid());
	if (!pw)
		die(
				"slock: cannot retrieve password entry (make sure to suid or sgid slock)");
	endpwent();
	rval = pw->pw_passwd;

#if HAVE_SHADOW_H
	if (strlen(rval) >= 1)
	{ /* kludge, assumes pw placeholder entry has len >= 1 */
		struct spwd *sp;
		sp = getspnam(getenv("USER"));
		if(!sp)
		die("slock: cannot retrieve shadow entry (make sure to suid or sgid slock)\n");
		endspent();
		rval = sp->sp_pwdp;
	}
#endif

	/* drop privileges */
	if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0)
		die("slock: cannot drop privileges");
	return rval;
}
#endif

static void change_background(int screen, int nscreens,
		Display* dpy, Lock** locks, int color)
{
	for (screen = 0; screen < nscreens; screen++)
	{
		if(spy_mode)
			XMoveWindow(dpy, locks[screen]->win, 0, 0);

		XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[color]);
		XClearWindow(dpy, locks[screen]->win);
	}
}

#ifdef HAVE_BSD_AUTH
static void readpw(Display *dpy)
#else
static void readpw(Display *dpy, const char *pws)
#endif
{
	char buf[32], passwd[256];
	int num, screen;
	unsigned int len, llen;
	KeySym ksym;
	XEvent ev;

	len = llen = 0;
	running = True;

	/* As "slock" stands for "Simple X display locker", the DPMS settings
	 * had been removed and you can set it with "xset" or some other
	 * utility. This way the user can easily set a customized DPMS
	 * timeout. */
	while (running && !XNextEvent(dpy, &ev))
	{
		switch (ev.type)
		{

		case KeyPress:

			buf[0] = 0;
			num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
			if (IsKeypadKey(ksym))
			{
				if (ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if (IsFunctionKey(ksym) || IsKeypadKey(ksym)
					|| IsMiscFunctionKey(ksym) || IsPFKey(ksym)
					|| IsPrivateKeypadKey(ksym))
				continue;
			switch (ksym)
			{
			case XK_Return:
				passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
				running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
				running = strcmp(crypt(passwd, pws), pws);
#endif
				if (running != False)
					XBell(dpy, 100);
				len = 0;
				break;
			case XK_Escape:
				len = 0;
				break;
			case XK_BackSpace:
				if (len)
					--len;
				break;
			default:
				if (num && !iscntrl((int) buf[0])
						&& (len + num < sizeof passwd))
				{
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}

			if (llen == 0 && len != 0)
			{
				if(!spy_mode)
					change_background(screen, nscreens, dpy, locks, 1);
			}
			else if (llen != 0 && len == 0)
			{
				if(!spy_mode)
					change_background(screen, nscreens, dpy, locks, 0);
			}

			llen = len;
			break;

		case ButtonPress:

			if (spy_mode)
			{
				// to shock the intruder, wait little time
				sleep(1.5);
				change_background(screen, nscreens, dpy, locks, 0);
				// generate a sound
				XBell(dpy, 100);
			}

			break;

		default:

			for (screen = 0; screen < nscreens; screen++)
				XRaiseWindow(dpy, locks[screen]->win);

			break;
		}

	}
}

static void unlockscreen(Display *dpy, Lock *lock)
{
	if (dpy == NULL || lock == NULL )
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen) , lock->colors, 2, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

static Lock * lockscreen(Display *dpy, int screen)
{
	char curs[] =
	{ 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned int len;
	Lock *lock;
	XColor color, dummy;

	XSetWindowAttributes wa;
	Cursor cursor;

	if (dpy == NULL || screen < 0)
		return NULL ;

	lock = malloc(sizeof(Lock));
	if (lock == NULL )
		return NULL ;

	lock->screen = screen;

	lock->root = RootWindow(dpy, lock->screen);

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = BlackPixel(dpy, lock->screen);

	lock->win = XCreateWindow(dpy, lock->root, 0, 0,
			DisplayWidth(dpy, lock->screen) , DisplayHeight(dpy, lock->screen),
			0, DefaultDepth(dpy, lock->screen), CopyFromParent,
			DefaultVisual(dpy, lock->screen),
			(spy_mode) ? CWOverrideRedirect : CWOverrideRedirect | CWBackPixel,
			&wa) ;

	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen) , COLOR3, &color, &dummy);
	lock->colors[2] = color.pixel;
	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen) , COLOR2, &color, &dummy);
	lock->colors[1] = color.pixel;
	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen) , COLOR1, &color, &dummy);
	lock->colors[0] = color.pixel;

	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);

	if(spy_mode)
	{
		// non-invisible cursor to let people beleive that the screen isn't locked
		// TODO: no need for a visible cursor since the window value mask is CWOverrideRedirect
		//cursor = XCreateFontCursor(dpy, XC_top_left_arrow);

		// To simulate a non-locked screen, it's necessary to move the window instead
		// of making the background opaque for this reason:
		//
		//   If a window has a background (almost all do), it obscures the other window for purposes of out-
		//   put. Attempts to output to the obscured area do nothing, and no input events (for example,
		//   pointer motion) are generated for the obscured area.
		XMoveWindow(dpy, lock->win, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen));
	}
	else
		// invisible cursor
		cursor = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);

	XDefineCursor(dpy, lock->win, cursor);
	XMapRaised(dpy, lock->win);

	for (len = 1000; len; len--)
	{
		if (XGrabPointer(dpy, lock->root, False,
				ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
				GrabModeAsync, GrabModeAsync, None, cursor,
				CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if (running && (len > 0))
	{
		for (len = 1000; len; len--)
		{
			if (XGrabKeyboard(dpy, lock->root, True, GrabModeAsync,
					GrabModeAsync, CurrentTime) == GrabSuccess)
				break;
			usleep(1000);
		}
	}

	running &= (len > 0);
	if (!running)
	{
		unlockscreen(dpy, lock);
		lock = NULL;
	}
	else
		XSelectInput(dpy, lock->root, SubstructureNotifyMask);

	return lock;
}

static void usage(void)
{
	fprintf(stderr, "usage: slock [-v] [-spy] [-h]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int screen;

	if ((argc == 2) && !strcmp("-v", argv[1]))
		die("slock-%s, © 2006-2012 Anselm R Garbe\n", VERSION);
	if ((argc == 2) && !strcmp("-spy", argv[1]))
		spy_mode = True;
	if ((argc == 2) && !strcmp("-h", argv[1]))
		usage();

	if (!getpwuid(getuid()))
		die("slock: no passwd entry for you");

#ifndef HAVE_BSD_AUTH
	pws = getpw();
#endif

	if (!(dpy = XOpenDisplay(0)))
		die("slock: cannot open display");
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	locks = malloc(sizeof(Lock *) * nscreens);
	if (locks == NULL )
		die("slock: malloc: %s", strerror(errno));
	int nlocks = 0;
	for (screen = 0; screen < nscreens; screen++)
	{
		if ((locks[screen] = lockscreen(dpy, screen)) != NULL )
			nlocks++;
	}
	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0)
	{ // nothing to protect
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}

	/* Everything is now blank. Now wait for the correct password. */
#ifdef HAVE_BSD_AUTH
	readpw(dpy);
#else
	readpw(dpy, pws);
#endif

	/* Password ok, unlock everything and quit. */
	for (screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
