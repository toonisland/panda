// Filename: qpcamera.cxx
// Created by:  drose (26Feb02)
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

#include "pandabase.h"
#include "qpcamera.h"
#include "lens.h"
#include "throw_event.h"

TypeHandle qpCamera::_type_handle;

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::Constructor
//       Access: Published
//  Description:
////////////////////////////////////////////////////////////////////
qpCamera::
qpCamera(const string &name) :
  qpLensNode(name),
  _active(true),
  _scene((PandaNode *)NULL)
{
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::Copy Constructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
qpCamera::
qpCamera(const qpCamera &copy) :
  qpLensNode(copy),
  _active(copy._active),
  _scene(copy._scene)
{
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::Copy Assignment Operator
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
void qpCamera::
operator = (const qpCamera &copy) {
  qpLensNode::operator = (copy);
  _active = copy._active;
  _scene = copy._scene;
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::Destructor
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
qpCamera::
~qpCamera() {
  // We don't have to destroy the display region(s) associated with
  // the camera; they're responsible for themselves.  However, they
  // should have removed themselves before we destruct, or something
  // went wrong.
  nassertv(_display_regions.empty());
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::make_copy
//       Access: Public, Virtual
//  Description: Returns a newly-allocated Node that is a shallow copy
//               of this one.  It will be a different Node pointer,
//               but its internal data may or may not be shared with
//               that of the original Node.
////////////////////////////////////////////////////////////////////
PandaNode *qpCamera::
make_copy() const {
  return new qpCamera(*this);
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::safe_to_flatten
//       Access: Public, Virtual
//  Description: Returns true if it is generally safe to flatten out
//               this particular kind of Node by duplicating
//               instances, false otherwise (for instance, a qpCamera
//               cannot be safely flattened, because the qpCamera
//               pointer itself is meaningful).
////////////////////////////////////////////////////////////////////
bool qpCamera::
safe_to_flatten() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::safe_to_transform
//       Access: Public, Virtual
//  Description: Returns true if it is generally safe to transform
//               this particular kind of Node by calling the xform()
//               method, false otherwise.  For instance, it's usually
//               a bad idea to attempt to xform a Character.
////////////////////////////////////////////////////////////////////
bool qpCamera::
safe_to_transform() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::add_display_region
//       Access: Private
//  Description: Adds the indicated DisplayRegion to the set of
//               DisplayRegions shared by the camera.  This is only
//               intended to be called from the DisplayRegion.
////////////////////////////////////////////////////////////////////
void qpCamera::
add_display_region(DisplayRegion *display_region) {
  _display_regions.push_back(display_region);
}

////////////////////////////////////////////////////////////////////
//     Function: qpCamera::remove_display_region
//       Access: Private
//  Description: Removes the indicated DisplayRegion from the set of
//               DisplayRegions shared by the camera.  This is only
//               intended to be called from the DisplayRegion.
////////////////////////////////////////////////////////////////////
void qpCamera::
remove_display_region(DisplayRegion *display_region) {
  DisplayRegions::iterator dri =
    find(_display_regions.begin(), _display_regions.end(), display_region);
  if (dri != _display_regions.end()) {
    _display_regions.erase(dri);
  }
}
