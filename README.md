Procrustes
==========

Interactive Tool for 3D modeling built on OpenSCAD


Prerequisites:

XCode, including XCode command-line tools (install from XCode Preferences).
CMake, which can be installed manually or through MacPorts/homebrew.


instructions to build:

1. clone git repo

2. download the src files for the dependencies from here:
download src from this https://www.dropbox.com/sh/8ure355m046gxyl/RBFaPs98oC
put in Procrustes/libraries/src


Then after you've cloned this git repository, run the script that sets up the environment variables.

cd openscad

source setenv_mac-clang.sh

Then run the script to compile all the prerequisite libraries above:

./scripts/macosx-build-dependencies.sh

Compile:
First, run 'qmake' from Qt4 to generate a Makefile. On some systems you need to run 'qmake4', 'qmake-qt4' or something alike to run the qt4 version of the tool.

sudo make install
