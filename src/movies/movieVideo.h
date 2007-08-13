// Filename: movieVideo.h
// Created by: jyelon (02Jul07)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) 2001 - 2004, Disney Enterprises, Inc.  All rights reserved
//
// All use of this software is subject to the terms of the Panda 3d
// Software license.  You should have received a copy of this license
// along with this source code; you will also find a current copy of
// the license at http://etc.cmu.edu/panda3d/docs/license/ .
//
// To contact the maintainers of this program write to
// panda3d-general@lists.sourceforge.net .
//
////////////////////////////////////////////////////////////////////

#ifndef MOVIEVIDEO_H
#define MOVIEVIDEO_H

#include "pandabase.h"
#include "texture.h"
#include "pointerTo.h"

////////////////////////////////////////////////////////////////////
//       Class : MovieVideo
// Description : A MovieVideo is actually any source that provides
//               a sequence of video frames.  That could include an
//               AVI file, a digital camera, or an internet TV station.
//
//               Thread safety: each individual MovieVideo or
//               must be owned and accessed by a single thread.
//               It is OK for two different threads to open
//               the same file at the same time, as long as they
//               use separate MovieVideo objects.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA_MOVIES MovieVideo : public TypedWritableReferenceCount, public Namable {

 PUBLISHED:
  MovieVideo(const string &name = "Blank Video");
  virtual ~MovieVideo();
  INLINE int size_x() const;
  INLINE int size_y() const;
  INLINE int get_num_components() const;
  INLINE int length() const;
  INLINE bool can_seek() const;
  INLINE bool can_seek_zero() const;
  INLINE bool aborted() const;
  INLINE double last_start() const;
  INLINE double next_start() const;
  virtual void fetch_into_bitbucket(double time);
  virtual void fetch_into_texture(double time, Texture *t, int page);
  virtual void fetch_into_texture_rgb(double time, Texture *t, int page);
  virtual void fetch_into_texture_alpha(double time, Texture *t, int page, int alpha_src);
  virtual PT(MovieVideo) make_copy() const;
  static PT(MovieVideo) load(const Filename &name);

 public:
  virtual void fetch_into_buffer(double time, unsigned char *block, bool rgba);
  
 private:
  void allocate_conversion_buffer();
  unsigned char *_conversion_buffer;
  
 protected:
  int _size_x;
  int _size_y;
  int _num_components;
  double _length;
  bool _can_seek;
  bool _can_seek_zero;
  bool _aborted;
  double _last_start;
  double _next_start;
  
public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    TypedWritableReferenceCount::init_type();
    register_type(_type_handle, "MovieVideo",
                  TypedWritableReferenceCount::get_class_type());
  }
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}

private:
  static TypeHandle _type_handle;
};

#include "movieVideo.I"

#endif
