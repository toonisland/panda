// Filename: windowProperties.h
// Created by:  drose (13Aug02)
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

#ifndef WINDOWPROPERTIES_H
#define WINDOWPROPERTIES_H

#include "pandabase.h"

////////////////////////////////////////////////////////////////////
//       Class : WindowProperties
// Description : A container for the various kinds of properties we
//               might ask to have on a graphics window before we open
//               it.  This also serves to hold the current properties
//               for a window after it has been opened.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA WindowProperties {
PUBLISHED:
  WindowProperties();
  INLINE WindowProperties(const WindowProperties &copy);
  void operator = (const WindowProperties &copy);
  INLINE ~WindowProperties();

  bool operator == (const WindowProperties &other) const;
  INLINE bool operator != (const WindowProperties &other) const;

  void clear();
  INLINE bool is_any_specified() const;
  
  INLINE void set_origin(int x_origin, int y_origin);
  INLINE int get_x_origin() const;
  INLINE int get_y_origin() const;
  INLINE bool has_origin() const;
  INLINE void clear_origin();

  INLINE void set_size(int x_size, int y_size);
  INLINE int get_x_size() const;
  INLINE int get_y_size() const;
  INLINE bool has_size() const;
  INLINE void clear_size();

  INLINE void set_title(const string &title);
  INLINE const string &get_title() const;
  INLINE bool has_title() const;
  INLINE void clear_title();

  INLINE void set_undecorated(bool undecorated);
  INLINE bool get_undecorated() const;
  INLINE bool has_undecorated() const;
  INLINE void clear_undecorated();

  INLINE void set_fullscreen(bool fullscreen);
  INLINE bool get_fullscreen() const;
  INLINE bool has_fullscreen() const;
  INLINE void clear_fullscreen();

  INLINE void set_foreground(bool foreground);
  INLINE bool get_foreground() const;
  INLINE bool has_foreground() const;
  INLINE void clear_foreground();

  INLINE void set_minimized(bool minimized);
  INLINE bool get_minimized() const;
  INLINE bool has_minimized() const;
  INLINE void clear_minimized();

  INLINE void set_open(bool open);
  INLINE bool get_open() const;
  INLINE bool has_open() const;
  INLINE void clear_open();

  INLINE void set_cursor_hidden(bool cursor_hidden);
  INLINE bool get_cursor_hidden() const;
  INLINE bool has_cursor_hidden() const;
  INLINE void clear_cursor_hidden();

  void add_properties(const WindowProperties &other);

  void output(ostream &out) const;
  
private:
  // This bitmask indicates which of the parameters in the properties
  // structure have been filled in by the user, and which remain
  // unspecified.
  enum Specified {
    S_origin           = 0x0001,
    S_size             = 0x0002,
    S_title            = 0x0004,
    S_undecorated      = 0x0008,
    S_fullscreen       = 0x0010,
    S_foreground       = 0x0020,
    S_minimized        = 0x0040,
    S_open             = 0x0080,
    S_cursor_hidden    = 0x0100,
  };

  // This bitmask represents the true/false settings for various
  // boolean flags (assuming the corresponding S_* bit has been set,
  // above).
  enum Flags {
    F_undecorated    = S_undecorated,
    F_fullscreen     = S_fullscreen,
    F_foreground     = S_foreground,
    F_minimized      = S_minimized,
    F_open           = S_open,
    F_cursor_hidden  = S_cursor_hidden,
  };

  int _specified;
  int _x_origin;
  int _y_origin;
  int _x_size;
  int _y_size;
  string _title;
  int _flags;
};

INLINE ostream &operator << (ostream &out, const WindowProperties &properties);

#include "windowProperties.I"

#endif
