// Filename: renderEffect.cxx
// Created by:  drose (14Mar02)
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

#include "renderEffect.h"
#include "bamReader.h"
#include "indent.h"
#include "config_pgraph.h"

RenderEffect::Effects *RenderEffect::_effects = NULL;
TypeHandle RenderEffect::_type_handle;

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::Constructor
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
RenderEffect::
RenderEffect() {
  if (_effects == (Effects *)NULL) {
    // Make sure the global _effects map is allocated.  This only has
    // to be done once.  We could make this map static, but then we
    // run into problems if anyone creates a RenderState object at
    // static init time; it also seems to cause problems when the
    // Panda shared library is unloaded at application exit time.
    _effects = new Effects;
  }
  _saved_entry = _effects->end();
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::Copy Constructor
//       Access: Private
//  Description: RenderEffects are not meant to be copied.
////////////////////////////////////////////////////////////////////
RenderEffect::
RenderEffect(const RenderEffect &) {
  nassertv(false);
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::Copy Assignment Operator
//       Access: Private
//  Description: RenderEffects are not meant to be copied.
////////////////////////////////////////////////////////////////////
void RenderEffect::
operator = (const RenderEffect &) {
  nassertv(false);
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::Destructor
//       Access: Public, Virtual
//  Description: The destructor is responsible for removing the
//               RenderEffect from the global set if it is there.
////////////////////////////////////////////////////////////////////
RenderEffect::
~RenderEffect() {
  if (_saved_entry != _effects->end()) {
    // We cannot make this assertion, because the RenderEffect has
    // already partially destructed--this means we cannot look up the
    // object in the map.  In fact, the map is temporarily invalid
    // until we finish destructing, since we screwed up the ordering
    // when we changed the return value of get_type().
    //    nassertv(_effects->find(this) == _saved_entry);

    // Note: this isn't thread-safe, because once the derived class
    // destructor exits and before this destructor completes, the map
    // is invalid, and other threads may inadvertently attempt to read
    // the invalid map.  To make it thread-safe, we need to move this
    // functionality to a separate method, that is to be called from
    // *each* derived class's destructor (and then we can put the
    // above assert back in).
    _effects->erase(_saved_entry);
    _saved_entry = _effects->end();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::safe_to_transform
//       Access: Public, Virtual
//  Description: Returns true if it is generally safe to transform
//               this particular kind of RenderEffect by calling the
//               xform() method, false otherwise.
////////////////////////////////////////////////////////////////////
bool RenderEffect::
safe_to_transform() const {
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::safe_to_combine
//       Access: Public, Virtual
//  Description: Returns true if this kind of effect can safely be
//               combined with sibling nodes that share the exact same
//               effect, or false if this is not a good idea.
////////////////////////////////////////////////////////////////////
bool RenderEffect::
safe_to_combine() const {
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::xform
//       Access: Public, Virtual
//  Description: Returns a new RenderEffect transformed by the
//               indicated matrix.
////////////////////////////////////////////////////////////////////
CPT(RenderEffect) RenderEffect::
xform(const LMatrix4f &) const {
  return this;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::has_cull_callback
//       Access: Public, Virtual
//  Description: Should be overridden by derived classes to return
//               true if cull_callback() has been defined.  Otherwise,
//               returns false to indicate cull_callback() does not
//               need to be called for this effect during the cull
//               traversal.
////////////////////////////////////////////////////////////////////
bool RenderEffect::
has_cull_callback() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::cull_callback
//       Access: Public, Virtual
//  Description: If has_cull_callback() returns true, this function
//               will be called during the cull traversal to perform
//               any additional operations that should be performed at
//               cull time.  This may include additional manipulation
//               of render state or additional visible/invisible
//               decisions, or any other arbitrary operation.
//
//               At the time this function is called, the current
//               node's transform and state have not yet been applied
//               to the net_transform and net_state.  This callback
//               may modify the node_transform and node_state to apply
//               an effective change to the render state at this
//               level.
////////////////////////////////////////////////////////////////////
void RenderEffect::
cull_callback(CullTraverser *, CullTraverserData &,
              CPT(TransformState) &, CPT(RenderState) &) const {
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::has_net_transform
//       Access: Public, Virtual
//  Description: Should be overridden by derived classes to return
//               true if net_transform() has been defined, and
//               therefore the RenderEffect has some effect on the
//               node's apparent net transform.
////////////////////////////////////////////////////////////////////
bool RenderEffect::
has_net_transform() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::net_transform
//       Access: Public, Virtual
//  Description: Given the node's parent's net transform, compute its
//               parent's new net transform after application of the
//               RenderEffect.  Presumably this interposes some
//               special transform derived from the RenderEffect.
//               This may only be called if has_net_transform(),
//               above, has been defined to return true.
////////////////////////////////////////////////////////////////////
CPT(TransformState) RenderEffect::
net_transform(CPT(TransformState) &orig_net_transform) const {
  return orig_net_transform;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::output
//       Access: Published, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
void RenderEffect::
output(ostream &out) const {
  out << get_type();
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::write
//       Access: Published, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
void RenderEffect::
write(ostream &out, int indent_level) const {
  indent(out, indent_level) << *this << "\n";
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::get_num_effects
//       Access: Published, Static
//  Description: Returns the total number of unique RenderEffect
//               objects allocated in the world.  This will go up and
//               down during normal operations.
////////////////////////////////////////////////////////////////////
int RenderEffect::
get_num_effects() {
  if (_effects == (Effects *)NULL) {
    return 0;
  }
  return _effects->size();
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::list_effects
//       Access: Published, Static
//  Description: Lists all of the RenderEffects in the cache to the
//               output stream, one per line.  This can be quite a lot
//               of output if the cache is large, so be prepared.
////////////////////////////////////////////////////////////////////
void RenderEffect::
list_effects(ostream &out) {
  out << _effects->size() << " effects:\n";
  Effects::const_iterator si;
  for (si = _effects->begin(); si != _effects->end(); ++si) {
    const RenderEffect *effect = (*si);
    effect->write(out, 2);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::validate_effects
//       Access: Published, Static
//  Description: Ensures that the cache is still stored in sorted
//               order.  Returns true if so, false if there is a
//               problem (which implies someone has modified one of
//               the supposedly-const RenderEffect objects).
////////////////////////////////////////////////////////////////////
bool RenderEffect::
validate_effects() {
  if (_effects->empty()) {
    return true;
  }

  Effects::const_iterator si = _effects->begin();
  Effects::const_iterator snext = si;
  ++snext;
  while (snext != _effects->end()) {
    if ((*si)->compare_to(*(*snext)) >= 0) {
      pgraph_cat.error()
        << "RenderEffects out of order!\n";
      (*si)->write(pgraph_cat.error(false), 2);
      (*snext)->write(pgraph_cat.error(false), 2);
      return false;
    }
    si = snext;
    ++snext;
  }

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::return_new
//       Access: Protected, Static
//  Description: This function is used by derived RenderEffect types
//               to share a common RenderEffect pointer for all
//               equivalent RenderEffect objects.
//
//               The make() function of the derived type should create
//               a new RenderEffect and pass it through return_new(),
//               which will either save the pointer and return it
//               unchanged (if this is the first similar such object)
//               or delete it and return an equivalent pointer (if
//               there was already a similar object saved).
////////////////////////////////////////////////////////////////////
CPT(RenderEffect) RenderEffect::
return_new(RenderEffect *effect) {
  nassertr(effect != (RenderEffect *)NULL, effect);

  // This should be a newly allocated pointer, not one that was used
  // for anything else.
  nassertr(effect->_saved_entry == _effects->end(), effect);

#ifndef NDEBUG
  if (paranoid_const) {
    nassertr(validate_effects(), effect);
  }
#endif

  // Save the effect in a local PointerTo so that it will be freed at
  // the end of this function if no one else uses it.
  CPT(RenderEffect) pt_effect = effect;

  pair<Effects::iterator, bool> result = _effects->insert(effect);
  if (result.second) {
    // The effect was inserted; save the iterator and return the
    // input effect.
    effect->_saved_entry = result.first;
    return pt_effect;
  }

  // The effect was not inserted; there must be an equivalent one
  // already in the set.  Return that one.
  return *(result.first);
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::compare_to_impl
//       Access: Protected, Virtual
//  Description: Intended to be overridden by derived RenderEffect
//               types to return a unique number indicating whether
//               this RenderEffect is equivalent to the other one.
//
//               This should return 0 if the two RenderEffect objects
//               are equivalent, a number less than zero if this one
//               should be sorted before the other one, and a number
//               greater than zero otherwise.
//
//               This will only be called with two RenderEffect
//               objects whose get_type() functions return the same.
////////////////////////////////////////////////////////////////////
int RenderEffect::
compare_to_impl(const RenderEffect *other) const {
  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::write_datagram
//       Access: Public, Virtual
//  Description: Writes the contents of this object to the datagram
//               for shipping out to a Bam file.
////////////////////////////////////////////////////////////////////
void RenderEffect::
write_datagram(BamWriter *manager, Datagram &dg) {
  TypedWritable::write_datagram(manager, dg);
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::change_this
//       Access: Public, Static
//  Description: Called immediately after complete_pointers(), this
//               gives the object a chance to adjust its own pointer
//               if desired.  Most objects don't change pointers after
//               completion, but some need to.
//
//               Once this function has been called, the old pointer
//               will no longer be accessed.
////////////////////////////////////////////////////////////////////
TypedWritable *RenderEffect::
change_this(TypedWritable *old_ptr, BamReader *manager) {
  // First, uniquify the pointer.
  RenderEffect *effect = DCAST(RenderEffect, old_ptr);
  CPT(RenderEffect) pointer = return_new(effect);

  // But now we have a problem, since we have to hold the reference
  // count and there's no way to return a TypedWritable while still
  // holding the reference count!  We work around this by explicitly
  // upping the count, and also setting a finalize() callback to down
  // it later.
  if (pointer == effect) {
    pointer->ref();
    manager->register_finalize(effect);
  }
  
  // We have to cast the pointer back to non-const, because the bam
  // reader expects that.
  return (RenderEffect *)pointer.p();
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::finalize
//       Access: Public, Virtual
//  Description: Method to ensure that any necessary clean up tasks
//               that have to be performed by this object are performed
////////////////////////////////////////////////////////////////////
void RenderEffect::
finalize() {
  // Unref the pointer that we explicitly reffed in make_from_bam().
  unref();

  // We should never get back to zero after unreffing our own count,
  // because we expect to have been stored in a pointer somewhere.  If
  // we do get to zero, it's a memory leak; the way to avoid this is
  // to call unref_delete() above instead of unref(), but this is
  // dangerous to do from within a virtual function.
  nassertv(get_ref_count() != 0);
}

////////////////////////////////////////////////////////////////////
//     Function: RenderEffect::fillin
//       Access: Protected
//  Description: This internal function is called by make_from_bam to
//               read in all of the relevant data from the BamFile for
//               the new RenderEffect.
////////////////////////////////////////////////////////////////////
void RenderEffect::
fillin(DatagramIterator &scan, BamReader *manager) {
  TypedWritable::fillin(scan, manager);
}
