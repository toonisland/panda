// Filename: qpgeomVertexArrayFormat.h
// Created by:  drose (06Mar05)
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

#ifndef qpGEOMVERTEXARRAYFORMAT_H
#define qpGEOMVERTEXARRAYFORMAT_H

#include "pandabase.h"
#include "typedWritableReferenceCount.h"
#include "qpgeomVertexColumn.h"
#include "pvector.h"
#include "pmap.h"

class qpGeomVertexFormat;
class qpGeomVertexData;
class InternalName;
class FactoryParams;
class BamWriter;
class BamReader;

////////////////////////////////////////////////////////////////////
//       Class : qpGeomVertexArrayFormat
// Description : This describes the structure of a single array within
//               a Geom data.  See GeomVertexFormat for the parent
//               class which collects together all of the individual
//               GeomVertexArrayFormat objects.
//
//               A particular array may include any number of standard
//               or user-defined columns.  All columns consist of a
//               sequence of one or more numeric values, packed in any
//               of a variety of formats; the semantic meaning of each
//               column is defined in general with its contents
//               member, and in particular by its name.  The standard
//               array types used most often are named "vertex",
//               "normal", "texcoord", and "color"; other kinds of
//               data may be piggybacked into the data record simply
//               by choosing a unique name.
//
//               This is part of the experimental Geom rewrite.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA qpGeomVertexArrayFormat : public TypedWritableReferenceCount {
PUBLISHED:
  qpGeomVertexArrayFormat();
  qpGeomVertexArrayFormat(const qpGeomVertexArrayFormat &copy);
  qpGeomVertexArrayFormat(const InternalName *name0, int num_components0,
                          qpGeomVertexColumn::NumericType numeric_type0,
                          qpGeomVertexColumn::Contents contents0);
  qpGeomVertexArrayFormat(const InternalName *name0, int num_components0,
                          qpGeomVertexColumn::NumericType numeric_type0,
                          qpGeomVertexColumn::Contents contents0,
                          const InternalName *name1, int num_components1,
                          qpGeomVertexColumn::NumericType numeric_type1,
                          qpGeomVertexColumn::Contents contents1);
  qpGeomVertexArrayFormat(const InternalName *name0, int num_components0,
                          qpGeomVertexColumn::NumericType numeric_type0,
                          qpGeomVertexColumn::Contents contents0,
                          const InternalName *name1, int num_components1,
                          qpGeomVertexColumn::NumericType numeric_type1,
                          qpGeomVertexColumn::Contents contents1,
                          const InternalName *name2, int num_components2,
                          qpGeomVertexColumn::NumericType numeric_type2,
                          qpGeomVertexColumn::Contents contents2);
  qpGeomVertexArrayFormat(const InternalName *name0, int num_components0,
                          qpGeomVertexColumn::NumericType numeric_type0,
                          qpGeomVertexColumn::Contents contents0,
                          const InternalName *name1, int num_components1,
                          qpGeomVertexColumn::NumericType numeric_type1,
                          qpGeomVertexColumn::Contents contents1,
                          const InternalName *name2, int num_components2,
                          qpGeomVertexColumn::NumericType numeric_type2,
                          qpGeomVertexColumn::Contents contents2,
                          const InternalName *name3, int num_components3,
                          qpGeomVertexColumn::NumericType numeric_type3,
                          qpGeomVertexColumn::Contents contents3);
  void operator = (const qpGeomVertexArrayFormat &copy);
  ~qpGeomVertexArrayFormat();

  INLINE bool is_registered() const;

  INLINE int get_stride() const;
  INLINE void set_stride(int stride);

  INLINE int get_total_bytes() const;
  INLINE int get_pad_to() const;

  int add_column(const InternalName *name, int num_components,
                 qpGeomVertexColumn::NumericType numeric_type,
                 qpGeomVertexColumn::Contents contents,
                 int start = -1);
  int add_column(const qpGeomVertexColumn &column);
  void remove_column(const InternalName *name);
  void clear_columns();

  INLINE int get_num_columns() const;
  INLINE const qpGeomVertexColumn *get_column(int i) const;

  const qpGeomVertexColumn *get_column(const InternalName *name) const;
  const qpGeomVertexColumn *get_column(int start_byte, int num_bytes) const;
  INLINE bool has_column(const InternalName *name) const;

  bool is_data_subset_of(const qpGeomVertexArrayFormat &other) const;

  void output(ostream &out) const;
  void write(ostream &out, int indent_level = 0) const;
  void write_with_data(ostream &out, int indent_level, 
                       const qpGeomVertexData *data, int array_index) const;

public:
  int compare_to(const qpGeomVertexArrayFormat &other) const;

private:
  INLINE void consider_sort_columns() const;
  void sort_columns();
  void do_register();

  bool _is_registered;
  int _stride;
  int _total_bytes;
  int _pad_to;

  typedef pvector<qpGeomVertexColumn *> Columns;
  Columns _columns;
  bool _columns_unsorted;

  typedef pmap<const InternalName *, qpGeomVertexColumn *> ColumnsByName;
  ColumnsByName _columns_by_name;

public:
  static void register_with_read_factory();
  virtual void write_datagram(BamWriter *manager, Datagram &dg);
  virtual int complete_pointers(TypedWritable **plist, BamReader *manager);

protected:
  static TypedWritable *make_from_bam(const FactoryParams &params);
  void fillin(DatagramIterator &scan, BamReader *manager);

public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    TypedWritableReferenceCount::init_type();
    register_type(_type_handle, "qpGeomVertexArrayFormat",
                  TypedWritableReferenceCount::get_class_type());
  }
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}

private:
  static TypeHandle _type_handle;

  friend class qpGeomVertexFormat;
};

INLINE ostream &operator << (ostream &out, const qpGeomVertexArrayFormat &obj);

#include "qpgeomVertexArrayFormat.I"

#endif
