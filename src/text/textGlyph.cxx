// Filename: textGlyph.cxx
// Created by:  drose (08Feb02)
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

#include "textGlyph.h"

////////////////////////////////////////////////////////////////////
//     Function: TextGlyph::Destructor
//       Access: Public, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
TextGlyph::
~TextGlyph() {
}

////////////////////////////////////////////////////////////////////
//     Function: TextGlyph::get_geom
//       Access: Public, Virtual
//  Description: Returns a Geom that renders the particular glyph.
////////////////////////////////////////////////////////////////////
PT(Geom) TextGlyph::
get_geom() const {
  return _geom;
}
