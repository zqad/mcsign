mcsign
======

A pigmap-compatible sign data fetcher written in C using the cnbt library

Building
--------

A version of cnbt that has been tested to work is included as a git submodule.
This means that the first thing that you need to do to build is to initialize
the submodule:

    $ git clone git://github.com/zqad/mcsign.git
    Cloning, blah blah.
    $ git submodule update --init
    Submodule 'external/cnbt' (git://github.com/FliPPeh/cNBT.git) registered for path 'external/cnbt'
    Cloning into 'external/cnbt'...
    blah blah, etc.
    $ 

And you are done! A simple make should now build it all. mcsign uses glib, so
if the build fails, make sure that you have the development package for glib 2.0
installed. For example, it is named libglib2.0-dev in Debian and Ubuntu.

A build problem with cnbt has been noted; -Wcpp is not supported in older gcc
versions. If you use one of those, upgrade. Or, if you really don't want to,
just remove that argument in the cnbt Makefile.

Running
-------

mcsign is becoming increasingly user friendly. Run mcsign with -h or --help to
get information about arguments.

Matching signs is controlled by a #define in mcsign.c that does a strcmp on the
first text line on a sign. By default, this is set to "#map" without the
quotes.

To trim down the sharp edges, I have included an example shell script called
run.sh that assumes that it's run from the same directory as mcsign, that the
world directory is located at ../minecraft/world, and that pigmap's output
directory is located in ../pigmap/output. This is configurable through a couple
of variables in the beginning of the script.

As for including the information in a pigmap map, the default pigmap index html
file tries to load markers from the array markerData. The example run.sh
concatenates all output files to a file named markers.js placed in the pigmap
output directory. To include this in the pigmap html page, add the following
tags to template.html in the pigmap directory:

    <script type="text/javascript" src="markers.js"></script>

The marker.js should then be picked up by your browser.

Developer Information
---------------------
Source is managed through Git.

To do a read-only clone, use the git protocol:

    git clone git://github.com/zqad/mcsign.git

To view the source in your browser, just visit the homepage on github:
<https://github.com/zqad/mcsign>
