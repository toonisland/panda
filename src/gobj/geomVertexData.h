// Filename: geomVertexData.h
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

#ifndef GEOMVERTEXDATA_H
#define GEOMVERTEXDATA_H

#include "pandabase.h"
#include "typedWritableReferenceCount.h"
#include "geomVertexFormat.h"
#include "geomVertexColumn.h"
#include "geomVertexArrayData.h"
#include "geomEnums.h"
#include "geomCacheEntry.h"
#include "transformTable.h"
#include "transformBlendTable.h"
#include "sliderTable.h"
#include "internalName.h"
#include "cycleData.h"
#include "cycleDataReader.h"
#include "cycleDataWriter.h"
#include "cycleDataStageReader.h"
#include "cycleDataStageWriter.h"
#include "pipelineCycler.h"
#include "pStatCollector.h"
#include "pointerTo.h"
#include "pmap.h"
#include "pvector.h"
#include "deletedChain.h"

class FactoryParams;
class GeomVertexColumn;

////////////////////////////////////////////////////////////////////
//       Class : GeomVertexData
// Description : This defines the actual numeric vertex data stored in
//               a Geom, in the structure defined by a particular
//               GeomVertexFormat object.
//
//               The data consists of one or more arrays, each of
//               which in turn consists of a series of rows, one per
//               vertex.  All arrays should have the same number of
//               rows; each vertex is defined by the column data from
//               a particular row across all arrays.
//
//               Often, there will be only one array per Geom, and the
//               various columns defined in the GeomVertexFormat will
//               be interleaved within that array.  However, it is
//               also possible to have multiple different arrays, with
//               a certain subset of the total columns defined in each
//               array.
//
//               However the data is distributed, the effect is of a
//               single table of vertices, where each vertex is
//               represented by one row of the table.
//
//               In general, application code should not attempt to
//               directly manipulate the vertex data through this
//               structure; instead, use the GeomVertexReader,
//               GeomVertexWriter, and GeomVertexRewriter objects to
//               read and write vertex data at a high level.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA GeomVertexData : public TypedWritableReferenceCount, public GeomEnums {
private:
  GeomVertexData();
PUBLISHED:
  GeomVertexData(const string &name,
                 const GeomVertexFormat *format, 
                 UsageHint usage_hint);
  GeomVertexData(const GeomVertexData &copy);
  GeomVertexData(const GeomVertexData &copy,
                 const GeomVertexFormat *format);
  void operator = (const GeomVertexData &copy);
  virtual ~GeomVertexData();
  ALLOC_DELETED_CHAIN(GeomVertexData);

  INLINE const string &get_name() const;
  void set_name(const string &name);

  INLINE UsageHint get_usage_hint() const;
  void set_usage_hint(UsageHint usage_hint);

  INLINE const GeomVertexFormat *get_format() const;
  void set_format(const GeomVertexFormat *format);

  INLINE bool has_column(const InternalName *name) const;

  INLINE int get_num_rows() const;
  INLINE bool set_num_rows(int n);
  void clear_rows();

  INLINE int get_num_arrays() const;
  INLINE const GeomVertexArrayData *get_array(int i) const;
  INLINE GeomVertexArrayData *modify_array(int i);
  INLINE void set_array(int i, const GeomVertexArrayData *array);

  INLINE const TransformTable *get_transform_table() const;
  void set_transform_table(const TransformTable *table);
  INLINE void clear_transform_table();

  INLINE const TransformBlendTable *get_transform_blend_table() const;
  TransformBlendTable *modify_transform_blend_table();
  void set_transform_blend_table(const TransformBlendTable *table);
  INLINE void clear_transform_blend_table();

  INLINE const SliderTable *get_slider_table() const;
  void set_slider_table(const SliderTable *table);
  INLINE void clear_slider_table();

  INLINE int get_num_bytes() const;
  INLINE UpdateSeq get_modified(Thread *current_thread = Thread::get_current_thread()) const;

  void copy_from(const GeomVertexData *source, bool keep_data_objects,
                 Thread *current_thread = Thread::get_current_thread());
  void copy_row_from(int dest_row, const GeomVertexData *source, 
                     int source_row, Thread *current_thread);
  CPT(GeomVertexData) convert_to(const GeomVertexFormat *new_format) const;
  CPT(GeomVertexData) 
    scale_color(const LVecBase4f &color_scale) const;
  CPT(GeomVertexData) 
    scale_color(const LVecBase4f &color_scale, int num_components,
                NumericType numeric_type, Contents contents) const;
  CPT(GeomVertexData) 
    set_color(const Colorf &color) const;
  CPT(GeomVertexData) 
    set_color(const Colorf &color, int num_components,
              NumericType numeric_type, Contents contents) const;

  CPT(GeomVertexData) animate_vertices(Thread *current_thread) const;

  PT(GeomVertexData) 
    replace_column(InternalName *name, int num_components,
                   NumericType numeric_type, Contents contents) const;

  void output(ostream &out) const;
  void write(ostream &out, int indent_level = 0) const;

  void clear_cache();
  void clear_cache_stage();

public:
  static INLINE PN_uint32 pack_abcd(unsigned int a, unsigned int b,
                                    unsigned int c, unsigned int d);
  static INLINE unsigned int unpack_abcd_a(PN_uint32 data);
  static INLINE unsigned int unpack_abcd_b(PN_uint32 data);
  static INLINE unsigned int unpack_abcd_c(PN_uint32 data);
  static INLINE unsigned int unpack_abcd_d(PN_uint32 data);

private:
  static void bytewise_copy(unsigned char *to, int to_stride,
                            const unsigned char *from, int from_stride,
                            const GeomVertexColumn *from_type,
                            int num_records);
  static void
  packed_argb_to_uint8_rgba(unsigned char *to, int to_stride,
                            const unsigned char *from, int from_stride,
                            int num_records);
  static void
  uint8_rgba_to_packed_argb(unsigned char *to, int to_stride,
                            const unsigned char *from, int from_stride,
                            int num_records);

  typedef pmap<const VertexTransform *, int> TransformMap;
  INLINE static int 
  add_transform(TransformTable *table, const VertexTransform *transform,
                TransformMap &already_added);

private:
  string _name;

  typedef pvector< PT(GeomVertexArrayData) > Arrays;

  // The pipelined data with each CacheEntry.
  class CDataCache : public CycleData {
  public:
    INLINE CDataCache();
    INLINE CDataCache(const CDataCache &copy);
    ALLOC_DELETED_CHAIN(CDataCache);
    virtual CycleData *make_copy() const;
    virtual TypeHandle get_parent_type() const {
      return GeomVertexData::get_class_type();
    }

    CPT(GeomVertexData) _result;
  };
  typedef CycleDataReader<CDataCache> CDCacheReader;
  typedef CycleDataWriter<CDataCache> CDCacheWriter;

public:
  // The CacheKey class separates out just the part of CacheEntry that
  // is used to key the cache entry within the map.  We have this as a
  // separate class so we can easily look up a new entry in the map,
  // without having to execute the relatively expensive CacheEntry
  // constructor.
  class CacheKey {
  public:
    INLINE CacheKey(const GeomVertexFormat *modifier);
    INLINE bool operator < (const CacheKey &other) const;

    CPT(GeomVertexFormat) _modifier;
  };
  // It is not clear why MSVC7 needs this class to be public.  
  class CacheEntry : public GeomCacheEntry {
  public:
    INLINE CacheEntry(GeomVertexData *source,
                      const GeomVertexFormat *modifier);
    ALLOC_DELETED_CHAIN(CacheEntry);

    virtual void evict_callback();
    virtual void output(ostream &out) const;

    GeomVertexData *_source;  // A back pointer to the containing data.
    CacheKey _key;

    PipelineCycler<CDataCache> _cycler;
  };
  typedef pmap<const CacheKey *, PT(CacheEntry), IndirectLess<CacheKey> > Cache;

private:
  // This is the data that must be cycled between pipeline stages.
  class EXPCL_PANDA CData : public CycleData {
  public:
    INLINE CData();
    INLINE CData(const CData &copy);
    ALLOC_DELETED_CHAIN(CData);
    virtual CycleData *make_copy() const;
    virtual void write_datagram(BamWriter *manager, Datagram &dg) const;
    virtual int complete_pointers(TypedWritable **plist, BamReader *manager);
    virtual void fillin(DatagramIterator &scan, BamReader *manager);
    virtual TypeHandle get_parent_type() const {
      return GeomVertexData::get_class_type();
    }

    UsageHint _usage_hint;
    CPT(GeomVertexFormat) _format;
    Arrays _arrays;
    CPT(TransformTable) _transform_table;
    PT(TransformBlendTable) _transform_blend_table;
    CPT(SliderTable) _slider_table;
    PT(GeomVertexData) _animated_vertices;
    UpdateSeq _animated_vertices_modified;
    UpdateSeq _modified;
  };

  PipelineCycler<CData> _cycler;
  typedef CycleDataReader<CData> CDReader;
  typedef CycleDataWriter<CData> CDWriter;
  typedef CycleDataStageReader<CData> CDStageReader;
  typedef CycleDataStageWriter<CData> CDStageWriter;

  Cache _cache;
  Mutex _cache_lock;

private:
  void update_animated_vertices(CData *cdata, Thread *current_thread);

  static PStatCollector _convert_pcollector;
  static PStatCollector _scale_color_pcollector;
  static PStatCollector _set_color_pcollector;
  static PStatCollector _animation_pcollector;

  PStatCollector _char_pcollector;
  PStatCollector _skinning_pcollector;
  PStatCollector _morphs_pcollector;

public:
  static void register_with_read_factory();
  virtual void write_datagram(BamWriter *manager, Datagram &dg);
  virtual int complete_pointers(TypedWritable **plist, BamReader *manager);

  virtual void finalize(BamReader *manager);

protected:
  static TypedWritable *make_from_bam(const FactoryParams &params);
  void fillin(DatagramIterator &scan, BamReader *manager);

public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    TypedWritableReferenceCount::init_type();
    register_type(_type_handle, "GeomVertexData",
                  TypedWritableReferenceCount::get_class_type());
  }
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}

private:
  static TypeHandle _type_handle;

  friend class CacheEntry;
  friend class GeomVertexDataPipelineBase;
  friend class GeomVertexDataPipelineReader;
  friend class GeomVertexDataPipelineWriter;
};

////////////////////////////////////////////////////////////////////
//       Class : GeomVertexDataPipelineBase
// Description : The common code from
//               GeomVertexDataPipelineReader and
//               GeomVertexDataPipelineWriter.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA GeomVertexDataPipelineBase : public GeomEnums {
protected:
  INLINE GeomVertexDataPipelineBase(GeomVertexData *object, 
                                    Thread *current_thread,
                                    GeomVertexData::CData *cdata);

public:
  INLINE Thread *get_current_thread() const;

  INLINE const GeomVertexFormat *get_format() const;
  INLINE bool has_column(const InternalName *name) const;

  INLINE UsageHint get_usage_hint() const;
  INLINE int get_num_arrays() const;
  INLINE const GeomVertexArrayData *get_array(int i) const;
  INLINE const TransformTable *get_transform_table() const;
  INLINE const TransformBlendTable *get_transform_blend_table() const;
  INLINE const SliderTable *get_slider_table() const;
  int get_num_bytes() const;
  INLINE UpdateSeq get_modified() const;

protected:
  GeomVertexData *_object;
  Thread *_current_thread;
  GeomVertexData::CData *_cdata;
};

////////////////////////////////////////////////////////////////////
//       Class : GeomVertexDataPipelineReader
// Description : Encapsulates the data from a GeomVertexData,
//               pre-fetched for one stage of the pipeline.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA GeomVertexDataPipelineReader : public GeomVertexDataPipelineBase {
public:
  INLINE GeomVertexDataPipelineReader(const GeomVertexData *object, Thread *current_thread);
private:
  INLINE GeomVertexDataPipelineReader(const GeomVertexDataPipelineReader &copy);
  INLINE void operator = (const GeomVertexDataPipelineReader &copy);

public:
  INLINE ~GeomVertexDataPipelineReader();
  ALLOC_DELETED_CHAIN(GeomVertexDataPipelineReader);

  INLINE const GeomVertexData *get_object() const;

  INLINE void check_array_readers() const;
  INLINE const GeomVertexArrayDataPipelineReader *get_array_reader(int i) const;
  int get_num_rows() const;

  bool get_array_info(const InternalName *name, 
                      const GeomVertexArrayDataPipelineReader *&array_reader,
                      int &num_values, NumericType &numeric_type, 
                      int &start, int &stride) const;

  INLINE bool has_vertex() const;
  INLINE bool is_vertex_transformed() const;
  bool get_vertex_info(const GeomVertexArrayDataPipelineReader *&array_reader,
                       int &num_values, NumericType &numeric_type, 
                       int &start, int &stride) const;

  INLINE bool has_normal() const;
  bool get_normal_info(const GeomVertexArrayDataPipelineReader *&array_reader,
                       NumericType &numeric_type,
                       int &start, int &stride) const;

  INLINE bool has_color() const;
  bool get_color_info(const GeomVertexArrayDataPipelineReader *&array_reader,
                      int &num_values, NumericType &numeric_type, 
                      int &start, int &stride) const;

private:
  void make_array_readers();
  void delete_array_readers();

  bool _got_array_readers;
  typedef pvector<GeomVertexArrayDataPipelineReader *> ArrayReaders;
  ArrayReaders _array_readers;
};

////////////////////////////////////////////////////////////////////
//       Class : GeomVertexDataPipelineWriter
// Description : Encapsulates the data from a GeomVertexData,
//               pre-fetched for one stage of the pipeline.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA GeomVertexDataPipelineWriter : public GeomVertexDataPipelineBase {
public:
  INLINE GeomVertexDataPipelineWriter(GeomVertexData *object, bool force_to_0,
                                      Thread *current_thread);
private:
  INLINE GeomVertexDataPipelineWriter(const GeomVertexDataPipelineWriter &copy);
  INLINE void operator = (const GeomVertexDataPipelineWriter &copy);

public:
  INLINE ~GeomVertexDataPipelineWriter();
  ALLOC_DELETED_CHAIN(GeomVertexDataPipelineWriter);

  INLINE GeomVertexData *get_object() const;

  INLINE void check_array_writers() const;
  INLINE GeomVertexArrayDataPipelineWriter *get_array_writer(int i) const;

  GeomVertexArrayData *modify_array(int i);
  void set_array(int i, const GeomVertexArrayData *array);

  int get_num_rows() const;
  bool set_num_rows(int n);

private:
  void make_array_writers();
  void delete_array_writers();

  bool _force_to_0;
  bool _got_array_writers;
  typedef pvector<GeomVertexArrayDataPipelineWriter *> ArrayWriters;
  ArrayWriters _array_writers;
};

INLINE ostream &operator << (ostream &out, const GeomVertexData &obj);

#include "geomVertexData.I"

#endif
