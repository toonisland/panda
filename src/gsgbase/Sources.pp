#define OTHER_LIBS p3interrogatedb:m \
                   p3dtoolutil:c p3dtoolbase:c p3dtool:m p3prc:m

#begin lib_target
  #define TARGET p3gsgbase
  #define LOCAL_LIBS \
    p3putil p3linmath

  #define BUILDING_DLL BUILDING_PANDA_GSGBASE

  #define SOURCES \
    config_gsgbase.h \
    graphicsOutputBase.I graphicsOutputBase.h \
    graphicsStateGuardianBase.h

  #define COMPOSITE_SOURCES \
    config_gsgbase.cxx \
    graphicsOutputBase.cxx \
    graphicsStateGuardianBase.cxx

  #define INSTALL_HEADERS \
    config_gsgbase.h \
    graphicsOutputBase.I graphicsOutputBase.h \
    graphicsStateGuardianBase.h

  #define IGATESCAN all

#end lib_target

#begin test_bin_target
  #define TARGET test_gsgbase
  #define LOCAL_LIBS \
    p3gsgbase

  #define SOURCES \
    test_gsgbase.cxx

#end test_bin_target
