// Filename: lineParticleRenderer.h
// Created by:  darren (06Oct00)
// 
////////////////////////////////////////////////////////////////////

#ifndef LINEPARTICLERENDERER_H
#define LINEPARTICLERENDERER_H

#include "baseParticle.h"
#include "baseParticleRenderer.h"

#include <pointerTo.h>
#include <pointerToArray.h>
#include <geom.h>
#include <geomLine.h>

////////////////////////////////////////////////////////////////////
//       Class : LineParticleRenderer
// Description : renders a line from last position to current
//               position -- good for rain, sparks, etc.
////////////////////////////////////////////////////////////////////

class EXPCL_PANDAPHYSICS LineParticleRenderer : public BaseParticleRenderer {
private:

  Colorf _head_color;
  Colorf _tail_color;

  PT(GeomLine) _line_primitive;

  PTA_Vertexf _vertex_array;
  PTA_Colorf _color_array;

  int _max_pool_size;

  LPoint3f _aabb_min, _aabb_max;

  virtual void birth_particle(int index);
  virtual void kill_particle(int index);
  virtual void init_geoms(void);
  virtual void render(vector< PT(PhysicsObject) >& po_vector,
                      int ttl_particles);
  virtual void resize_pool(int new_size);

public:

  LineParticleRenderer(void);
  LineParticleRenderer(const LineParticleRenderer& copy);
  LineParticleRenderer(const Colorf& head,
                       const Colorf& tail,
                       ParticleRendererAlphaMode alpha_mode);

  virtual ~LineParticleRenderer(void);

  virtual BaseParticleRenderer *make_copy(void);

  INLINE void set_head_color(const Colorf& c);
  INLINE void set_tail_color(const Colorf& c);

  INLINE const Colorf& get_head_color(void) const;
  INLINE const Colorf& get_tail_color(void) const;
};

#include "lineParticleRenderer.I"

#endif // LINEPARTICLERENDERER_H
