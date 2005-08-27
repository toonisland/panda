// Filename: pgButtonNotify.cxx
// Created by:  drose (18Aug05)
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

#include "pgButtonNotify.h"
#include "pgButton.h"

////////////////////////////////////////////////////////////////////
//     Function: PGButtonNotify::button_click
//       Access: Protected, Virtual
//  Description: Called whenever a watched PGButton has been clicked.
////////////////////////////////////////////////////////////////////
void PGButtonNotify::
button_click(PGButton *, const MouseWatcherParameter &) {
}
