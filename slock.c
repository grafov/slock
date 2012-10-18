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
#include <X11/X.h>
#include <X11/Xatom.h>
#include <giblib/gib_imlib.h>

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
static Bool ergo = False;
static double opacity = 0.5;

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

			case XK_Escape:

				len = 0;
				break;

			case XK_BackSpace:

				if (len)
					--len;
				break;

			case XK_Return:

				// if not in ergonomic mode, check here if the password is typed in is correct
				if(!ergo)
				{
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
				}

			default:

				if (num && !iscntrl((int) buf[0])
						&& (len + num < sizeof passwd))
				{
					memcpy(passwd + len, buf, num);
					len += num;
				}

				// if in ergonomic mode, check here if the password is typed in so far is correct
				if(ergo)
				{
					passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
					running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
					running = strcmp(crypt(passwd, pws), pws);
#endif
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

				char *fontname;
				XFontStruct *font;
				char *text = "KERNEL PANIC";
				int text_width;
				int textx, texty;

		        fontname = "*clearlyu-medium-r-normal--0-0*";
		        font = XLoadQueryFont(dpy, fontname);
		        if (!font) {
		                fprintf(stderr, "unable to load preferred font: %s using fixed", fontname);
		        }
		        else
		        {
					XGCValues values;
					values.font = font->fid;

					fprintf( stdout, "%d/n", nscreens);

					GC pen = XCreateGC(dpy, locks[0]->win, GCForeground|GCLineWidth|GCFont, &values);
					text_width = XTextWidth(font, text, strlen(text));
					fprintf(stdout, "%d\n", text_width);

					int width = 800;
					int height = 800;

					textx = (width - text_width)/2;
					texty = (height + font->ascent)/2;

					fprintf(stdout, "textx: %d\n", textx);
					fprintf(stdout, "texty: %d\n", texty);
					for(int i=0; i<100; i++)
					{
						XDrawString(dpy, locks[0]->win, pen, 20, 20+20*i, text, strlen(text));
						XBell(dpy, 100);
						XFlush(dpy);
						usleep(500000);

						// the screen locker blocks as long as this loop is running
						// so a break is forced here until a solution is found to
						// process X events in this place
						break;
					}
		        }
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
	{
		// TODO: the opacity doesn't work, is this an issue in archlinux?
		XSelectInput(dpy, lock->root, SubstructureNotifyMask);
		unsigned int value = (unsigned int) (opacity * 0xffffffff);
		XChangeProperty(dpy, lock->win,
				XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False),
				XA_CARDINAL, 32, PropModeReplace,
				(unsigned char *) &value, 1L);
		XSync(dpy, False);
	}

	return lock;
}

static void usage(void)
{
	fprintf(stderr, "usage: slock [-v] [-s] [-e] [-h] [-o <OPACITY>]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int screen;

	{
		int result;
		while((result = getopt(argc,argv,"vo:seh")) != -1) {
			switch(result) {
				case 'v':
					die("slock-%s, © 2006-2012 Anselm R Garbe\n", VERSION);
				case 'o':
					opacity = atof(optarg);
					printf("%f\n", opacity);
					break;
				case 's':
					spy_mode = True;
					break;
				case 'e':
					ergo = True;
					break;
				case 'h':
				case '?':
					usage();
					break;
			}
		}
		if ((argc - optind) > 0)
			usage();
	}

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
