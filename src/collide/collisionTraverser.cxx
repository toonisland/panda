// Filename: collisionTraverser.cxx
// Created by:  drose (24Apr00)
// 
////////////////////////////////////////////////////////////////////

#include "collisionTraverser.h"
#include "collisionNode.h"
#include "collisionEntry.h"
#include "collisionPolygon.h"
#include "config_collide.h"

#include <dftraverser.h>
#include <wrt.h>
#include <get_rel_pos.h>
#include <transformTransition.h>
#include <pStatTimer.h>
#include <geomNode.h>
#include <geom.h>


PStatCollector CollisionTraverser::_collisions_pcollector =
  PStatCollector("Collisions", RGBColorf(1,0.5,0), 40);

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::Constructor
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
CollisionTraverser::
CollisionTraverser(TypeHandle graph_type) :
  _graph_type(graph_type)
{
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::Destructor
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
CollisionTraverser::
~CollisionTraverser() {
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::add_collider
//       Access: Public
//  Description: Adds a new CollisionNode, representing an object that
//               will be tested for collisions into other objects,
//               along with the handler that will serve each detected
//               collision.  Each CollisionNode may be served by only
//               one handler at a time, but a given handler may serve
//               many CollisionNodes.
//
//               The handler that serves a particular node may be
//               changed from time to time by calling add_collider()
//               again on the same node.
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
add_collider(CollisionNode *node, CollisionHandler *handler) {
  nassertv(_ordered_colliders.size() == _colliders.size());
  nassertv(node != (CollisionNode *)NULL);
  nassertv(handler != (CollisionHandler *)NULL);

  Colliders::iterator ci = _colliders.find(node);
  if (ci != _colliders.end()) {
    // We already knew about this collider.
    if ((*ci).second != handler) {
      // Change the handler.
      (*ci).second = handler;

      // Now update our own reference counts within our handler set.
      CollisionHandler *old_handler = (*ci).second;
      Handlers::iterator hi = _handlers.find(old_handler);
      nassertv(hi != _handlers.end());
      (*hi).second--;
      nassertv((*hi).second >= 0);
      if ((*hi).second == 0) {
	_handlers.erase(hi);
      }

      hi = _handlers.find(handler);
      if (hi == _handlers.end()) {
	_handlers.insert(Handlers::value_type(handler, 1));
      } else {
	(*hi).second++;
      }
    }

  } else {
    // We hadn't already known about this collider.
    _colliders.insert(Colliders::value_type(node, handler));
    _ordered_colliders.push_back(node);

    Handlers::iterator hi = _handlers.find(handler);
    if (hi == _handlers.end()) {
      _handlers.insert(Handlers::value_type(handler, 1));
    } else {
      (*hi).second++;
    }
  }

  nassertv(_ordered_colliders.size() == _colliders.size());
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::remove_collider
//       Access: Public
//  Description: Removes the collider (and its associated handler)
//               from the set of CollisionNodes that will be tested
//               each frame for collisions into other objects.
//               Returns true if the definition was found and removed,
//               false if it wasn't present to begin with.
////////////////////////////////////////////////////////////////////
bool CollisionTraverser::
remove_collider(CollisionNode *node) {
  nassertr(_ordered_colliders.size() == _colliders.size(), false);

  Colliders::iterator ci = _colliders.find(node);
  if (ci == _colliders.end()) {
    // We didn't know about this node.
    return false;
  }

  CollisionHandler *handler = (*ci).second;

  // Update the set of handlers.
  Handlers::iterator hi = _handlers.find(handler);
  nassertr(hi != _handlers.end(), false);
  (*hi).second--;
  nassertr((*hi).second >= 0, false);
  if ((*hi).second == 0) {
    _handlers.erase(hi);
  }

  _colliders.erase(ci);

  OrderedColliders::iterator oci = 
    find(_ordered_colliders.begin(), _ordered_colliders.end(), node);
  nassertr(oci != _ordered_colliders.end(), false);
  _ordered_colliders.erase(oci);

  nassertr(_ordered_colliders.size() == _colliders.size(), false);
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::has_collider
//       Access: Public
//  Description: Returns true if the indicated node is current in the
//               set of nodes that will be tested each frame for
//               collisions into other objects.
////////////////////////////////////////////////////////////////////
bool CollisionTraverser::
has_collider(CollisionNode *node) const {
  Colliders::const_iterator ci = _colliders.find(node);
  return (ci != _colliders.end());
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::get_num_colliders
//       Access: Public
//  Description: Returns the number of CollisionNodes that have been
//               added to the traverser via add_collider().
////////////////////////////////////////////////////////////////////
int CollisionTraverser::
get_num_colliders() const {
  nassertr(_ordered_colliders.size() == _colliders.size(), 0);
  return _ordered_colliders.size();
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::get_collider
//       Access: Public
//  Description: Returns the nth CollisionNode that has been
//               added to the traverser via add_collider().
////////////////////////////////////////////////////////////////////
CollisionNode *CollisionTraverser::
get_collider(int n) const {
  nassertr(_ordered_colliders.size() == _colliders.size(), NULL);
  nassertr(n >= 0 && n < (int)_ordered_colliders.size(), NULL);
  return _ordered_colliders[n];
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::get_handler
//       Access: Public
//  Description: Returns the handler that is currently assigned to
//               serve the indicated collision node, or NULL if the
//               node is not on the traverser's set of active nodes.
////////////////////////////////////////////////////////////////////
CollisionHandler *CollisionTraverser::
get_handler(CollisionNode *node) const {
  Colliders::const_iterator ci = _colliders.find(node);
  if (ci != _colliders.end()) {
    return (*ci).second;
  };
  return NULL;
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::clear_colliders
//       Access: Public
//  Description: Completely empties the set of collision nodes and
//               their associated handlers.
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
clear_colliders() {
  _colliders.clear();
  _ordered_colliders.clear();
}



////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::traverse
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
traverse(Node *root) {
  PStatTimer timer(_collisions_pcollector);

  CollisionLevelState level_state;
  prepare_colliders(level_state);

  Handlers::iterator hi;
  for (hi = _handlers.begin(); hi != _handlers.end(); ++hi) {
    (*hi).first->begin_group();
  }

  df_traverse(root, *this, NullAttributeWrapper(), level_state,
	      _graph_type);

  for (hi = _handlers.begin(); hi != _handlers.end(); ++hi) {
    (*hi).first->end_group();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::output
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
output(ostream &out) const {
  out << "CollisionTraverser, " << _colliders.size()
      << " colliders and " << _handlers.size() << " handlers.\n";
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::write
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
write(ostream &out, int indent_level) const {
  indent(out, indent_level)
    << "CollisionTraverser, " << _colliders.size()
    << " colliders and " << _handlers.size() << " handlers:\n";
  Colliders::const_iterator ci;
  for (ci = _colliders.begin(); ci != _colliders.end(); ++ci) {
    CollisionNode *cnode = (*ci).first;
    CollisionHandler *handler = (*ci).second;
    nassertv(cnode != (CollisionNode *)NULL);
    nassertv(handler != (CollisionHandler *)NULL);

    indent(out, indent_level + 2)
      << *cnode << " handled by " << handler->get_type() << "\n";
    int num_solids = cnode->get_num_solids();
    for (int i = 0; i < num_solids; i++) {
      cnode->get_solid(i)->write(out, indent_level + 4);
    }
  }
}


////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::prepare_colliders
//       Access: Private
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
prepare_colliders(CollisionLevelState &level_state) {
  level_state.clear();
  level_state.reserve(_colliders.size());

  int i = 0;
  while (i < (int)_ordered_colliders.size()) {
    CollisionNode *cnode = _ordered_colliders[i];

    CollisionLevelState::ColliderDef def;
    def._node = cnode;
    get_rel_mat(cnode, NULL, def._space, _graph_type);
    def._inv_space.invert_from(def._space);

#ifndef NDEBUG
    if (def._space.is_nan()) {
      collide_cat.error()
	<< "Collider " << *cnode
	<< " has become NaN.  Dropping from traverser.\n";
      // This is safe to do while traversing the list of colliders,
      // because we do not increment i in this case.
      remove_collider(cnode);
    } else
#endif
      {
	int num_solids = cnode->get_num_solids();
	for (int s = 0; s < num_solids; s++) {
	  CollisionSolid *collider = cnode->get_solid(s);
	  def._collider = collider;
	  level_state.prepare_collider(def);
	}
	i++;
      }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::reached_node
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
bool CollisionTraverser::
reached_node(Node *node, NullAttributeWrapper &, 
	     CollisionLevelState &level_state) {
  if (node->is_of_type(CollisionNode::get_class_type())) {
    level_state.reached_collision_node();

    CollisionNode *cnode;
    DCAST_INTO_R(cnode, node, false);
    const BoundingVolume &node_bv = cnode->get_bound();
    const GeometricBoundingVolume *node_gbv = NULL;
    if (node_bv.is_of_type(GeometricBoundingVolume::get_class_type())) {
      DCAST_INTO_R(node_gbv, &node_bv, false);
    }

    CollisionEntry entry;
    entry._into_node = cnode;
    get_rel_mat(node, NULL, entry._into_space, _graph_type);

    int num_colliders = level_state.get_num_colliders();
    for (int c = 0; c < num_colliders; c++) {
      if (level_state.has_collider(c)) {
	entry._from_node = level_state.get_node(c);

	if ((entry._from_node->get_from_collide_mask() &
	     cnode->get_into_collide_mask()) != 0) {
	  entry._from = level_state.get_collider(c);
	  entry._from_space = level_state.get_space(c);
	  
	  LMatrix4f into_space_inv;
	  get_rel_mat(NULL, node, into_space_inv, _graph_type);
	  entry._wrt_space = entry._from_space * into_space_inv;
	  entry._inv_wrt_space = 
	    entry._into_space * level_state.get_inv_space(c);
	  
	  const GeometricBoundingVolume *col_gbv = 
	    level_state.get_local_bound(c);
	  
	  compare_collider_to_node(entry, col_gbv, node_gbv);
	}
      }
    }

  } else if (node->is_of_type(GeomNode::get_class_type()) &&
	     level_state.has_any_collide_geom()) {
    GeomNode *gnode;
    DCAST_INTO_R(gnode, node, false);
    const BoundingVolume &node_bv = gnode->get_bound();
    const GeometricBoundingVolume *node_gbv = NULL;
    if (node_bv.is_of_type(GeometricBoundingVolume::get_class_type())) {
      DCAST_INTO_R(node_gbv, &node_bv, false);
    }

    CollisionEntry entry;
    entry._into_node = gnode;
    get_rel_mat(node, NULL, entry._into_space, _graph_type);

    int num_colliders = level_state.get_num_colliders();
    for (int c = 0; c < num_colliders; c++) {
      if (level_state.has_collider_with_geom(c)) {
	entry._from_node = level_state.get_node(c);

	entry._from = level_state.get_collider(c);
	entry._from_space = level_state.get_space(c);
	  
	LMatrix4f into_space_inv;
	get_rel_mat(NULL, node, into_space_inv, _graph_type);
	entry._wrt_space = entry._from_space * into_space_inv;
	entry._inv_wrt_space = 
	  entry._into_space * level_state.get_inv_space(c);
	  
	const GeometricBoundingVolume *col_gbv = 
	  level_state.get_local_bound(c);
	  
	compare_collider_to_geom_node(entry, col_gbv, node_gbv);
      }
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::forward_arc
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
bool CollisionTraverser::
forward_arc(NodeRelation *arc, NullTransitionWrapper &,
	    NullAttributeWrapper &, NullAttributeWrapper &,
	    CollisionLevelState &level_state) {
  // Check the bounding volume on the arc against each of our
  // colliders.
  const BoundingVolume &arc_bv = arc->get_bound();
  if (arc_bv.is_of_type(GeometricBoundingVolume::get_class_type())) {
    const GeometricBoundingVolume *arc_gbv;
    DCAST_INTO_R(arc_gbv, &arc_bv, false);

    int num_colliders = level_state.get_num_colliders();
    for (int c = 0; c < num_colliders; c++) {
      if (level_state.has_collider(c)) {
	const GeometricBoundingVolume *col_gbv = 
	  level_state.get_local_bound(c);
	if (col_gbv != (GeometricBoundingVolume *)NULL) {
	  if (arc_gbv->contains(col_gbv) == 0) {
	    // This collider certainly does not intersect with any
	    // geometry at this arc or below.
	    level_state.omit_collider(c);
	  }
	}
      }
    }
  }

  if (!level_state.has_any_collider()) {
    // No colliders intersect with the geometry at this arc or below,
    // so don't bother traversing them.
    return false;
  }

  TransformTransition *tt;
  if (get_transition_into(tt, arc)) {
    // There's a transform on the arc; apply it to our colliders'
    // bounding volumes.
    LMatrix4f inv_mat;
    inv_mat.invert_from(tt->get_matrix());
    level_state.xform(inv_mat);
  }

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::compare_collider_to_node
//       Access: Private
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
compare_collider_to_node(CollisionEntry &entry,
			 const GeometricBoundingVolume *from_node_gbv,
			 const GeometricBoundingVolume *into_node_gbv) {
  bool within_node_bounds = true;
  if (from_node_gbv != (GeometricBoundingVolume *)NULL &&
      into_node_gbv != (GeometricBoundingVolume *)NULL) {
    within_node_bounds = (into_node_gbv->contains(from_node_gbv) != 0);
  }
	  
  if (within_node_bounds) {
    CollisionNode *cnode;
    DCAST_INTO_V(cnode, entry._into_node);
    int num_solids = cnode->get_num_solids();
    for (int s = 0; s < num_solids; s++) {
      entry._into = cnode->get_solid(s);
      if (entry._from != entry._into) {
	const BoundingVolume &solid_bv = entry._into->get_bound();
	const GeometricBoundingVolume *solid_gbv = NULL;
	if (num_solids > 1 &&
	    solid_bv.is_of_type(GeometricBoundingVolume::get_class_type())) {
	  // Only bother to test against each solid's bounding
	  // volume if we have more than one solid in the node, as a
	  // slight optimization.  (If the node contains just one
	  // solid, then the node's bounding volume, which we just
	  // tested, is the same as the solid's bounding volume.)
	  DCAST_INTO_V(solid_gbv, &solid_bv);
	}
	
	compare_collider_to_solid(entry, from_node_gbv, solid_gbv);
      }
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::compare_collider_to_geom_node
//       Access: Private
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
compare_collider_to_geom_node(CollisionEntry &entry,
			      const GeometricBoundingVolume *from_node_gbv,
			      const GeometricBoundingVolume *into_node_gbv) {
  bool within_node_bounds = true;
  if (from_node_gbv != (GeometricBoundingVolume *)NULL &&
      into_node_gbv != (GeometricBoundingVolume *)NULL) {
    within_node_bounds = (into_node_gbv->contains(from_node_gbv) != 0);
  }
	  
  if (within_node_bounds) {
    GeomNode *gnode;
    DCAST_INTO_V(gnode, entry._into_node);
    int num_geoms = gnode->get_num_geoms();
    for (int s = 0; s < num_geoms; s++) {
      entry._into = (CollisionSolid *)NULL;
      Geom *geom = DCAST(Geom, gnode->get_geom(s));
      if (geom != (Geom *)NULL) {
	const BoundingVolume &geom_bv = geom->get_bound();
	const GeometricBoundingVolume *geom_gbv = NULL;
	if (num_geoms > 1 &&
	    geom_bv.is_of_type(GeometricBoundingVolume::get_class_type())) {
	  // Only bother to test against each geom's bounding
	  // volume if we have more than one geom in the node, as a
	  // slight optimization.  (If the node contains just one
	  // geom, then the node's bounding volume, which we just
	  // tested, is the same as the geom's bounding volume.)
	  DCAST_INTO_V(geom_gbv, &geom_bv);
	}
	
	compare_collider_to_geom(entry, geom, from_node_gbv, geom_gbv);
      }
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::compare_collider_to_solid
//       Access: Private
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
compare_collider_to_solid(CollisionEntry &entry,
			  const GeometricBoundingVolume *from_node_gbv,
			  const GeometricBoundingVolume *solid_gbv) {
  bool within_solid_bounds = true;
  if (from_node_gbv != (GeometricBoundingVolume *)NULL &&
      solid_gbv != (GeometricBoundingVolume *)NULL) {
    within_solid_bounds = (solid_gbv->contains(from_node_gbv) != 0);
  }
  if (within_solid_bounds) {
    Colliders::const_iterator ci;
    ci = _colliders.find(entry.get_from_node());
    nassertv(ci != _colliders.end());
    entry.get_from()->test_intersection((*ci).second, entry, entry.get_into());
  }
}

////////////////////////////////////////////////////////////////////
//     Function: CollisionTraverser::compare_collider_to_geom
//       Access: Private
//  Description: 
////////////////////////////////////////////////////////////////////
void CollisionTraverser::
compare_collider_to_geom(CollisionEntry &entry, Geom *geom,
			 const GeometricBoundingVolume *from_node_gbv,
			 const GeometricBoundingVolume *geom_gbv) {
  bool within_geom_bounds = true;
  if (from_node_gbv != (GeometricBoundingVolume *)NULL &&
      geom_gbv != (GeometricBoundingVolume *)NULL) {
    within_geom_bounds = (geom_gbv->contains(from_node_gbv) != 0);
  }
  if (within_geom_bounds) {
    Colliders::const_iterator ci;
    ci = _colliders.find(entry.get_from_node());
    nassertv(ci != _colliders.end());

    PTA_Vertexf coords;
    GeomBindType bind;
    PTA_ushort vindex;
    geom->get_coords(coords, bind, vindex);
    PTA_ushort tris = geom->get_tris();
    
    for (int i = 0; i < (int)tris.size(); i += 3) {
      if (CollisionPolygon::verify_points(coords[tris[i]],
					  coords[tris[i + 1]],
					  coords[tris[i + 2]])) {
	// Generate a temporary CollisionPolygon on the fly for each
	// triangle in the Geom.
	CollisionPolygon poly(coords[tris[i]], coords[tris[i + 1]],
			      coords[tris[i + 2]]);
	if (entry.get_from()->test_intersection((*ci).second, entry, &poly) != 0) {
	  return;
	}
      }
    }
  }
}
