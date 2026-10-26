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
Version 2.2.0 uses the JSON REST API over HTTPS. Building requires CMake 3.16
or newer, a C compiler and build tool (make or Ninja), libcurl, Jansson, and
pkg-config.

Install the dependencies on MidnightBSD:

```sh
mport install cmake curl jansson pkgconf
```

On FreeBSD:

```sh
pkg install cmake curl jansson pkgconf
```

On Debian/Ubuntu Linux:

```sh
sudo apt-get install build-essential cmake libcurl4-openssl-dev libjansson-dev pkg-config
```

From the repository root, configure and build the CLI client:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is `build/jjclient`. Install it and its manual page with:

```sh
cmake --install build
```

Installation defaults to `/usr/local`; use your system's privilege tool if
needed. Set `-DCMAKE_INSTALL_PREFIX=/your/prefix` when configuring to change
that prefix. Staged packaging is supported with
`DESTDIR=/your/stage cmake --install build`.

The root `make` (BSD make) and `gmake` (GNU make) commands remain available as
CMake wrappers and place `jjclient` in the repository root.

### Using the software

jjclient -u username -p password

Supply your username and password.  After starting the software,
type your blog entry and use control + D and then return to end. (EOF)

Alternatively, you may use a text file like this:
jjclient -u username -p password < myfile.txt

Other flags include -s for subject, -d for debug messages, and -r to list
the latest 15 entries (including private entries and drafts for your account).
Use -h hostname to connect to another compatible HTTPS server.
Control characters in retrieved entries are displayed as escape sequences;
newlines, tabs, and normal UTF-8 text remain intact.

Subjects are sent separately from entry bodies. The legacy <title>...</title>
and <music>...</music> conventions remain supported. Input must be UTF-8;
entries larger than 31,999 bytes are rejected instead of truncated.

## Graphical Client using GTK 3

A graphical client is provided in the gtk folder.

This client additionally requires GTK 3 to build.
Install gtk3 on MidnightBSD/FreeBSD, or libgtk-3-dev on Debian/Ubuntu,
in addition to the dependencies above.

The optional Title field sets the entry subject separately from the body.
Music accepts the current artist/song; Tags accepts tags separated by spaces or commas and converts them
to lowercase, matching the website. Empty tags are ignored.

Failed requests retain all fields so you can correct the problem and retry.
A successful post clears the title, music, tags, and entry body, keeping your
username and password filled in for the next post.

### Available gtkjj packages

To install on MidnightBSD:
mport install gtkjj

### Build gtkjj from source

From the repository root, enable GTK when configuring:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_GTK=ON
cmake --build build
cmake --install build
```

This builds and installs both clients. The graphical executable is
`build/gtkjj`. GTK is disabled by default, so CLI-only builds do not require
GTK development files.

For GTK-only packaging, build the `gtkjj` target and install the `gtk`
component:

```sh
cmake --build build --target gtkjj
DESTDIR=/your/stage cmake --install build --component gtk
```

The other installation components are `cli` (the CLI executable) and `man`
(its manual page). The legacy `make` or `gmake` commands in the `gtk`
directory remain available as CMake wrappers and place `gtkjj` there.

## Compatibility

Both clients are version 2.2.0 and target MidnightBSD, FreeBSD, and Linux.
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

Enable testing and run the suite through CTest:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
(cd build && ctest --output-on-failure)
```

Tests require Python 3, openssl, and the build dependencies. GTK callback
tests additionally require GTK 3 and Xvfb; they are reported as skipped if
those tools are unavailable. Add `-DBUILD_GTK=ON` to also build the GTK app.
The root `make check` and `gmake check` wrappers run the suite through CTest.

The suite runs both clients against a local HTTPS test server with a temporary
certificate. It checks login and session cookies, JSON escaping, entry defaults,
recent-entry JSON/HAL responses, failure handling, and GTK draft retention.
It does not contact JustJournal.com or post to a real account. CTest also runs
offline credential-buffer checks for complete erasure, JSON/UTF-8 encoding,
and allocation-failure handling.

## History

Originally hosted on SourceForge <https://sourceforge.net/projects/justjournal/files/JustJournal%20UNIX%20Client/>

Written by Lucas Holt for JustJournal.com
