// Filename: bioStreamBuf.cxx
// Created by:  drose (25Sep02)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) Carnegie Mellon University.  All rights reserved.
//
// All use of this software is subject to the terms of the revised BSD
// license.  You should have received a copy of this license along
// with this source code in a file named "LICENSE."
//
////////////////////////////////////////////////////////////////////

#include "bioStreamBuf.h"
#include "config_downloader.h"
#include "ssl_utils.h"
#include <errno.h>

#ifdef HAVE_OPENSSL

#ifdef WIN32_VC
  #include <WinSock2.h>
  #include <windows.h>  // for WSAGetLastError()
  #undef X509_NAME
#endif  // WIN32_VC

#ifndef HAVE_STREAMSIZE
// Some compilers (notably SGI) don't define this for us
typedef int streamsize;
#endif /* HAVE_STREAMSIZE */

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::Constructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
BioStreamBuf::
BioStreamBuf() {
  _is_closed = false;

#ifdef HAVE_IOSTREAM
  _buffer = (char *)PANDA_MALLOC_ARRAY(8192);
  char *ebuf = _buffer + 8192;
  char *mbuf = _buffer + 4096;
  setg(_buffer, mbuf, mbuf);
  setp(mbuf, ebuf);

#else
  allocate();
  // Chop the buffer in half.  The bottom half goes to the get buffer;
  // the top half goes to the put buffer.
  char *b = base();
  char *t = ebuf();
  char *m = b + (t - b) / 2;
  setg(b, m, m);
  setp(b, m);
#endif
}

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::Destructor
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
BioStreamBuf::
~BioStreamBuf() {
  close();
#ifdef HAVE_IOSTREAM
  PANDA_FREE_ARRAY(_buffer);
#endif
}

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::open
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
void BioStreamBuf::
open(BioPtr *source) {
  _source = source;
}

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::close
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
void BioStreamBuf::
close() {
  sync();
  _source.clear();
}

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::overflow
//       Access: Protected, Virtual
//  Description: Called by the system ostream implementation when its
//               internal buffer is filled, plus one character.
////////////////////////////////////////////////////////////////////
int BioStreamBuf::
overflow(int ch) {
  size_t n = pptr() - pbase();
  if (downloader_cat.is_spam()) {
    downloader_cat.spam()
      << "BioStreamBuf::overflow, " << n << " bytes\n";
  }
  if (n != 0) {
    size_t num_wrote = write_chars(pbase(), n);
    pbump(-(int)n);
    if (num_wrote != n) {
      return EOF;
    }
  }

  if (ch != EOF) {
    // Store the next character back in the buffer.
    *pptr() = ch;
    pbump(1);
  }

  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::sync
//       Access: Protected, Virtual
//  Description: Called by the system iostream implementation to
//               implement a flush operation.
////////////////////////////////////////////////////////////////////
int BioStreamBuf::
sync() {
  /*
  size_t n = egptr() - gptr();
  gbump(n);
  */

  size_t n = pptr() - pbase();

  if (downloader_cat.is_spam()) {
    downloader_cat.spam()
      << "BioStreamBuf::sync, " << n << " bytes\n";
  }

  size_t num_wrote = write_chars(pbase(), n);
  pbump(-(int)n);

  if (num_wrote != n) {
    return EOF;
  }

  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::underflow
//       Access: Protected, Virtual
//  Description: Called by the system istream implementation when its
//               internal buffer needs more characters.
////////////////////////////////////////////////////////////////////
int BioStreamBuf::
underflow() {
  // Sometimes underflow() is called even if the buffer is not empty.
  if (gptr() >= egptr()) {
    size_t buffer_size = egptr() - eback();
    gbump(-(int)buffer_size);

    size_t num_bytes = buffer_size;

    // BIO_read might return -1 or -2 on eof or error, so we have to
    // allow for negative numbers.
    int read_count = BIO_read(*_source, gptr(), buffer_size);
    thread_consider_yield();

    if (read_count != (int)num_bytes) {
      // Oops, we didn't read what we thought we would.
      if (read_count <= 0) {
        // Immediately save the os error in case we screw up and do
        // something that will change its value before we can output
        // it.
#ifdef WIN32_VC
        int os_error = WSAGetLastError();
#else
        int os_error = errno;
#endif  // WIN32_VC

        // Though BIO_eof() is tempting, it appears there are cases in
        // which that never returns true, if the socket is closed by
        // the server.  But BIO_should_retry() *appears* to be
        // reliable.
        _is_closed = !BIO_should_retry(*_source);
        if (_is_closed) {
          downloader_cat.info()
            << "Lost connection to "
            << _source->get_server_name() << ":" 
            << _source->get_port() << " (" << read_count << ").\n";
          notify_ssl_errors();

          SSL *ssl = NULL;
          BIO_get_ssl(*_source, &ssl);
          if (ssl != (SSL *)NULL) {
            downloader_cat.warning()
              << "OpenSSL error code: " << SSL_get_error(ssl, read_count)
              << "\n";
          }

#ifdef WIN32_VC
          downloader_cat.warning()
            << "Windows error code: " << os_error << "\n";
#else
          downloader_cat.warning()
            << "Unix error code: " << os_error << "\n";
#endif  // WIN32_VC
        }
        gbump(num_bytes);
        return EOF;
      } 
        
      // Slide what we did read to the top of the buffer.
      nassertr(read_count < (int)num_bytes, EOF);
      size_t delta = (int)num_bytes - read_count;
      memmove(gptr() + delta, gptr(), read_count);
      gbump(delta);
    }

    if (downloader_cat.is_spam()) {
      downloader_cat.spam()
        << "read " << read_count << " bytes from " << _source << "\n";
    }
  }

  return (unsigned char)*gptr();
}

////////////////////////////////////////////////////////////////////
//     Function: BioStreamBuf::write_chars
//       Access: Private
//  Description: Sends some characters to the dest stream.  Does not
//               return until all characters are sent or the socket is
//               closed, even if the underlying BIO is non-blocking.
////////////////////////////////////////////////////////////////////
size_t BioStreamBuf::
write_chars(const char *start, size_t length) {
  if (length != 0) {
    size_t wrote_so_far = 0;

    int write_count = BIO_write(*_source, start, length);
    thread_consider_yield();
    while (write_count != (int)(length - wrote_so_far)) {
      if (write_count <= 0) {
        // http://www.openssl.org/docs/crypto/BIO_s_bio.html
        // "Calls to BIO_write() will place data in the buffer or
        // request a retry if the buffer is full."
        //
        // when the server is terminated, this seems to be the best
        // way of detecting that case on the client: a BIO write error
        // without a retry request
        //_is_closed = !BIO_should_retry(*_source);
        //_is_closed = BIO_eof(*_source);
        _is_closed = !BIO_should_write(*_source);
        if (_is_closed) {
          return wrote_so_far;
        }
        
        // Block on the underlying socket before we try to write some
        // more.
        int fd = -1;
        BIO_get_fd(*_source, &fd);
        if (fd < 0) {
          downloader_cat.warning()
            << "socket BIO has no file descriptor.\n";
        } else {
          if (downloader_cat.is_spam()) {
            downloader_cat.spam()
              << "waiting to write to BIO.\n";
          }
#if defined(HAVE_THREADS) && defined(SIMPLE_THREADS)
          // In SIMPLE_THREADS mode, instead of blocking, simply yield
          // the thread.
          thread_yield();
#else
          // In any other threading mode, we actually want to block.
          fd_set wset;
          FD_ZERO(&wset);
          FD_SET(fd, &wset);
          select(fd + 1, NULL, &wset, NULL, NULL);
#endif  // SIMPLE_THREADS
        }        
        
      } else {
        // wrote some characters.
        wrote_so_far += write_count;
        if (downloader_cat.is_spam()) {
          downloader_cat.spam()
            << "wrote " << write_count << " bytes to " << _source << "\n";
        }
      }
      
      // Try to write some more.
      write_count = BIO_write(*_source, start + wrote_so_far, length - wrote_so_far);
      if (downloader_cat.is_spam()) {
        downloader_cat.spam()
          << "continued, wrote " << write_count << " bytes.\n";
      }
      thread_consider_yield();
    }
    if (downloader_cat.is_spam()) {
      downloader_cat.spam()
        << "wrote " << write_count << " bytes to " << _source << "\n";
    }
  }

  return length;
}

#endif  // HAVE_OPENSSL
