# Command line and GTK 3 clients for JustJournal.com

Provides commandline and graphical clients for JustJournal.  

## Command Line Client

### Available jjclient packages

To install on MidnightBSD:
mport install jjclient

To install on FreeBSD:
pkg install jjclient

(note freebsd version is very old)

### Build jjclient from source

The clients are written in C99 for MidnightBSD, FreeBSD, and Linux.
Version 2.1.0 uses the JSON REST API over HTTPS. It requires libcurl,
Jansson, and pkg-config to build.

#### Compiling on MidnightBSD & FreeBSD: (using bmake)

Install the dependencies:
MidnightBSD:
mport install curl jansson pkgconf

FreeBSD:
pkg install curl jansson pkgconf

Then run:

make
doas make install

NOTE: uses Makefile

#### Compiling on Linux: (using gmake)

install the dependencies

On Debian/Ubuntu Linux:
sudo apt-get install libcurl4-openssl-dev libjansson-dev pkg-config

make
make install

### Using the software

jjclient -u username -p password

Supply your username and password.  After starting the software,
type your blog entry and use control + D and then return to end. (EOF)

Alternatively, you may use a text file like this:
jjclient -u username -p password < myfile.txt

Other flags include -s for subject, -d for debug messages, and -r to list
the latest 15 entries (including private entries and drafts for your account).
Use -h hostname to connect to another compatible HTTPS server.

Subjects are sent separately from entry bodies. The legacy <title>...</title>
and <music>...</music> conventions remain supported. Input must be UTF-8;
entries larger than 31,999 bytes are rejected instead of truncated.

## Graphical Client using GTK 3

A graphical client is provided in the gtk folder.

This client requires GTK 3, libcurl, Jansson, and pkg-config to build.
Install gtk3 on MidnightBSD/FreeBSD, or libgtk-3-dev on Debian/Ubuntu,
in addition to the dependencies above.

Run the build commands below from the gtk directory. The optional Title field
sets the entry subject separately from the body. Failed requests retain the
title, entry, and login fields so you can correct the problem and retry.
A successful post clears these fields.

### Available gtkjj packages

To install on MidnightBSD:
mport install gtkjj

### Build gtkjj from source

Compiling on MidnightBSD:

make
doas make install

Compiling on Linux
gmake
gmake install

## Compatibility

Both clients are version 2.1.0 and target MidnightBSD, FreeBSD, and Linux.
The server must support the JSON session login at POST /api/login and entry
creation at POST /api/entry. The CLI's -r option uses the authenticated
GET /api/entry/{username}/size/15/page/0 endpoint. Servers supporting only
XML-RPC require an older client.

Both clients verify HTTPS certificates. Session cookies are held in memory
for the current operation and are never saved to disk. Redirects and
unsuccessful HTTP or JSON responses are treated as failures. A failed or
interrupted post is not retried automatically; check your journal before
retrying if the server may have saved the entry.

The request format follows the server's
[login controller](https://github.com/justjournal/justjournal/blob/d75bc9ae0b8bac12a22804356c6aefe07c66dd9f/src/main/java/com/justjournal/ctl/api/LoginController.java),
[entry controller](https://github.com/justjournal/justjournal/blob/d75bc9ae0b8bac12a22804356c6aefe07c66dd9f/src/main/java/com/justjournal/ctl/api/entry/EntryController.java), and
[entry model](https://github.com/justjournal/justjournal/blob/d75bc9ae0b8bac12a22804356c6aefe07c66dd9f/src/main/java/com/justjournal/model/api/EntryTo.java).

## Tests

Run `make check` (BSD make) or `gmake check` (GNU make) from the repository
root. Tests require Python 3, openssl, and the build dependencies. GTK callback
tests additionally require GTK 3 and Xvfb; they are reported as skipped if
those tools are unavailable.

The suite runs both clients against a local HTTPS test server with a temporary
certificate. It checks login and session cookies, JSON escaping, entry defaults,
recent-entry JSON/HAL responses, failure handling, and GTK draft retention.
It does not contact JustJournal.com or post to a real account.

## History

Originally hosted on SourceForge <https://sourceforge.net/projects/justjournal/files/JustJournal%20UNIX%20Client/>

Written by Lucas Holt for JustJournal.com
