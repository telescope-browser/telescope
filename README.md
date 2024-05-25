# Telescope

Telescope is a Emacs/w3m-inspired browser for the "small internet"
that supports Gemini, Gopher and Finger.

In features some expected stuff (tabs, bookmarks, history, client
certificates, ...) with an UI that's very much Emacs and w3m inspired,
and a privsep design.

There are still various things missing or, if you prefer, various
things that you can help develop :)

 - other "smol internet" protocols
 - subscriptions
 - TOFU out-of-band verification and/or DANE
 - multiple UIs: at the moment it uses only ncurses, but telescope
   shouldn't be restricted to TTYs only!

[![asciicast](https://asciinema.org/a/426862.svg)](https://asciinema.org/a/426862)


## Why yet another browser?

One of the great virtues of Gemini is its simplicity.  It means that
writing browsers or server is easy and thus a plethora of those
exists.  I myself routinely switch between a couple of them, depending
on my mood.

More browsers means more choice for the users, and more stability for
the protocol too.

However, Telescope was ultimately written for fun, on a whim, just to
play with ncurses, libtls, async I/O and the macros from `sys/queue.h`,
but I'd like to finish it into a complete Gemini browser.


## Goals

 - Fun: hacking on Telescope should be fun.
 - Clean: write readable and clean code mostly following the OpenBSD
   style(9) guideline.  Don't become a kitchen sink.
 - Secure: write secure code with privilege separation to mitigate the
   security risks of possible bugs.
 - Fast: it features a modern, fast, event-based asynchronous I/O
   model.
 - Cooperation: re-use existing conventions to allow inter-operations
   and easy migrations from/to other clients.


## TOFU

Telescope aims to use the "Trust, but Verify (where appropriate)"
approach outlined here:
[gemini://thfr.info/gemini/modified-trust-verify.gmi](gemini://thfr.info/gemini/modified-trust-verify.gmi).

The idea is to define three level of verification for a certificate:

 - **untrusted**: the server fingerprint does NOT match the stored
   value
 - **trusted**: the server fingerprint matches the stored one
 - **verified**: the fingerprint matches and has been verified
   out-of-band by the client.

Most of the time, the `trusted` level is enough, but where is
appropriate users should be able to verify out-of-band the
certificate.

At the moment there is no UI for out-of-band verification though.


## Building

Telescope depends on ncursesw and libtls or libretls.
[libgrapheme][libgrapheme] is an optional dependency: there's a
bundled copy but it's reccomended to install it with a package manager
if available.  When building from a git checkout, yacc (or bison) is
also needed.

To build execute:

	$ ./autogen.sh		# only from git checkouts
	$ ./configure
	$ make
	$ sudo make install	# eventually

The configure script has optional support for building with libraries
provided by your distribution instead of using the bundled versions:

 - `--with-libbsd`: link with [libbsd][libbsd]
 - `--with-libimsg`: link with the [imsg-compat][imsg-compat] library

[libbsd]:	https://libbsd.freedesktop.org
[imsg-compat]:	https://github.com/bsd-ac/imsg-compat
[libgrapheme]:	https://libs.suckless.org/libgrapheme/


## Contributing

Any form of contribution is appreciated, not only patches or bug
reports: feel free to open an issue or send an email to
`telescope@omarpolo.com`.

If you have a sample configuration, custom theme, a script or anything
that could be helpful to others, consider adding it to the `contrib`
directory.

Consider also joining the official
[irc channel](ircs://irc.libera.chat:6697/telescope),
`#telescope` on libera.chat!


## User files

Telescope stores user files according to the [XDG Base Directory
Specification][xdg] by default.  The usage and contents of these files
are described in [the man page](telescope.1), under "FILES".

At the moment, only one instance of Telescope can be running at time per
user.


## License

Telescope is distributed under a BSD-style licence.  The main code is
either under the ISC or is Public Domain, but some files under `compat/`
are 3-Clause BSD or MIT.  See the first few lines of every file or
`about:license` inside telescope for the copyright information.

`data/emoji.txt` is copyright © 2022 Unicode, Inc. and distributed
under the [UNICODE, Inc license agreement][unicode-license].


[unicode-license]: https://www.unicode.org/license.html
[xdg]: https://specifications.freedesktop.org/basedir-spec/latest/
