// Filename: dynamicTextPage.h
// Created by:  drose (09Feb02)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) 2001, Disney Enterprises, Inc.  All rights reserved
//
// All use of this software is subject to the terms of the Panda 3d
// Software license.  You should have received a copy of this license
// along with this source code; you will also find a current copy of
// the license at http://www.panda3d.org/license.txt .
//
// To contact the maintainers of this program write to
// panda3d@yahoogroups.com .
//
////////////////////////////////////////////////////////////////////

#ifndef DYNAMICTEXTPAGE_H
#define DYNAMICTEXTPAGE_H

#include "pandabase.h"

#ifdef HAVE_FREETYPE

#include "texture.h"
#include "dynamicTextGlyph.h"
#include "pointerTo.h"
#include "pvector.h"

class DynamicTextFont;

////////////////////////////////////////////////////////////////////
//       Class : DynamicTextPage
// Description : A single "page" of a DynamicTextFont.  This is a
//               single texture that holds a number of glyphs for
//               rendering.  The font starts out with one page, and
//               will add more as it needs them.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA DynamicTextPage : public Texture {
public:
  DynamicTextPage(DynamicTextFont *font);

  DynamicTextGlyph *slot_glyph(int x_size, int y_size, int margin);

  INLINE int get_x_size() const;
  INLINE int get_y_size() const;

PUBLISHED:
  INLINE bool is_empty() const;

private:
  bool find_hole(int &x, int &y, int x_size, int y_size) const;
  DynamicTextGlyph *find_overlap(int x, int y, int x_size, int y_size) const;

  typedef pvector< PT(DynamicTextGlyph) > Glyphs;
  Glyphs _glyphs;

  int _x_size, _y_size;

  DynamicTextFont *_font;

public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    Texture::init_type();
    register_type(_type_handle, "DynamicTextPage",
                  Texture::get_class_type());
  }
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}

private:
  static TypeHandle _type_handle;
};

#include "dynamicTextPage.I"

#endif  // HAVE_FREETYPE

#endif
