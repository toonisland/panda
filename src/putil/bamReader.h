// Filename: bamReader.h
// Created by:  jason (12Jun00)
//

#ifndef __BAM_READER_
#define __BAM_READER_

#include <pandabase.h>
#include <notify.h>

#include "typedWritable.h"
#include "datagramGenerator.h"
#include "datagramIterator.h"
#include "bamReaderParam.h"
#include "factory.h"
#include "vector_ushort.h"

#include <algorithm>


// A handy macro for reading PointerToArrays.
#define READ_PTA(Manager, source, Read_func, array)   \
{                                                     \
  void *t;                                            \
  if ((t = Manager->get_pta(source)) == (void*)NULL)  \
  {                                                   \
    array = Read_func(source);                        \
    Manager->register_pta(array.get_void_ptr());      \
  }                                                   \
  else                                                \
  {                                                   \
    array.set_void_ptr(t);                            \
  }                                                   \
}

////////////////////////////////////////////////////////////////////
// 	 Class : BamReader
// Description : This is the fundamental interface for extracting
//               binary objects from a Bam file, as generated by a
//               BamWriter.
//
//               A Bam file can be thought of as a linear collection
//               of objects.  Each object is an instance of a class
//               that inherits, directly or indirectly, from
//               TypedWritable.  The objects may include pointers to
//               other objects within the Bam file; the BamReader
//               automatically manages these (with help from code
//               within each class) and restores the pointers
//               correctly.
//
//               This is the abstract interface and does not
//               specifically deal with disk files, but rather with a
//               DatagramGenerator of some kind, which is simply a
//               linear source of Datagrams.  It is probably from a
//               disk file, but it might conceivably be streamed
//               directly from a network or some such nonsense.
//
//               Bam files are most often used to store scene graphs
//               or subgraphs, and by convention they are given
//               filenames ending in the extension ".bam" when they
//               are used for this purpose.  However, a Bam file may
//               store any arbitrary list of TypedWritable objects;
//               in this more general usage, they are given filenames
//               ending in ".boo" to differentiate them from the more
//               common scene graph files.
//
//               See also BamFile, which defines a higher-level
//               interface to read and write Bam files on disk.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA BamReader {
public:
  typedef Factory<TypedWritable> WritableFactory;
  static BamReader *const Null;
  static WritableFactory *const NullFactory;

  // The primary interface for a caller.

  BamReader(DatagramGenerator *generator);
  ~BamReader();

  bool init();
  TypedWritable *read_object();
  INLINE bool is_eof() const;
  bool resolve();

  INLINE int get_file_major_ver() const;
  INLINE int get_file_minor_ver() const;

  INLINE int get_current_major_ver() const;
  INLINE int get_current_minor_ver() const;

public:
  // Functions to support classes that read themselves from the Bam.

  void read_pointer(DatagramIterator &scan, TypedWritable *for_whom);
  void read_pointers(DatagramIterator &scan, TypedWritable *for_whom, int count);
  void skip_pointer(DatagramIterator &scan);

  void register_finalize(TypedWritable *whom);

  void finalize_now(TypedWritable *whom);

  void *get_pta(DatagramIterator &scan);
  void register_pta(void *ptr);

  TypeHandle read_handle(DatagramIterator &scan);
  

public:
  INLINE static WritableFactory *get_factory();
private:
  INLINE static void create_factory();

private:
  int p_read_object();
  void finalize();

private:
  static WritableFactory *_factory;

  DatagramGenerator *_source;

  // This maps the type index numbers encountered within the Bam file
  // to actual TypeHandles.
  typedef map<int, TypeHandle> IndexMap;
  IndexMap _index_map;

  // This maps the object ID numbers encountered within the Bam file
  // to the actual pointers of the corresponding generated objects.
  typedef map<int, TypedWritable *> CreatedObjs;
  CreatedObjs _created_objs;

  // This records all the objects that still need their pointers
  // completed, along with the object ID's of the pointers they need,
  // in the order in which read_pointer() was called.
  typedef map<TypedWritable *, vector_ushort> Requests;
  Requests _deferred_pointers;

  // This is the number of extra objects that must still be read (and
  // saved in the _created_objs map) before returning from
  // read_object().
  int _num_extra_objects;

  // This is the set of all objects that registered themselves for
  // finalization.
  typedef set<TypedWritable *> Finalize;
  Finalize _finalize_list;

  // These are used by get_pta() and register_pta() to unify multiple
  // references to the same PointerToArray.
  typedef map<int, void *> PTAMap;
  PTAMap _pta_map;
  int _pta_id;

  int _file_major, _file_minor;
  static const int _cur_major;
  static const int _cur_minor;
};

typedef BamReader::WritableFactory WritableFactory;

// Useful function for taking apart the Factory Params in the static
// functions that need to be defined in each writable class that will
// be generated by a factory.  Sets the DatagramIterator and the
// BamReader pointers.
INLINE void
parse_params(const FactoryParams &params,
	     DatagramIterator &scan, BamReader *&manager);

#include "bamReader.I"

#endif
