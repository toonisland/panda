// Filename: physicsManager.cxx
// Created by:  charles (14Jun00)
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

#include "physicsManager.h"
#include "actorNode.h"

#include <algorithm>
#include "pvector.h"

////////////////////////////////////////////////////////////////////
//     Function : PhysicsManager
//       Access : Public
//  Description : Default Constructor.  NOTE: EulerIntegrator is
//                the standard default.
////////////////////////////////////////////////////////////////////
PhysicsManager::
PhysicsManager() {
  _linear_integrator.clear();
  _angular_integrator.clear();
  _viscosity=0.0;
}

////////////////////////////////////////////////////////////////////
//     Function : ~PhysicsManager
//       Access : Public
//  Description : Simple Destructor
////////////////////////////////////////////////////////////////////
PhysicsManager::
~PhysicsManager() {
}

////////////////////////////////////////////////////////////////////
//     Function : remove_linear_force
//       Access : Public
//  Description : takes a linear force out of the physics list
////////////////////////////////////////////////////////////////////
void PhysicsManager::
remove_linear_force(LinearForce *f) {
  nassertv(f);
  LinearForceVector::iterator found;

  PT(LinearForce) ptbf = f;
  found = find(_linear_forces.begin(), _linear_forces.end(), ptbf);

  if (found == _linear_forces.end()) {
    return;
  }
  _linear_forces.erase(found);
}

////////////////////////////////////////////////////////////////////
//     Function : remove_angular_force
//       Access : Public
//  Description : takes an angular force out of the physics list
////////////////////////////////////////////////////////////////////
void PhysicsManager::
remove_angular_force(AngularForce *f) {
  nassertv(f);
  AngularForceVector::iterator found;

  PT(BaseForce) ptbf = f;
  found = find(_angular_forces.begin(), _angular_forces.end(), ptbf);

  if (found == _angular_forces.end())
    return;

  _angular_forces.erase(found);
}

////////////////////////////////////////////////////////////////////
//     Function : remove_physical
//       Access : Public
//  Description : takes a physical out of the object list
////////////////////////////////////////////////////////////////////
void PhysicsManager::
remove_physical(Physical *p) {
  nassertv(p);
  pvector< Physical * >::iterator found;

  found = find(_physicals.begin(), _physicals.end(), p);
  if (found == _physicals.end()) {
    return;
  }
  p->_physics_manager = (PhysicsManager *) NULL;
  _physicals.erase(found);
}

////////////////////////////////////////////////////////////////////
//     Function : DoPhysics
//       Access : Public
//  Description : This is the main high-level API call.  Performs
//                integration on every attached Physical.
////////////////////////////////////////////////////////////////////
void PhysicsManager::
do_physics(float dt) {
  // now, run through each physics object in the set.
  PhysicalsVector::iterator p_cur = _physicals.begin();
  for (; p_cur != _physicals.end(); ++p_cur) {
    Physical *physical = *p_cur;
    nassertv(physical);

    // do linear
    if (_linear_integrator.is_null() == false) {
      _linear_integrator->integrate(physical, _linear_forces, dt);
    }

    // do angular
    if (_angular_integrator.is_null() == false) {
      _angular_integrator->integrate(physical, _angular_forces, dt);
    }

    // if it's an actor node, tell it to update itself.
    PhysicalNode *pn = physical->get_physical_node();
    if (pn && pn->is_of_type(ActorNode::get_class_type())) {
      ActorNode *an = (ActorNode *) pn;
      an->update_transform();
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function : output
//       Access : Public
//  Description : Write a string representation of this instance to
//                <out>.
////////////////////////////////////////////////////////////////////
void PhysicsManager::
output(ostream &out) const {
  #ifndef NDEBUG //[
  out<<""<<"PhysicsManager";
  #endif //] NDEBUG
}

////////////////////////////////////////////////////////////////////
//     Function : write_physicals
//       Access : Public
//  Description : Write a string representation of this instance to
//                <out>.
////////////////////////////////////////////////////////////////////
void PhysicsManager::
write_physicals(ostream &out, unsigned int indent) const {
  #ifndef NDEBUG //[
  if (indent>10) {
    return;
  }
  out.width(indent);
  out<<""<<"_physicals ("<<_physicals.size()<<" physicals)\n";
  //out<<ios::width(indent)<<" "<<"[physicals \n";
  for (pvector< Physical * >::const_iterator i=_physicals.begin();
       i != _physicals.end();
       ++i) {
    (*i)->write(out, indent+2);
  }
  #endif //] NDEBUG
}

////////////////////////////////////////////////////////////////////
//     Function : write_forces
//       Access : Public
//  Description : Write a string representation of this instance to
//                <out>.
////////////////////////////////////////////////////////////////////
void PhysicsManager::
write_linear_forces(ostream &out, unsigned int indent) const {
  #ifndef NDEBUG //[
  out.width(indent);
  out<<""<<"_linear_forces ("<<_linear_forces.size()<<" forces)\n";
  for (LinearForceVector::const_iterator i=_linear_forces.begin();
       i != _linear_forces.end();
       ++i) {
    (*i)->write(out, indent+2);
  }
  #endif //] NDEBUG
}

////////////////////////////////////////////////////////////////////
//     Function : write_angular_forces
//       Access : Public
//  Description : Write a string representation of this instance to
//                <out>.
////////////////////////////////////////////////////////////////////
void PhysicsManager::
write_angular_forces(ostream &out, unsigned int indent) const {
  #ifndef NDEBUG //[
  out.width(indent);
  out<<""<<"_angular_forces ("<<_angular_forces.size()<<" forces)\n";
  for (AngularForceVector::const_iterator i=_angular_forces.begin();
       i != _angular_forces.end();
       ++i) {
    (*i)->write(out, indent+2);
  }
  #endif //] NDEBUG
}

////////////////////////////////////////////////////////////////////
//     Function : write
//       Access : Public
//  Description : Write a string representation of this instance to
//                <out>.
////////////////////////////////////////////////////////////////////
void PhysicsManager::
write(ostream &out, unsigned int indent) const {
  #ifndef NDEBUG //[
  out.width(indent); out<<""<<"PhysicsManager:\n";
  if (indent>20) {
    // ...indent limit is arbitrary, it limits recursion.
    out.width(indent+2); out<<""<<"...\n";
    return;
  }
  write_physicals(out, indent+2);
  write_linear_forces(out, indent+2);
  write_angular_forces(out, indent+2);
  out.width(indent+2); out<<""<<"_linear_integrator:\n";
  if (_linear_integrator) {
    _linear_integrator->write(out, indent+4);
  } else {
    out.width(indent+4); out<<""<<"null\n";
  }
  out.width(indent+2); out<<""<<"_angular_integrator:\n";
  if (_angular_integrator) {
    _angular_integrator->write(out, indent+4);
  } else {
    out.width(indent+4); out<<""<<"null\n";
  }
  #endif //] NDEBUG
}
