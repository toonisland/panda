#define LOCAL_LIBS express pandabase
#define OTHER_LIBS interrogatedb:c dconfig:c dtoolconfig:m \
                   dtoolutil:c dtoolbase:c prc:c dtool:m
#define USE_PACKAGES zlib net openssl

#begin lib_target
  #define TARGET downloader

  #define COMBINED_SOURCES $[TARGET]_composite1.cxx $[TARGET]_composite2.cxx

  #define SOURCES \
    config_downloader.h \
    bioPtr.I bioPtr.h \
    bioStreamPtr.I bioStreamPtr.h \
    bioStream.I bioStream.h bioStreamBuf.h \
    chunkedStream.I chunkedStream.h \
    chunkedStreamBuf.h chunkedStreamBuf.I \
    decompressor.h decompressor.I \
    documentSpec.I documentSpec.h \
    downloadDb.I downloadDb.h \
    download_utils.h \
    extractor.h \
    httpAuthorization.I httpAuthorization.h \
    httpBasicAuthorization.I httpBasicAuthorization.h \
    httpChannel.I httpChannel.h \
    httpClient.I httpClient.h \
    httpCookie.I httpCookie.h \
    httpDate.I httpDate.h \
    httpDigestAuthorization.I httpDigestAuthorization.h \
    httpEntityTag.I httpEntityTag.h \
    httpEnum.h \
    identityStream.I identityStream.h \
    identityStreamBuf.h identityStreamBuf.I \
    multiplexStream.I multiplexStream.h \
    multiplexStreamBuf.I multiplexStreamBuf.h \
    patcher.h patcher.I \
    socketStream.h socketStream.I \
    ssl_utils.h \
    urlSpec.I urlSpec.h
    
  #define INCLUDED_SOURCES                 \
    config_downloader.cxx \
    bioPtr.cxx \
    bioStreamPtr.cxx \
    bioStream.cxx bioStreamBuf.cxx \
    chunkedStream.cxx chunkedStreamBuf.cxx \
    decompressor.cxx \
    documentSpec.cxx \
    downloadDb.cxx \
    download_utils.cxx \
    extractor.cxx \
    httpAuthorization.cxx \
    httpBasicAuthorization.cxx \
    httpChannel.cxx \
    httpClient.cxx \
    httpCookie.cxx \
    httpDate.cxx \
    httpDigestAuthorization.cxx \
    httpEntityTag.cxx \
    httpEnum.cxx \
    identityStream.cxx identityStreamBuf.cxx \
    multiplexStream.cxx multiplexStreamBuf.cxx \
    patcher.cxx \
    socketStream.cxx \
    ssl_utils.cxx \
    urlSpec.cxx

  #define INSTALL_HEADERS \
    bioPtr.I bioPtr.h \
    bioStreamPtr.I bioStreamPtr.h \
    bioStream.I bioStream.h bioStreamBuf.h \
    chunkedStream.I chunkedStream.h \
    chunkedStreamBuf.h chunkedStreamBuf.I \
    config_downloader.h \
    decompressor.h decompressor.I \
    documentSpec.h documentSpec.I \
    download_utils.h downloadDb.h downloadDb.I \
    extractor.h \
    httpAuthorization.I httpAuthorization.h \
    httpBasicAuthorization.I httpBasicAuthorization.h \
    httpChannel.I httpChannel.h \
    httpClient.I httpClient.h \
    httpCookie.I httpCookie.h \
    httpDate.I httpDate.h \
    httpDigestAuthorization.I httpDigestAuthorization.h \
    httpEntityTag.I httpEntityTag.h \
    httpEnum.h \
    identityStream.I identityStream.h \
    identityStreamBuf.h identityStreamBuf.I \
    multiplexStream.I multiplexStream.h \
    multiplexStreamBuf.I multiplexStreamBuf.h \
    patcher.h patcher.I \
    socketStream.h socketStream.I \
    ssl_utils.h \
    urlSpec.h urlSpec.I
    
  #define IGATESCAN all

#end lib_target

// This is a handy file for identifying public certificate authorities
// (and thus almost any public https server).  It was taken from the
// OpenSSL distribution; if you need a fresher copy, go get a new one
// from the latest OpenSSL.  To use this file, point the variable
// ssl-certificates in your Configrc file to its installed location,
// e.g:
//
// ssl-certificates /usr/local/panda/install/shared/ca-bundle.crt
//
#define INSTALL_DATA ca-bundle.crt
