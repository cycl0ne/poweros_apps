PowerOS Apps
============

PowerOS Multiplatform Application SDK:

Following platforms are included
- x86 (fully working apps running on poweros on qemu)

How to start creating an app for poweros
========================================
1. create your own application directory in "apps" directory
2. create your own .c and .h files in your application directory.
3. simply copy "Makefile" and "MakeVars" files from any other application directory to your application directory. No need to modify anything in these files!
4. to compile your application, go to your application directory, run "make clean", "make" and "make install".
5. to compile all applications, go to "compile" directory, run "make clean", "make" and "make install".

