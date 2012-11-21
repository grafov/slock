slock - simple screen locker
============================

The most simple screen locker utility available for X.


Requirements
------------

* [Devil](http://openil.sourceforge.net/)
* [fswebca](http://www.firestorm.cx/fswebcam/) (optional)

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

Lock X whereas your screen looks unlocked. If a mouse button is hit the screen becomes black and a snapshot of the unwanted user is taken and rendered. In order to use this spy mode you need to have fswebcam installed.

Exit slock
----------

Just enter your password.