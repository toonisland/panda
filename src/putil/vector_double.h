// Filename: vector_double.h
// Created by:  drose (10May00)
// 
////////////////////////////////////////////////////////////////////

#ifndef VECTOR_DOUBLE_H
#define VECTOR_DOUBLE_H

#include <pandabase.h>

#include <vector>

////////////////////////////////////////////////////////////////////
//       Class : vector_double
// Description : A vector of doubles.  This class is defined once here,
//               and exported to PANDA.DLL; other packages that want
//               to use a vector of this type (whether they need to
//               export it or not) should include this header file,
//               rather than defining the vector again.
////////////////////////////////////////////////////////////////////

#ifdef HAVE_DINKUM
#define VV_DOUBLE std::_Vector_val<double, std::allocator<double> >
EXPORT_TEMPLATE_CLASS(EXPCL_PANDA, EXPTP_PANDA, VV_DOUBLE)
#endif
EXPORT_TEMPLATE_CLASS(EXPCL_PANDA, EXPTP_PANDA, std::vector<double>)
typedef vector<double> vector_double;

// Tell GCC that we'll take care of the instantiation explicitly here.
#ifdef __GNUC__
#pragma interface
#endif

#endif
