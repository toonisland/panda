// DIR_TYPE "metalib" indicates we are building a shared library that
// consists mostly of references to other shared libraries.  Under
// Windows, this directly produces a DLL (as opposed to the regular
// src libraries, which don't produce anything but a pile of OBJ files
// under Windows).

#define DIR_TYPE metalib
#define BUILDING_DLL BUILDING_PANDAGLES
#define BUILD_DIRECTORY $[HAVE_GLES]

#define COMPONENT_LIBS \
    p3glesgsg p3egldisplay_gles1

#define LOCAL_LIBS p3gsgbase p3display p3express
#define OTHER_LIBS p3interrogatedb \
                   p3dtoolutil:c p3dtoolbase:c p3dtool:m p3prc

#begin metalib_target
  #define TARGET pandagles
  #define SOURCES pandagles.cxx pandagles.h
  #define INSTALL_HEADERS pandagles.h
#end metalib_target
