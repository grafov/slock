slock - simple screen locker
============================

The most simple screen locker utility available for X.


Requirements
------------

* [Devil](http://openil.sourceforge.net/)
* [MPlayer](http://www.mplayerhq.hu) (optional)

Installation
------------

Edit config.mk to match your local setup (slock is installed into the /usr/local namespace by default).

In order to build and install slock execute

```sudo make clean install```

Running slock
-------------

<h3>Standard</h3>

`slock`

Lock X by displaying a black screen and waiting for your input.

<h3>Spy Mode</h3>

`slock -s`

Lock X whereas your screen looks unlocked. If a mouse button is hit the screen becomes black, a few _KERNEL PANIC_ messages are displayed and a snapshot of the unwanted user is taken and rendered. The spy mode requires mplayer to be installed which is responsible for the webcam snapshot.

Exit slock
----------

Just enter your password.