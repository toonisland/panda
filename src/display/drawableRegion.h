// Filename: drawableRegion.h
// Created by:  drose (11Jul02)
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

#ifndef DRAWABLEREGION_H
#define DRAWABLEREGION_H

#include "pandabase.h"
#include "luse.h"
#include "renderBuffer.h"
#include "typedWritableReferenceCount.h"

////////////////////////////////////////////////////////////////////
//       Class : DrawableRegion
// Description : This is a base class for GraphicsWindow (actually,
//               GraphicsOutput) and DisplayRegion, both of which are
//               conceptually rectangular regions into which drawing
//               commands may be issued.  Sometimes you want to deal
//               with a single display region, and sometimes you want
//               to deal with the whole window at once, particularly
//               for issuing clear commands and capturing screenshots.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA_DISPLAY DrawableRegion {
public:
  INLINE DrawableRegion();
  INLINE DrawableRegion(const DrawableRegion &copy);
  INLINE void operator = (const DrawableRegion &copy);
  virtual ~DrawableRegion();

  INLINE void copy_clear_settings(const DrawableRegion &copy);

PUBLISHED:
  // It seems awkward to have this type, and also
  // RenderBuffer::Type.  However, the fact that RenderBuffer::Type
  // is a bitmask makes it awfully awkward to work with.
  enum RenderTexturePlane {
    RTP_stencil=0,
    RTP_depth_stencil=1,
    RTP_depth=1,
    RTP_color,
    RTP_aux_rgba_0,
    RTP_aux_rgba_1,
    RTP_aux_rgba_2,
    RTP_aux_rgba_3,
    RTP_aux_hrgba_0,
    RTP_aux_hrgba_1,
    RTP_aux_hrgba_2,
    RTP_aux_hrgba_3,
    RTP_aux_float_0,
    RTP_aux_float_1,
    RTP_aux_float_2,
    RTP_aux_float_3,
    RTP_COUNT
  };

  INLINE void set_clear_color_active(bool clear_color_active);
  INLINE bool get_clear_color_active() const;

  INLINE void set_clear_depth_active(bool clear_depth_active);
  INLINE bool get_clear_depth_active() const;

  INLINE void set_clear_stencil_active(bool clear_stencil_active);
  INLINE bool get_clear_stencil_active() const;

  INLINE void set_clear_color(const Colorf &color);
  INLINE const Colorf &get_clear_color() const;

  INLINE void set_clear_depth(float depth);
  INLINE float get_clear_depth() const;

  INLINE void set_clear_stencil(unsigned int stencil);
  INLINE unsigned int get_clear_stencil() const;

  INLINE void set_clear_active(int n, bool clear_aux_active);
  INLINE bool get_clear_active(int n) const;

  INLINE void set_clear_value(int n, const Colorf &color);
  INLINE const Colorf &get_clear_value(int n) const;
  
  INLINE void disable_clears();

  INLINE bool is_any_clear_active() const;

public:
  INLINE int get_screenshot_buffer_type() const;
  INLINE int get_draw_buffer_type() const;

protected:
  int _screenshot_buffer_type;
  int _draw_buffer_type;

private:
  bool    _clear_active[RTP_COUNT];
  Colorf  _clear_value[RTP_COUNT];
};


#include "drawableRegion.I"

#endif
