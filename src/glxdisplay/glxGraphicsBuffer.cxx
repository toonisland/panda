// Filename: glxGraphicsBuffer.cxx
// Created by:  drose (09Feb04)
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

#include "glxGraphicsBuffer.h"
#include "glxGraphicsStateGuardian.h"
#include "config_glxdisplay.h"
#include "glxGraphicsPipe.h"

#include "graphicsPipe.h"
#include "glgsg.h"

TypeHandle glxGraphicsBuffer::_type_handle;

////////////////////////////////////////////////////////////////////
//     Function: glxGraphicsBuffer::Constructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
glxGraphicsBuffer::
glxGraphicsBuffer(GraphicsPipe *pipe, GraphicsStateGuardian *gsg,
                  int x_size, int y_size, bool want_texture) :
  GraphicsBuffer(pipe, gsg, x_size, y_size, want_texture) 
{
  glxGraphicsPipe *glx_pipe;
  DCAST_INTO_V(glx_pipe, _pipe);
  _display = glx_pipe->get_display();
  _pbuffer = None;
}

////////////////////////////////////////////////////////////////////
//     Function: glxGraphicsBuffer::Destructor
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
glxGraphicsBuffer::
~glxGraphicsBuffer() {
  nassertv(_pbuffer == None);
}

////////////////////////////////////////////////////////////////////
//     Function: glxGraphicsBuffer::make_current
//       Access: Public, Virtual
//  Description: This function will be called within the draw thread
//               during begin_frame() to ensure the graphics context
//               is ready for drawing.
////////////////////////////////////////////////////////////////////
void glxGraphicsBuffer::
make_current() {
  glxGraphicsStateGuardian *glxgsg;
  DCAST_INTO_V(glxgsg, _gsg);
  glXMakeCurrent(_display, _pbuffer, glxgsg->_context);

  // Now that we have made the context current to a window, we can
  // reset the GSG state if this is the first time it has been used.
  // (We can't just call reset() when we construct the GSG, because
  // reset() requires having a current context.)
  glxgsg->reset_if_new();
}

////////////////////////////////////////////////////////////////////
//     Function: glxGraphicsBuffer::release_gsg
//       Access: Public
//  Description: Releases the current GSG pointer, if it is currently
//               held, and resets the GSG to NULL.  The window will be
//               permanently unable to render; this is normally called
//               only just before destroying the window.  This should
//               only be called from within the draw thread.
////////////////////////////////////////////////////////////////////
void glxGraphicsBuffer::
release_gsg() {
  glXMakeCurrent(_display, None, NULL);
  GraphicsBuffer::release_gsg();
}

////////////////////////////////////////////////////////////////////
//     Function: glxGraphicsBuffer::begin_flip
//       Access: Public, Virtual
//  Description: This function will be called within the draw thread
//               after end_frame() has been called on all windows, to
//               initiate the exchange of the front and back buffers.
//
//               This should instruct the window to prepare for the
//               flip at the next video sync, but it should not wait.
//
//               We have the two separate functions, begin_flip() and
//               end_flip(), to make it easier to flip all of the
//               windows at the same time.
////////////////////////////////////////////////////////////////////
void glxGraphicsBuffer::
begin_flip() {
  if (_gsg != (GraphicsStateGuardian *)NULL) {
    make_current();

    if (has_texture()) {
      // Use glCopyTexImage2D to copy the framebuffer to the texture.
      // This appears to be the only way to "render to a texture" in
      // OpenGL; there's no interface to make the offscreen buffer
      // itself be a texture.
      DisplayRegion dr(_x_size, _y_size);
      get_texture()->copy(_gsg, &dr, _gsg->get_render_buffer(RenderBuffer::T_back));
    }

    glXSwapBuffers(_display, _pbuffer);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: glxGraphicsBuffer::close_buffer
//       Access: Protected, Virtual
//  Description: Closes the buffer right now.  Called from the window
//               thread.
////////////////////////////////////////////////////////////////////
void glxGraphicsBuffer::
close_buffer() {
  if (_pbuffer != None) {
    glXDestroyPbuffer(_display, _pbuffer);
    _pbuffer = None;
  }

  _is_valid = false;
}

////////////////////////////////////////////////////////////////////
//     Function: glxGraphicsBuffer::open_buffer
//       Access: Protected, Virtual
//  Description: Opens the buffer right now.  Called from the window
//               thread.  Returns true if the buffer is successfully
//               opened, or false if there was a problem.
////////////////////////////////////////////////////////////////////
bool glxGraphicsBuffer::
open_buffer() {
  glxGraphicsPipe *glx_pipe;
  DCAST_INTO_R(glx_pipe, _pipe, false);
  glxGraphicsStateGuardian *glxgsg;
  DCAST_INTO_R(glxgsg, _gsg, false);

  static const int max_attrib_list = 32;
  int attrib_list[max_attrib_list];
  int n=0;

#ifdef GLX_VERSION_1_3
  // The official GLX 1.3 version passes in the size in the attrib
  // list.
  attrib_list[n++] = GLX_PBUFFER_WIDTH;
  attrib_list[n++] = _x_size;
  attrib_list[n++] = GLX_PBUFFER_HEIGHT;
  attrib_list[n++] = _y_size;

  nassertr(n < max_attrib_list, false);
  attrib_list[n] = (int)None;
  _pbuffer = glXCreatePbuffer(glxgsg->_display, glxgsg->_fbconfig,
                              attrib_list);

#else
  // The pre-GLX 1.3 version passed in the size in the parameter list.
  nassertr(n < max_attrib_list, false);
  attrib_list[n] = (int)None;
  _pbuffer = glXCreateGLXPbufferSGIX(glxgsg->_display, glxgsg->_fbconfig,
                                     _x_size, _y_size, attrib_list);
#endif

  if (_pbuffer == None) {
    glxdisplay_cat.error()
      << "failed to create GLX pbuffer.\n";
    return false;
  }

  _is_valid = true;
  return true;
}
