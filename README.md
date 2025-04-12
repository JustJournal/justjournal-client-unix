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

This software has been tested on MidnightBSD, FreeBSD, Linux and Mac OS X.  
It requires xmlrpc-c to operate.

#### Compiling on MidnightBSD & FreeBSD: (using bmake)

Install the dependencies:
MidnightBSD:
mport install xmlrpc-c pkgconf

FreeBSD:
pkg install xmlrpc-c pkgconf

Then run:

make
doas make install

NOTE: uses Makefile

#### Compiling on Linux: (using gmake)

install the dependencies

On Ubuntu Linux, old releases needed:
sudo apt-get install -qq libxmlrpc-c3-dev pkg-config

ubuntu 24.10+ needs
sudo apt-get install libxmlrpc-core-c3t64 libxmlrpc-core-c3-dev pkg-config

make
make install

### Using the software

jjclient -u username -p password

Supply your username and password.  After starting the software,
type your blog entry and use control + D and then return to end. (EOF)

Alternatively, you may use a text file like this:
jjclient -u username -p password < myfile.txt

Other flags include -s for subject and -d for turning on debug messages

## Graphical Client using GTK 3

A graphical client is provided in the gtk folder.

This client requires Gtk 3 and pkg-config to build along with
xmlrpc-c.

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

JustJournal client 2.0.1 or higher is required for JustJournal 3.0.11+.  
JustJournal 3.0.10 is not supported
JustJournal < 3.0.10 will work with older clients.

## History

Originally hosted on SourceForge <https://sourceforge.net/projects/justjournal/files/JustJournal%20UNIX%20Client/>

Written by Lucas Holt for JustJournal.com
