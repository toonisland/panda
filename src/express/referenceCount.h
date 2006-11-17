// Filename: referenceCount.h
// Created by:  drose (23Oct98)
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

#ifndef REFERENCECOUNT_H
#define REFERENCECOUNT_H

#include "pandabase.h"
#include "weakReferenceList.h"
#include "typedObject.h"
#include "memoryUsage.h"
#include "memoryBase.h"
#include "config_express.h"
#include "atomicAdjust.h"
#include "numeric_types.h"
#include "deletedChain.h"
#include "objectDeletor.h"

#include <stdlib.h>

#ifdef HAVE_RTTI
#include <typeinfo>
#endif

////////////////////////////////////////////////////////////////////
//       Class : ReferenceCount
// Description : A base class for all things that want to be
//               reference-counted.  ReferenceCount works in
//               conjunction with PointerTo to automatically delete
//               objects when the last pointer to them goes away.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDAEXPRESS ReferenceCount : public MemoryBase {
protected:
  INLINE ReferenceCount();
  INLINE ReferenceCount(const ReferenceCount &);
  INLINE void operator = (const ReferenceCount &);
  INLINE ~ReferenceCount();

PUBLISHED:
  INLINE int get_ref_count() const;
  INLINE void ref() const;
  INLINE bool unref() const;

  INLINE bool test_ref_count_integrity() const;
  INLINE bool test_ref_count_nonzero() const;

public:
  INLINE void local_object();
  INLINE bool has_weak_list() const;
  INLINE WeakReferenceList *get_weak_list() const;

  INLINE void weak_ref(WeakPointerToVoid *ptv);
  INLINE void weak_unref(WeakPointerToVoid *ptv);

protected:
  bool do_test_ref_count_integrity() const;
  bool do_test_ref_count_nonzero() const;

private:
  void create_weak_list();

private:
  enum { 
    // We use this value as a flag to indicate an object has been
    // indicated as a local object, and should not be deleted except
    // by its own destructor.  Really, any nonzero value would do, but
    // having a large specific number makes the sanity checks easier.
    local_ref_count = 10000000,

    // This value is used as a flag to indicate that an object has
    // just been deleted, and you're looking at deallocated memory.
    // It's not guaranteed to stick around, of course (since the
    // deleted memory might be repurposed for anything else, including
    // a new object), but if you ever do encounter this value in a
    // reference count field, you screwed up.
    deleted_ref_count = -100,
  };

  PN_int32 _ref_count;
  WeakReferenceList *_weak_list;

public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    register_type(_type_handle, "ReferenceCount");
  }

private:
  static TypeHandle _type_handle;
};

template<class ObjectType>
class DeleteWrapper {
public:
  static void do_delete(void *ptr);
};

template<class RefCountType>
class RefCountDeleteWrapper {
public:
  static void do_delete(void *ptr);
};

template<class RefCountType>
INLINE void unref_delete(RefCountType *ptr);

template<class ObjectType>
INLINE void defer_delete(ObjectType *ptr);

////////////////////////////////////////////////////////////////////
//       Class : RefCountProxy
// Description : A "proxy" to use to make a reference-countable object
//               whenever the object cannot inherit from
//               ReferenceCount for some reason.  RefCountPr<MyClass>
//               can be treated as an instance of MyClass directly,
//               for the most part, except that it can be reference
//               counted.
//
//               If you want to declare a RefCountProxy to something
//               that does not have get_class_type(), you will have to
//               define a template specialization on
//               _get_type_handle() and _do_init_type(), as in
//               typedObject.h.
////////////////////////////////////////////////////////////////////
template<class Base>
class RefCountProxy : public ReferenceCount {
public:
  INLINE RefCountProxy();
  INLINE RefCountProxy(const Base &copy);

  INLINE operator Base &();
  INLINE operator const Base &() const;

  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type();

private:
  Base _base;
  static TypeHandle _type_handle;
};


////////////////////////////////////////////////////////////////////
//       Class : RefCountObj
// Description : Another kind of proxy, similar to RefCountProxy.
//               This one works by inheriting from the indicated base
//               type, giving it an is-a relation instead of a has-a
//               relation.  As such, it's a little more robust, but
//               only works when the base type is, in fact, a class.
////////////////////////////////////////////////////////////////////
template<class Base>
class RefCountObj : public ReferenceCount, public Base {
public:
  INLINE RefCountObj();
  INLINE RefCountObj(const Base &copy);
  ALLOC_DELETED_CHAIN(RefCountObj<Base>);

  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type();

private:
  static TypeHandle _type_handle;
};

#include "referenceCount.I"

#endif
