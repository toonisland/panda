// Filename: mesaGraphicsPipe.h
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

#ifndef MESAGRAPHICSPIPE_H
#define MESAGRAPHICSPIPE_H

#include "pandabase.h"
#include "graphicsWindow.h"
#include "graphicsPipe.h"

#include <GL/osmesa.h>

class FrameBufferProperties;

////////////////////////////////////////////////////////////////////
//       Class : MesaGraphicsPipe
// Description : This graphics pipe represents the interface for
//               rendering with direct calls to the Mesa open-source
//               software-only implementation of OpenGL.
//
//               Raw Mesa supports only offscreen buffers, but it's
//               possible to create and render into these offscreen
//               buffers without having any X server or other
//               operating system infrastructure in place.
////////////////////////////////////////////////////////////////////
class MesaGraphicsPipe : public GraphicsPipe {
public:
  MesaGraphicsPipe();
  virtual ~MesaGraphicsPipe();

  virtual string get_interface_name() const;
  static PT(GraphicsPipe) pipe_constructor();

protected:
  virtual PT(GraphicsStateGuardian) make_gsg(const FrameBufferProperties &properties);
  virtual PT(GraphicsBuffer) make_buffer(GraphicsStateGuardian *gsg, 
                                         int x_size, int y_size, bool want_texture);

private:

public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    GraphicsPipe::init_type();
    register_type(_type_handle, "MesaGraphicsPipe",
                  GraphicsPipe::get_class_type());
  }
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}

private:
  static TypeHandle _type_handle;
};

#include "mesaGraphicsPipe.I"

#endif
