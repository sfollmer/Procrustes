Procrustes
==========

Interactive Tool for 3D modeling built on OpenSCAD


Prerequisites:

XCode, including XCode command-line tools (install from XCode Preferences).
CMake, which can be installed manually or through MacPorts/homebrew.


instructions to build:

Then after you've cloned this git repository, run the script that sets up the environment variables.

cd openscad

source setenv_mac-clang.sh

Then run the script to compile all the prerequisite libraries above:

./scripts/macosx-build-dependencies.sh

Compile:
First, run 'qmake' from Qt4 to generate a Makefile. On some systems you need to run 'qmake4', 'qmake-qt4' or something alike to run the qt4 version of the tool.

sudo make install

