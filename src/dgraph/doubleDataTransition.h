// Filename: doubleDataTransition.h
// Created by:  drose (27Mar00)
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

#ifndef DOUBLEDATATRANSITION_H
#define DOUBLEDATATRANSITION_H

#include <pandabase.h>

#include "numericDataTransition.h"

EXPORT_TEMPLATE_CLASS(EXPCL_PANDA, EXPTP_PANDA, NumericDataTransition<double>);

////////////////////////////////////////////////////////////////////
//       Class : DoubleDataTransition
// Description : A NumericDataTransition templated on double types.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA DoubleDataTransition :
  public NumericDataTransition<double> {
public:
  INLINE DoubleDataTransition(double value = 0.0);

  virtual NodeTransition *make_copy() const;

public:
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    NumericDataTransition<double>::init_type();
    register_type(_type_handle, "DoubleDataTransition",
                  NumericDataTransition<double>::get_class_type());
  }

private:
  static TypeHandle _type_handle;
};

#include "doubleDataTransition.I"

// Tell GCC that we'll take care of the instantiation explicitly here.
#ifdef __GNUC__
#pragma interface
#endif

#endif
