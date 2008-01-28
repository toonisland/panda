// Filename: dxGraphicsStateGuardian9.cxx
// Created by:  mike (02Feb99)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) 2001 - 2006, Disney Enterprises, Inc.  All rights reserved
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

#include "dxGraphicsStateGuardian9.h"
#include "config_dxgsg9.h"
#include "displayRegion.h"
#include "renderBuffer.h"
#include "geom.h"
#include "graphicsWindow.h"
#include "graphicsEngine.h"
#include "lens.h"
#include "ambientLight.h"
#include "directionalLight.h"
#include "pointLight.h"
#include "spotlight.h"
#include "textureAttrib.h"
#include "texGenAttrib.h"
#include "shadeModelAttrib.h"
#include "cullFaceAttrib.h"
#include "transparencyAttrib.h"
#include "alphaTestAttrib.h"
#include "depthTestAttrib.h"
#include "depthWriteAttrib.h"
#include "colorWriteAttrib.h"
#include "texMatrixAttrib.h"
#include "materialAttrib.h"
#include "renderModeAttrib.h"
#include "rescaleNormalAttrib.h"
#include "fogAttrib.h"
#include "depthOffsetAttrib.h"
#include "fog.h"
#include "throw_event.h"
#include "geomVertexFormat.h"
#include "geomVertexData.h"
#include "geomTriangles.h"
#include "geomTristrips.h"
#include "geomTrifans.h"
#include "geomLines.h"
#include "geomLinestrips.h"
#include "geomPoints.h"
#include "geomVertexReader.h"
#include "dxGeomMunger9.h"
#include "config_gobj.h"
#include "dxVertexBufferContext9.h"
#include "dxIndexBufferContext9.h"
#include "dxOcclusionQueryContext9.h"
#include "pStatTimer.h"
#include "pStatCollector.h"
#include "wdxGraphicsBuffer9.h"
#include "config_pgraph.h"
#ifdef HAVE_CG
#include "Cg/cgD3D9.h"
#endif

#include <mmsystem.h>

#include <dxsdkver.h>

#define tostring(x) #x
#define SDK_VERSION(major,minor) tostring(major) << "." << tostring(minor)
#define DIRECTX_SDK_VERSION  SDK_VERSION (_DXSDK_PRODUCT_MAJOR, _DXSDK_PRODUCT_MINOR) << "." << SDK_VERSION (_DXSDK_BUILD_MAJOR, _DXSDK_BUILD_MINOR)

#define DEBUG_LRU false


TypeHandle DXGraphicsStateGuardian9::_type_handle;

D3DMATRIX DXGraphicsStateGuardian9::_d3d_ident_mat;

unsigned char *DXGraphicsStateGuardian9::_temp_buffer = NULL;
unsigned char *DXGraphicsStateGuardian9::_safe_buffer_start = NULL;

#define __D3DLIGHT_RANGE_MAX ((float)sqrt(FLT_MAX))  //for some reason this is missing in dx9 hdrs

#define MY_D3DRGBA(r, g, b, a) ((D3DCOLOR) D3DCOLOR_COLORVALUE(r, g, b, a))

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::Constructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
DXGraphicsStateGuardian9::
DXGraphicsStateGuardian9(GraphicsPipe *pipe) :
  GraphicsStateGuardian(CS_yup_left, pipe)
{
  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug()
      << "DXGraphicsStateGuardian9 " << this << " constructing\n";
  }

  _screen = NULL;
  _d3d_device = NULL;

  _dx_is_ready = false;
  _vertex_blending_enabled = false;
  _overlay_windows_supported = false;
  _tex_stats_retrieval_impossible = false;

  _active_vbuffer = NULL;
  _active_ibuffer = NULL;

  // This is a static member, but we initialize it here in the
  // constructor anyway.  It won't hurt if it gets repeatedly
  // initalized.
  ZeroMemory(&_d3d_ident_mat, sizeof(D3DMATRIX));
  _d3d_ident_mat._11 = _d3d_ident_mat._22 = _d3d_ident_mat._33 = _d3d_ident_mat._44 = 1.0f;

  _cur_read_pixel_buffer = RenderBuffer::T_front;
  set_color_clear_value(_color_clear_value);

  // DirectX drivers seem to consistently invert the texture when
  // they copy framebuffer-to-texture.  Ok.
  _copy_texture_inverted = true;

  // D3DRS_POINTSPRITEENABLE doesn't seem to support remapping the
  // texture coordinates via a texture matrix, so we don't advertise
  // GR_point_sprite_tex_matrix.
  _supported_geom_rendering =
    Geom::GR_point | Geom::GR_point_uniform_size |
    Geom::GR_point_perspective | Geom::GR_point_sprite |
    Geom::GR_indexed_other |
    Geom::GR_triangle_strip | Geom::GR_triangle_fan |
    Geom::GR_flat_first_vertex;

  _gsg_managed_textures = dx_management | dx_texture_management;
  _gsg_managed_vertex_buffers = dx_management;
  _gsg_managed_index_buffers = dx_management;

  _enable_lru = dx_lru_management;

  _lru = 0;
  _last_fvf = 0;

  _vertex_shader_version_major = 0;
  _vertex_shader_version_minor = 0;
  _pixel_shader_version_major = 0;
  _pixel_shader_version_minor = 0;

  _vertex_shader_profile = 0;
  _pixel_shader_profile = 0;

  _vertex_shader_maximum_constants = 0;

  _supports_stream_offset = false;

  get_gamma_table();
  atexit (atexit_function);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::Destructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
DXGraphicsStateGuardian9::
~DXGraphicsStateGuardian9() {
  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug()
      << "DXGraphicsStateGuardian9 " << this << " destructing\n";
  }

  if (IS_VALID_PTR(_d3d_device)) {
    _d3d_device->SetTexture(0, NULL);  // this frees reference to the old texture
  }
  free_nondx_resources();

  if (_lru) {
    delete _lru;
    _lru = 0;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::prepare_texture
//       Access: Public, Virtual
//  Description: Creates a new retained-mode representation of the
//               given texture, and returns a newly-allocated
//               TextureContext pointer to reference it.  It is the
//               responsibility of the calling function to later
//               call release_texture() with this same pointer (which
//               will also delete the pointer).
//
//               This function should not be called directly to
//               prepare a texture.  Instead, call Texture::prepare().
////////////////////////////////////////////////////////////////////
TextureContext *DXGraphicsStateGuardian9::
prepare_texture(Texture *tex) {
  DXTextureContext9 *dtc = new DXTextureContext9(_prepared_objects, tex);

  if (!get_supports_compressed_texture_format(tex->get_ram_image_compression())) {
    dxgsg9_cat.error()
      << *dtc->get_texture() << " is stored in an unsupported compressed format.\n";
    return NULL;
  }

  if (!dtc->create_texture(*_screen)) {
    delete dtc;
    return NULL;
  }

  return dtc;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::apply_texture
//       Access: Public
//  Description: Makes the texture the currently available texture for
//               rendering on the ith stage.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
apply_texture(int i, TextureContext *tc) {
  if (tc == (TextureContext *)NULL) {
    // The texture wasn't bound properly or something, so ensure
    // texturing is disabled and just return.
    set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
    return;
  }

  tc->set_active(true);

  DXTextureContext9 *dtc = DCAST(DXTextureContext9, tc);

  if (_lru) {
    _lru -> access_page (dtc -> _lru_page);
  }

  // If the texture image has changed, or if its use of mipmaps has
  // changed, we need to re-create the image.

  if (dtc->was_modified()) {
    if (!get_supports_compressed_texture_format(tc->get_texture()->get_ram_image_compression())) {
      dxgsg9_cat.error()
        << *dtc->get_texture() << " is stored in an unsupported compressed format.\n";
      set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
      return;
    }

    if (!dtc->create_texture(*_screen)) {
      // Oops, we can't re-create the texture for some reason.
      dxgsg9_cat.error()
        << "Unable to re-create texture " << *dtc->get_texture() << endl;
      set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
      return;
    }
  }

  Texture *tex = tc->get_texture();
  Texture::WrapMode wrap_u, wrap_v, wrap_w;

  DWORD address_u;
  DWORD address_v;
  DWORD address_w;

  wrap_u = tex->get_wrap_u();
  wrap_v = tex->get_wrap_v();
  wrap_w = tex->get_wrap_w();

  address_u = get_texture_wrap_mode(wrap_u);
  address_v = get_texture_wrap_mode(wrap_v);
  address_w = get_texture_wrap_mode(wrap_w);

  set_sampler_state(i, D3DSAMP_ADDRESSU, address_u);
  set_sampler_state(i, D3DSAMP_ADDRESSV, address_v);
  set_sampler_state(i, D3DSAMP_ADDRESSW, address_w);

  DWORD border_color;
  border_color = Colorf_to_D3DCOLOR(tex->get_border_color());

  set_sampler_state(i, D3DSAMP_BORDERCOLOR, border_color);

  uint aniso_degree = tex->get_anisotropic_degree();
  Texture::FilterType ft = tex->get_magfilter();

  if (aniso_degree >= 1) {
    set_sampler_state(i, D3DSAMP_MAXANISOTROPY, aniso_degree);
  }

  D3DTEXTUREFILTERTYPE new_mag_filter;
  if (aniso_degree <= 1) {
    new_mag_filter = ((ft != Texture::FT_nearest) ? D3DTEXF_LINEAR : D3DTEXF_POINT);
  } else {
    new_mag_filter = D3DTEXF_ANISOTROPIC;
  }

  set_sampler_state(i, D3DSAMP_MAGFILTER, new_mag_filter);

  // map Panda composite min+mip filter types to d3d's separate min & mip filter types
  D3DTEXTUREFILTERTYPE new_min_filter = get_d3d_min_type(tex->get_minfilter(),
                                                         tex->get_format());
  D3DTEXTUREFILTERTYPE new_mip_filter = get_d3d_mip_type(tex->get_minfilter(),
                                                         tex->get_format());

  if (!tex->might_have_ram_image()) {
    // If the texture is completely dynamic, don't try to issue
    // mipmaps--pandadx doesn't support auto-generated mipmaps at this
    // point.
    new_mip_filter = D3DTEXF_NONE;
  }

#ifndef NDEBUG
  // sanity check
  if ((!dtc->has_mipmaps()) && (new_mip_filter != D3DTEXF_NONE)) {
    dxgsg9_cat.error()
      << "Trying to set mipmap filtering for texture with no generated mipmaps!! texname["
      << tex->get_name() << "], filter("
      << tex->get_minfilter() << ")\n";
    new_mip_filter = D3DTEXF_NONE;
  }
#endif

  if (aniso_degree >= 2) {
    new_min_filter = D3DTEXF_ANISOTROPIC;
  }

  set_sampler_state(i, D3DSAMP_MINFILTER, new_min_filter);
  set_sampler_state(i, D3DSAMP_MIPFILTER, new_mip_filter);

  _d3d_device->SetTexture(i, dtc->get_d3d_texture());
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::release_texture
//       Access: Public, Virtual
//  Description: Frees the GL resources previously allocated for the
//               texture.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
release_texture(TextureContext *tc) {
  DXTextureContext9 *dtc = DCAST(DXTextureContext9, tc);
  delete dtc;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::prepare_shader
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
ShaderContext *DXGraphicsStateGuardian9::
prepare_shader(Shader *se) {
#ifdef HAVE_CG
  CLP(ShaderContext) *result = new CLP(ShaderContext)(se, this);
  return result;
#endif
  return NULL;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::release_shader
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
release_shader(ShaderContext *sc) {
  CLP(ShaderContext) *gsc = DCAST(CLP(ShaderContext), sc);
  delete gsc;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::prepare_vertex_buffer
//       Access: Public, Virtual
//  Description: Creates a new retained-mode representation of the
//               given data, and returns a newly-allocated
//               VertexBufferContext pointer to reference it.  It is the
//               responsibility of the calling function to later
//               call release_vertex_buffer() with this same pointer (which
//               will also delete the pointer).
//
//               This function should not be called directly to
//               prepare a buffer.  Instead, call Geom::prepare().
////////////////////////////////////////////////////////////////////
VertexBufferContext *DXGraphicsStateGuardian9::
prepare_vertex_buffer(GeomVertexArrayData *data) {
  DXVertexBufferContext9 *dvbc = new DXVertexBufferContext9(_prepared_objects, data, *(this -> _screen));
  return dvbc;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::apply_vertex_buffer
//       Access: Public
//  Description: Updates the vertex buffer with the current data, and
//               makes it the current vertex buffer for rendering.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
apply_vertex_buffer(VertexBufferContext *vbc,
                    CLP(ShaderContext) *shader_context,
                    const GeomVertexArrayDataHandle *reader, bool force) {
  DXVertexBufferContext9 *dvbc = DCAST(DXVertexBufferContext9, vbc);

  DBG_SH3 dxgsg9_cat.debug ( ) << "apply_vertex_buffer\n"; DBG_E

  bool set_stream_source;
  HRESULT hr;
  UINT stream;
  UINT offset;

  set_stream_source = false;
  stream = 0;
  offset = 0;

  if (_lru) {
    _lru -> access_page (dvbc -> _lru_page);
  }

  if (dvbc->_vbuffer == NULL) {
    // Attempt to create a new vertex buffer.
    if (vertex_buffers &&
        reader->get_usage_hint() != Geom::UH_client) {
      dvbc->create_vbuffer(*_screen, reader);
    }

    if (dvbc->_vbuffer != NULL) {
      if (!dvbc->upload_data(reader, force)) {
        return false;
      }

      dvbc->mark_loaded(reader);

      set_stream_source = true;

    } else {
      _active_vbuffer = NULL;
    }

  } else {
    if (dvbc->was_modified(reader)) {
      if (dvbc->changed_size(reader)) {
        // We have to destroy the old vertex buffer and create a new
        // one.
        dvbc->create_vbuffer(*_screen, reader);
      }

      if (!dvbc->upload_data(reader, force)) {
        return false;
      }

      dvbc->mark_loaded(reader);
      _active_vbuffer = NULL;
    }

    if (_active_vbuffer != dvbc) {
      set_stream_source = true;
    }
  }

  if (shader_context == 0) {
    // FVF MODE
    if (set_stream_source) {
      hr = _d3d_device->SetStreamSource
        (stream, dvbc->_vbuffer, offset, reader->get_array_format()->get_stride());
      if (FAILED(hr)) {
        dxgsg9_cat.error()
          << "SetStreamSource failed" << D3DERRORSTRING(hr);
      }
      _active_vbuffer = dvbc;
      _active_ibuffer = NULL;
      dvbc->set_active(true);
    }

    if ((dvbc->_fvf != _last_fvf)) {
      hr = _d3d_device->SetFVF(dvbc->_fvf);
      if (FAILED(hr)) {
        dxgsg9_cat.error() << "SetFVF failed" << D3DERRORSTRING(hr);
      }

      _last_fvf = dvbc->_fvf;
    }
  }
  else {
    // SHADER MODE
    if (set_stream_source) {
      if (dvbc -> _direct_3d_vertex_declaration) {
        if (dvbc -> _shader_context == shader_context) {
          // same shader as before, no need to remap a new vertex declaration
        }
        else {
          // need to make a new vertex declaration since the new shader may
          // have a different mapping
          dvbc -> _direct_3d_vertex_declaration -> Release ( );
          dvbc -> _direct_3d_vertex_declaration = 0;
          dvbc -> _shader_context = 0;
        }
      }

      if (dvbc -> _direct_3d_vertex_declaration == 0 &&
          dvbc -> _vertex_element_type_array) {
        VertexElementArray *vertex_element_array;

        vertex_element_array = shader_context -> _vertex_element_array;
        if (vertex_element_array) {
          int index;

          for (index = 0; index < vertex_element_array->total_elements; index++) {
            VERTEX_ELEMENT_TYPE *vertex_element_type;
            VERTEX_ELEMENT_TYPE *source_vertex_element_type;

            vertex_element_type =
              &vertex_element_array -> vertex_element_type_array [index];

            // MAP VERTEX ELEMENTS to VERTEX SHADER INPUTS
            // get offsets from vertex data for certain types of vertex elements

            offset = 0;
            source_vertex_element_type = dvbc -> _vertex_element_type_array;
            while (source_vertex_element_type -> vs_input_type != VS_END) {
              if (source_vertex_element_type -> vs_input_type == vertex_element_type -> vs_input_type &&
                  source_vertex_element_type -> index == vertex_element_type -> index) {
                  offset = source_vertex_element_type -> offset;
                  break;
              }
              source_vertex_element_type++;
            }
            if (source_vertex_element_type -> vs_input_type == VS_END) {
              dxgsg9_cat.error()
                << "unable to find a mapping for vertex shader input type="
                << vertex_element_type -> vs_input_type
                << " from vertex elements\n";
            }

            vertex_element_array -> set_vertex_element_offset (index, offset);
          }

          hr = _d3d_device -> CreateVertexDeclaration (
            vertex_element_array -> vertex_element_array,
            &dvbc -> _direct_3d_vertex_declaration);
          if (FAILED (hr)) {
            dxgsg9_cat.error()
              << "CreateVertexDeclaration failed"
              << D3DERRORSTRING(hr);
          }

          dvbc -> _shader_context = shader_context;
        }
        else {
          dxgsg9_cat.error() << "apply_vertex_buffer ( ): shader_context vertex_element_array == 0\n";
        }
      }

      offset = 0;
      hr = _d3d_device->SetStreamSource
        (stream, dvbc->_vbuffer, offset, reader->get_array_format()->get_stride());
      if (FAILED(hr)) {
        dxgsg9_cat.error()
          << "SetStreamSource failed" << D3DERRORSTRING(hr);
      }
      _active_vbuffer = dvbc;
      _active_ibuffer = NULL;
      dvbc->set_active(true);
    }

    if (dvbc -> _direct_3d_vertex_declaration) {

      DBG_SH5  dxgsg9_cat.debug() << "SetVertexDeclaration ( ) \n"; DBG_E

      hr = _d3d_device -> SetVertexDeclaration (dvbc -> _direct_3d_vertex_declaration);
      if (FAILED(hr)) {
        dxgsg9_cat.error()
          << "SetVertexDeclaration failed" << D3DERRORSTRING(hr);
      }
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::release_vertex_buffer
//       Access: Public, Virtual
//  Description: Frees the GL resources previously allocated for the
//               data.  This function should never be called
//               directly; instead, call Data::release() (or simply
//               let the Data destruct).
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
release_vertex_buffer(VertexBufferContext *vbc) {
  DXVertexBufferContext9 *dvbc = DCAST(DXVertexBufferContext9, vbc);
  delete dvbc;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::prepare_index_buffer
//       Access: Public, Virtual
//  Description: Creates a new retained-mode representation of the
//               given data, and returns a newly-allocated
//               IndexBufferContext pointer to reference it.  It is the
//               responsibility of the calling function to later call
//               release_index_buffer() with this same pointer (which
//               will also delete the pointer).
//
//               This function should not be called directly to
//               prepare a buffer.  Instead, call Geom::prepare().
////////////////////////////////////////////////////////////////////
IndexBufferContext *DXGraphicsStateGuardian9::
prepare_index_buffer(GeomPrimitive *data) {
  DXIndexBufferContext9 *dibc = new DXIndexBufferContext9(_prepared_objects, data);
  return dibc;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::apply_index_buffer
//       Access: Public
//  Description: Updates the index buffer with the current data, and
//               makes it the current index buffer for rendering.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
apply_index_buffer(IndexBufferContext *ibc,
                   const GeomPrimitivePipelineReader *reader, bool force) {
  DXIndexBufferContext9 *dibc = DCAST(DXIndexBufferContext9, ibc);

  if (_lru) {
    _lru -> access_page (dibc -> _lru_page);
  }

  if (dibc->_ibuffer == NULL) {
    // Attempt to create a new index buffer.
    dibc->create_ibuffer(*_screen, reader);

    if (dibc->_ibuffer != NULL) {
      if (!dibc->upload_data(reader, force)) {
        return false;
      }
      dibc->mark_loaded(reader);

      _d3d_device->SetIndices(dibc->_ibuffer);
      _active_ibuffer = dibc;
      dibc->set_active(true);

    } else {
      _d3d_device->SetIndices(NULL);
      _active_ibuffer = NULL;
    }

  } else {
    if (dibc->was_modified(reader)) {
      if (dibc->changed_size(reader)) {
        // We have to destroy the old index buffer and create a new
        // one.
        dibc->create_ibuffer(*_screen, reader);
      }

      if (!dibc->upload_data(reader, force)) {
        return false;
      }

      dibc->mark_loaded(reader);
      _active_ibuffer = NULL;
    }

    if (_active_ibuffer != dibc) {
      _d3d_device->SetIndices(dibc->_ibuffer);
      _active_ibuffer = dibc;
      dibc->set_active(true);
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::release_index_buffer
//       Access: Public, Virtual
//  Description: Frees the GL resources previously allocated for the
//               data.  This function should never be called
//               directly; instead, call Data::release() (or simply
//               let the Data destruct).
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
release_index_buffer(IndexBufferContext *ibc) {
  DXIndexBufferContext9 *dibc = DCAST(DXIndexBufferContext9, ibc);
  delete dibc;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::begin_occlusion_query
//       Access: Public, Virtual
//  Description: Begins a new occlusion query.  After this call, you
//               may call begin_draw_primitives() and
//               draw_triangles()/draw_whatever() repeatedly.
//               Eventually, you should call end_occlusion_query()
//               before the end of the frame; that will return a new
//               OcclusionQueryContext object that will tell you how
//               many pixels represented by the bracketed geometry
//               passed the depth test.
//
//               It is not valid to call begin_occlusion_query()
//               between another begin_occlusion_query()
//               .. end_occlusion_query() sequence.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
begin_occlusion_query() {
  nassertv(_supports_occlusion_query);
  nassertv(_current_occlusion_query == (OcclusionQueryContext *)NULL);

  IDirect3DQuery9 *query;
  HRESULT hr = _d3d_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query);
  if (FAILED(hr)) {
    dxgsg9_cat.warning()
      << "Occlusion query failed.\n";
    return;
  }
  
  PT(DXOcclusionQueryContext9) queryobj = new DXOcclusionQueryContext9(query);

  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug()
      << "beginning occlusion query " << query << "\n";
  }

  query->Issue(D3DISSUE_BEGIN);
  _current_occlusion_query = queryobj;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::end_occlusion_query
//       Access: Public, Virtual
//  Description: Ends a previous call to begin_occlusion_query().
//               This call returns the OcclusionQueryContext object
//               that will (eventually) report the number of pixels
//               that passed the depth test between the call to
//               begin_occlusion_query() and end_occlusion_query().
////////////////////////////////////////////////////////////////////
PT(OcclusionQueryContext) DXGraphicsStateGuardian9::
end_occlusion_query() {
  if (_current_occlusion_query == (OcclusionQueryContext *)NULL) {
    return NULL;
  }

  PT(OcclusionQueryContext) result = _current_occlusion_query;

  IDirect3DQuery9 *query = DCAST(DXOcclusionQueryContext9, result)->_query;
    
  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug()
      << "ending occlusion query " << query << "\n";
  }

  _current_occlusion_query = NULL;
  query->Issue(D3DISSUE_END);

  return result;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::make_geom_munger
//       Access: Public, Virtual
//  Description: Creates a new GeomMunger object to munge vertices
//               appropriate to this GSG for the indicated state.
////////////////////////////////////////////////////////////////////
PT(GeomMunger) DXGraphicsStateGuardian9::
make_geom_munger(const RenderState *state, Thread *current_thread) {
  PT(DXGeomMunger9) munger = new DXGeomMunger9(this, state);
  return GeomMunger::register_munger(munger, current_thread);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_color_clear_value
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_color_clear_value(const Colorf& value) {
  _color_clear_value = value;
  _d3dcolor_clear_value =  Colorf_to_D3DCOLOR(value);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_clear
//       Access: Public, Virtual
//  Description: Clears all of the indicated buffers to their assigned
//               colors.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_clear(const RenderBuffer &buffer) {
  nassertv(buffer._gsg == this);
  int buffer_type = buffer._buffer_type;

  DWORD main_flags = 0;
  DWORD aux_flags = 0;

  DBG_S dxgsg9_cat.debug ( ) << "DXGraphicsStateGuardian9::do_clear\n"; DBG_E

  //set appropriate flags
  if (buffer_type & RenderBuffer::T_color) {
    main_flags |=  D3DCLEAR_TARGET;
  }

  if (buffer_type & RenderBuffer::T_depth) {
    aux_flags |=  D3DCLEAR_ZBUFFER;
    nassertv(_screen->_presentation_params.EnableAutoDepthStencil);
  }

  if (buffer_type & RenderBuffer::T_stencil) {
    // clear only if there is a stencil buffer
    if (_screen->_presentation_params.EnableAutoDepthStencil &&
      IS_STENCIL_FORMAT(_screen->_presentation_params.AutoDepthStencilFormat)) {
      aux_flags |=  D3DCLEAR_STENCIL;

      // DEBUG
      if (false) {
        dxgsg9_cat.debug ( ) << "STENCIL CLEAR " << _stencil_clear_value << "\n";
      }
    }
  }

  if ((main_flags | aux_flags) != 0) {

  DBG_S dxgsg9_cat.debug ( ) << "ccccc DXGraphicsStateGuardian9::really do_clear\n"; DBG_E
  DBG_S dxgsg9_cat.debug ( ) << "clear flags: main " << main_flags << " aux :" << aux_flags << "\n"; DBG_E

    HRESULT hr = _d3d_device->Clear(0, NULL, main_flags | aux_flags, _d3dcolor_clear_value,
                                    _depth_clear_value, (DWORD)_stencil_clear_value);
    if (FAILED(hr) && main_flags == D3DCLEAR_TARGET && aux_flags != 0) {
      // Maybe there's a problem with the one or more of the auxiliary
      // buffers.
      hr = _d3d_device->Clear(0, NULL, D3DCLEAR_TARGET, _d3dcolor_clear_value,
                              _depth_clear_value, (DWORD)_stencil_clear_value);
      if (!FAILED(hr)) {
        // Yep, it worked without them.  That's a problem.  Which buffer
        // poses the problem?
        if (buffer_type & RenderBuffer::T_depth) {
          aux_flags |=  D3DCLEAR_ZBUFFER;
          HRESULT hr2 = _d3d_device->Clear(0, NULL, D3DCLEAR_ZBUFFER, _d3dcolor_clear_value,
                                           _depth_clear_value, (DWORD)_stencil_clear_value);
          if (FAILED(hr2)) {
            dxgsg9_cat.error()
              << "Unable to clear depth buffer; removing.\n";
            // This is really hacky code.
            ((FrameBufferProperties *)_current_properties)->set_depth_bits(0);
          }
        }
        if (buffer_type & RenderBuffer::T_stencil) {
          aux_flags |=  D3DCLEAR_STENCIL;
          HRESULT hr2 = _d3d_device->Clear(0, NULL, D3DCLEAR_STENCIL, _d3dcolor_clear_value,
                                           _stencil_clear_value, (DWORD)_stencil_clear_value);
          if (FAILED(hr2)) {
            dxgsg9_cat.error()
              << "Unable to clear stencil buffer; removing.\n";
            // This is really hacky code.
            ((FrameBufferProperties *)_current_properties)->set_stencil_bits(0);
          }
        }
      }
    }

    if (FAILED(hr)) {
      dxgsg9_cat.error()
        << "clear_buffer failed:  Clear returned " << D3DERRORSTRING(hr);
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::prepare_display_region
//       Access: Public, Virtual
//  Description: Prepare a display region for rendering (set up
//       scissor region and viewport)
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
prepare_display_region(DisplayRegionPipelineReader *dr,
                       Lens::StereoChannel stereo_channel) {
  nassertv(dr != (DisplayRegionPipelineReader *)NULL);
  GraphicsStateGuardian::prepare_display_region(dr, stereo_channel);

// DBG_S dxgsg9_cat.debug ( ) << "DXGraphicsStateGuardian9::PRE prepare_display_region\n"; DBG_E

// DBG_S dxgsg9_cat.debug ( ) << "DXGraphicsStateGuardian9::prepare_display_region\n"; DBG_E

  int l, u, w, h;
  dr->get_region_pixels_i(l, u, w, h);

  DBG_S dxgsg9_cat.debug ( ) << "display_region " << l << " " << u << " "  << w << " "  << h << "\n"; DBG_E;

  // Create the viewport
  D3DVIEWPORT9 vp = { l, u, w, h, 0.0f, 1.0f };
  _current_viewport = vp;
  HRESULT hr = _d3d_device->SetViewport(&_current_viewport);
  if (FAILED(hr)) {
    dxgsg9_cat.error()
      << "_screen->_swap_chain = " << _screen->_swap_chain << " _swap_chain = " << _swap_chain << "\n";
    dxgsg9_cat.error()
      << "SetViewport(" << l << ", " << u << ", " << w << ", " << h
      << ") failed" << D3DERRORSTRING(hr);

    D3DVIEWPORT9 vp_old;
    _d3d_device->GetViewport(&vp_old);
    dxgsg9_cat.error()
      << "GetViewport(" << vp_old.X << ", " << vp_old.Y << ", " << vp_old.Width << ", "
      << vp_old.Height << ") returned: Trying to set that vp---->\n";
    hr = _d3d_device->SetViewport(&vp_old);

    if (FAILED(hr)) {
      dxgsg9_cat.error() << "Failed again\n";
      throw_event("panda3d-render-error");
      nassertv(false);
    }
  }

  if (_screen->_can_direct_disable_color_writes) {
    set_render_state(D3DRS_COLORWRITEENABLE, _color_write_mask);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::calc_projection_mat
//       Access: Public, Virtual
//  Description: Given a lens, calculates the appropriate projection
//               matrix for use with this gsg.  Note that the
//               projection matrix depends a lot upon the coordinate
//               system of the rendering API.
//
//               The return value is a TransformState if the lens is
//               acceptable, NULL if it is not.
////////////////////////////////////////////////////////////////////
CPT(TransformState) DXGraphicsStateGuardian9::
calc_projection_mat(const Lens *lens) {
  if (lens == (Lens *)NULL) {
    return NULL;
  }

  if (!lens->is_linear()) {
    return NULL;
  }

  // DirectX also uses a Z range of 0 to 1, whereas the Panda
  // convention is for the projection matrix to produce a Z range of
  // -1 to 1.  We have to rescale to compensate.
  static const LMatrix4f rescale_mat
    (1, 0, 0, 0,
     0, 1, 0, 0,
     0, 0, 0.5, 0,
     0, 0, 0.5, 1);

  LMatrix4f result =
    LMatrix4f::convert_mat(CS_yup_left, _current_lens->get_coordinate_system()) *
    lens->get_projection_mat(_current_stereo_channel) *
    rescale_mat;

  if (_scene_setup->get_inverted()) {
    // If the scene is supposed to be inverted, then invert the
    // projection matrix.
    result *= LMatrix4f::scale_mat(1.0f, -1.0f, 1.0f);
  }

  return TransformState::make_mat(result);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::prepare_lens
//       Access: Public, Virtual
//  Description: Makes the current lens (whichever lens was most
//               recently specified with set_scene()) active, so
//               that it will transform future rendered geometry.
//               Normally this is only called from the draw process,
//               and usually it is called by set_scene().
//
//               The return value is true if the lens is acceptable,
//               false if it is not.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
prepare_lens() {
  HRESULT hr =
    _d3d_device->SetTransform(D3DTS_PROJECTION,
                              (D3DMATRIX*)_projection_mat->get_mat().get_data());
  return SUCCEEDED(hr);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::begin_frame
//       Access: Public, Virtual
//  Description: Called before each frame is rendered, to allow the
//               GSG a chance to do any internal cleanup before
//               beginning the frame.
//
//               The return value is true if successful (in which case
//               the frame will be drawn and end_frame() will be
//               called later), or false if unsuccessful (in which
//               case nothing will be drawn and end_frame() will not
//               be called).
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
begin_frame(Thread *current_thread) {

  DBG_S dxgsg9_cat.debug ( ) << "^^^^^^^^^^^ begin_frame \n"; DBG_E

  GraphicsStateGuardian::begin_frame(current_thread);

  if (_d3d_device == NULL) {
    dxgsg9_cat.debug()
      << this << "::begin_frame(): no device.\n";
    return false;
  }

  if (_lru)
  {
    _lru -> begin_frame ( );
  }

  HRESULT hr = _d3d_device->BeginScene();

  if (FAILED(hr)) {
    if (hr == D3DERR_DEVICELOST) {
      if (dxgsg9_cat.is_debug()) {
        dxgsg9_cat.debug()
          << "BeginScene returns D3DERR_DEVICELOST" << endl;
      }

      check_cooperative_level();

    } else {
      dxgsg9_cat.error()
        << "BeginScene failed, unhandled error hr == "
        << D3DERRORSTRING(hr) << endl;
      throw_event("panda3d-render-error");
    }
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::begin_scene
//       Access: Public, Virtual
//  Description: Called between begin_frame() and end_frame() to mark
//               the beginning of drawing commands for a "scene"
//               (usually a particular DisplayRegion) within a frame.
//               All 3-D drawing commands, except the clear operation,
//               must be enclosed within begin_scene() .. end_scene().
//
//               The return value is true if successful (in which case
//               the scene will be drawn and end_scene() will be
//               called later), or false if unsuccessful (in which
//               case nothing will be drawn and end_scene() will not
//               be called).
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
begin_scene() {
  if (!GraphicsStateGuardian::begin_scene()) {
    return false;
  }

DBG_S dxgsg9_cat.debug ( ) << "DXGraphicsStateGuardian9::begin_scene\n"; DBG_E

/*
  HRESULT hr = _d3d_device->BeginScene();

  if (FAILED(hr)) {
    if (hr == D3DERR_DEVICELOST) {
      if (dxgsg9_cat.is_debug()) {
        dxgsg9_cat.debug()
          << "BeginScene returns D3DERR_DEVICELOST" << endl;
      }

      check_cooperative_level();

    } else {
      dxgsg9_cat.error()
        << "BeginScene failed, unhandled error hr == "
        << D3DERRORSTRING(hr) << endl;
      throw_event("panda3d-render-error");
    }
    return false;
  }
*/

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::end_scene
//       Access: Public, Virtual
//  Description: Called between begin_frame() and end_frame() to mark
//               the end of drawing commands for a "scene" (usually a
//               particular DisplayRegion) within a frame.  All 3-D
//               drawing commands, except the clear operation, must be
//               enclosed within begin_scene() .. end_scene().
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
end_scene() {
  GraphicsStateGuardian::end_scene();

DBG_S dxgsg9_cat.debug ( ) << "DXGraphicsStateGuardian9::end_scene\n"; DBG_E

/*
  HRESULT hr = _d3d_device->EndScene();

  if (FAILED(hr)) {
    if (hr == D3DERR_DEVICELOST) {
      if (dxgsg9_cat.is_debug()) {
        dxgsg9_cat.debug()
          << "EndScene returns DeviceLost\n";
      }
      check_cooperative_level();

    } else {
      dxgsg9_cat.error()
        << "EndScene failed, unhandled error hr == " << D3DERRORSTRING(hr);
      throw_event("panda3d-render-error");
    }
    return;
  }
*/

}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::end_frame
//       Access: Public, Virtual
//  Description: Called after each frame is rendered, to allow the
//               GSG a chance to do any internal cleanup after
//               rendering the frame, and before the window flips.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
end_frame(Thread *current_thread) {

DBG_S dxgsg9_cat.debug ( ) << "@@@@@@@@@@ end_frame \n"; DBG_E

  HRESULT hr = _d3d_device->EndScene();

  if (FAILED(hr)) {
    if (hr == D3DERR_DEVICELOST) {
      if (dxgsg9_cat.is_debug()) {
        dxgsg9_cat.debug()
          << "EndScene returns DeviceLost\n";
      }
      check_cooperative_level();

    } else {
      dxgsg9_cat.error()
        << "EndScene failed, unhandled error hr == " << D3DERRORSTRING(hr);
      throw_event("panda3d-render-error");
    }
    return;
  }

  if (_lru) {
    int frames;
    int maximum_updates;

    // LRU update
    maximum_updates = dx_lru_maximum_page_updates_per_frame;
    _lru -> partial_lru_update (maximum_updates);

    // LRU debug
    if (false && dxgsg9_cat.is_debug()) {
      dxgsg9_cat.debug() << "*  start_priority_index " << _lru -> _m.start_priority_index << "\n";
      dxgsg9_cat.debug() << "*  start_update_lru_page " << _lru -> _m.start_update_lru_page << "\n";
    }

    frames = dx_lru_debug_frames_til_output;
    if (dx_lru_debug && frames > 0 && (_lru -> _m.current_frame_identifier % frames) == 0) {
      if (dxgsg9_cat.is_error()) {
        UINT available_texture_memory;

        available_texture_memory = _d3d_device->GetAvailableTextureMem ( );

        dxgsg9_cat.error() << "* LRU: total_pages " << _lru -> _m.total_pages << "/" << _lru -> _m.maximum_pages << " upf " << dx_lru_maximum_page_updates_per_frame << " fto " << dx_lru_debug_frames_til_output << "\n";
        dxgsg9_cat.error() << "*  DX available_texture_memory = " << available_texture_memory << "\n";
        dxgsg9_cat.error() << "*  delta_memory " << _available_texture_memory - (available_texture_memory + (_lru -> _m.maximum_memory - _lru -> _m.available_memory)) << "\n";
        dxgsg9_cat.error() << "*  available_memory " << _lru -> _m.available_memory << "/" << _lru -> _m.maximum_memory << "\n";
        dxgsg9_cat.error() << "*  total lifetime pages created " << _lru -> _m.identifier << "\n";
        dxgsg9_cat.error() << "*  total_lifetime_page_ins " << _lru -> _m.total_lifetime_page_ins << "\n";
        dxgsg9_cat.error() << "*  total_lifetime_page_outs " << _lru -> _m.total_lifetime_page_outs << "\n";
        dxgsg9_cat.error() << "*  total_page_access " << _lru -> _m.total_page_access << " avg page access " << ((float) _lru -> _m.total_page_access / (float) frames) << "\n";
        dxgsg9_cat.error() << "*  total_lru_pages_in_pool " << _lru -> _m.total_lru_pages_in_pool << "\n";
        dxgsg9_cat.error() << "*  total_lru_pages_in_free_pool " << _lru -> _m.total_lru_pages_in_free_pool << "\n";
        dxgsg9_cat.error() << "*  avg unique page access size " << (_lru -> _m.total_page_access_size / (double) frames) << "\n";
        dxgsg9_cat.error() << "*  avg of all page access size " << ((_lru -> _m.total_page_access_size + _lru -> _m.total_page_all_access_size) / (double) frames) << "\n";

        _lru -> _m.total_page_access = 0;
        _lru -> _m.total_page_access_size = 0;
        _lru -> _m.total_page_all_access_size = 0;

        _lru -> count_priority_level_pages ( );

        int index;

        for (index = 0; index < LPP_TotalPriorities; index++) {
          if (_lru -> _m.lru_page_count_array [index]) {
            dxgsg9_cat.error() << "*  priority " << index << " pages " << _lru -> _m.lru_page_count_array [index] << "\n";
          }
        }

        _lru -> calculate_lru_statistics ( );

        for (index = 0; index < _lru -> _m.maximum_page_types; index++) {
          PageTypeStatistics *page_type_statistics;

          page_type_statistics = &_lru -> _m.page_type_statistics_array [index];
          dxgsg9_cat.error() << "\n" <<
              " page type " << index <<
              "  total pages " << page_type_statistics -> total_pages <<
              "  in " << page_type_statistics -> total_pages_in <<
              "  size " << page_type_statistics -> total_memory_in <<
              "  out " << page_type_statistics -> total_pages_out <<
              "  size " << page_type_statistics -> total_memory_out <<
              "\n";

        }
      }
    }
  }

#if defined(DO_PSTATS)
  if (_texmgrmem_total_pcollector.is_active()) {
#define TICKS_PER_GETTEXINFO (2.5*1000)   // 2.5 second interval
    static DWORD last_tick_count = 0;
    DWORD cur_tick_count = GetTickCount();

    if (cur_tick_count - last_tick_count > TICKS_PER_GETTEXINFO) {
      last_tick_count = cur_tick_count;
      report_texmgr_stats();
    }
  }
#endif

  // Note: regular GraphicsWindow::end_frame is being called,
  // but we override gsg::end_frame, so need to explicitly call it here
  // (currently it's an empty fn)
  GraphicsStateGuardian::end_frame(current_thread);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::begin_draw_primitives
//       Access: Public, Virtual
//  Description: Called before a sequence of draw_primitive()
//               functions are called, this should prepare the vertex
//               data for rendering.  It returns true if the vertices
//               are ok, false to abort this group of primitives.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
begin_draw_primitives(const GeomPipelineReader *geom_reader,
                      const GeomMunger *munger,
                      const GeomVertexDataPipelineReader *data_reader,
                      bool force) {
  if (!GraphicsStateGuardian::begin_draw_primitives(geom_reader, munger,
                                                    data_reader, force)) {
    return false;
  }
  nassertr(_data_reader != (GeomVertexDataPipelineReader *)NULL, false);

  DBG_SH5 dxgsg9_cat.debug ( ) << "begin_draw_primitives\n"; DBG_E

// SHADER
  if (_vertex_array_shader_context==0) {
    if (_current_shader_context==0) {
//      ?????       update_standard_vertex_arrays();
    } else {
//      ?????       disable_standard_vertex_arrays();
      _current_shader_context->update_shader_vertex_arrays(NULL,this);
    }
  } else {
    if (_current_shader_context==0) {
      _vertex_array_shader_context->disable_shader_vertex_arrays(this);
//      ?????       update_standard_vertex_arrays();
    } else {
      _current_shader_context->
        update_shader_vertex_arrays(_vertex_array_shader_context,this);
    }
  }
  _vertex_array_shader = _current_shader;
  _vertex_array_shader_context = _current_shader_context;

  const GeomVertexFormat *format = _data_reader->get_format ( );
  const GeomVertexArrayDataHandle *data;
  int number_of_arrays = _data_reader -> get_num_arrays ( );

  if (_current_shader_context && number_of_arrays > 1) {

    // find a matching vertex format for the vertex shader's input if possible
    VertexElementArray *vertex_element_array;

    vertex_element_array = _current_shader_context -> _vertex_element_array;
    if (vertex_element_array)
    {
      bool match;
      bool multiple_matches;
      int index;
      int first_index;

      match = false;
      multiple_matches = false;
      first_index = -1;

      // quick check for a match
      // find the one array with the minimum number of elements if possible
      {
        for (index = 0; index < number_of_arrays; index++) {
          data = _data_reader -> get_array_reader (index);

          const GeomVertexArrayFormat *array_format = data->get_array_format();
          int number_of_columns = array_format->get_num_columns();

          DBG_VEA
            dxgsg9_cat.debug ( )
              << " data index " << index
              << " number_of_columns " << number_of_columns
              << " vertex_element_array -> total_elements " << vertex_element_array -> total_elements
              << "\n";
          DBG_E

          if (number_of_columns >= vertex_element_array -> total_elements) {
            if (first_index >= 0) {
              multiple_matches = true;
            }
            else {
              first_index = index;
            }
          }
        }
      }

      if (multiple_matches)
      {
        // ugh slow, need to find which one
        for (index = first_index; index < number_of_arrays; index++)
        {
          data = _data_reader -> get_array_reader (index);

          const GeomVertexArrayFormat *array_format = data->get_array_format();
          int number_of_columns = array_format->get_num_columns();

          if (number_of_columns >= vertex_element_array -> total_elements)
          {

// check not implemented yet
dxgsg9_cat.error ( ) << "vertex_element_type_array check not implemented yet\n";

// build a vertex_element_type_array from data

// compare both vertex_element_type_array for a match
vertex_element_array -> vertex_element_type_array;


          }
        }

        // since the check is not implemented yet use first_index for now
        data = _data_reader -> get_array_reader (first_index);

        match = true;
      }
      else
      {
        if (first_index >= 0) {
          data = _data_reader -> get_array_reader (first_index);
          match = true;
        }
      }

      if (match) {

      }
      else {
        // ERROR
        dxgsg9_cat.error ( ) << "could not find matching vertex element data for vertex shader\n";

        // just use the 0 array
        data = _data_reader->get_array_reader(0);
      }
    }
    else {
      // ERROR
      dxgsg9_cat.error ( ) << "_current_shader_context -> _vertex_element_array == 0\n";
    }
  }
  else {
    // The munger should have put the FVF data in the first array.
    data = _data_reader->get_array_reader(0);
  }

  VertexBufferContext *vbc = ((GeomVertexArrayData *)(data->get_object()))->prepare_now(get_prepared_objects(), this);
  nassertr(vbc != (VertexBufferContext *)NULL, false);
  if (!apply_vertex_buffer(vbc, _current_shader_context, data, force)) {
    return false;
  }

  const GeomVertexAnimationSpec &animation =
    data_reader->get_format()->get_animation();
  if (animation.get_animation_type() == Geom::AT_hardware) {
    // Set up vertex blending.
    switch (animation.get_num_transforms()) {
    case 1:
      // The MSDN docs suggest we should use D3DVBF_0WEIGHTS here, but
      // that doesn't seem to work at all.  On the other hand,
      // D3DVBF_DISABLE *does* work, because it disables special
      // handling, meaning only the world matrix affects these
      // vertices--and by accident or design, the first matrix,
      // D3DTS_WORLDMATRIX(0), *is* the world matrix.
      set_render_state(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
      break;
    case 2:
      set_render_state(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
      break;
    case 3:
      set_render_state(D3DRS_VERTEXBLEND, D3DVBF_2WEIGHTS);
      break;
    case 4:
      set_render_state(D3DRS_VERTEXBLEND, D3DVBF_3WEIGHTS);
      break;
    }

    if (animation.get_indexed_transforms()) {
      // Set up indexed vertex blending.
      set_render_state(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
    } else {
      set_render_state(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    }

    const TransformTable *table = data_reader->get_transform_table();
    if (table != (TransformTable *)NULL) {
      for (int i = 0; i < table->get_num_transforms(); i++) {
        LMatrix4f mat;
        table->get_transform(i)->mult_matrix(mat, _internal_transform->get_mat());
        const D3DMATRIX *d3d_mat = (const D3DMATRIX *)mat.get_data();
        _d3d_device->SetTransform(D3DTS_WORLDMATRIX(i), d3d_mat);
      }

      // Setting the first animation matrix steps on the world matrix,
      // so we have to set a flag to reload the world matrix later.
      _transform_stale = true;
    }
    _vertex_blending_enabled = true;

  } else {
    // We're not using vertex blending.
    if (_vertex_blending_enabled) {
      set_render_state(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
      set_render_state(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
      _vertex_blending_enabled = false;
    }

    if (_transform_stale && !_data_reader->is_vertex_transformed()) {
      const D3DMATRIX *d3d_mat = (const D3DMATRIX *)_internal_transform->get_mat().get_data();
      _d3d_device->SetTransform(D3DTS_WORLD, d3d_mat);
      _transform_stale = false;
    }
  }

  if (_data_reader->is_vertex_transformed()) {
    // If the vertex data claims to be already transformed into clip
    // coordinates, wipe out the current projection and modelview
    // matrix (so we don't attempt to transform it again).

    // It's tempting just to use the D3DFVF_XYZRHW specification on
    // these vertices, but that turns out to be a bigger hammer than
    // we want: that also prevents lighting calculations and user clip
    // planes.
    _d3d_device->SetTransform(D3DTS_WORLD, &_d3d_ident_mat);
    static const LMatrix4f rescale_mat
      (1, 0, 0, 0,
       0, 1, 0, 0,
       0, 0, 0.5, 0,
       0, 0, 0.5, 1);
    _transform_stale = true;

    _d3d_device->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX *)rescale_mat.get_data());
  }

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_triangles
//       Access: Public, Virtual
//  Description: Draws a series of disconnected triangles.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
draw_triangles(const GeomPrimitivePipelineReader *reader, bool force) {
  PStatTimer timer(_draw_primitive_pcollector);

//  DBG_SH5 dxgsg9_cat.debug ( ) << "draw_triangles 1\n"; DBG_E

  _vertices_tri_pcollector.add_level(reader->get_num_vertices());
  _primitive_batches_tri_pcollector.add_level(1);
  if (reader->is_indexed()) {
    int min_vertex = dx_broken_max_index ? 0 : reader->get_min_vertex();
    int max_vertex = reader->get_max_vertex();

    if (_active_vbuffer != NULL) {
      // Indexed, vbuffers.
      IndexBufferContext *ibc = ((GeomPrimitive *)(reader->get_object()))->prepare_now(get_prepared_objects(), this);
      nassertr(ibc != (IndexBufferContext *)NULL, false);
      if (!apply_index_buffer(ibc, reader, force)) {
        return false;
      }

//DBG_SH2 dxgsg9_cat.debug ( ) << "DrawIndexedPrimitive \n"; DBG_E

      _d3d_device->DrawIndexedPrimitive
        (D3DPT_TRIANGLELIST, 0,
         min_vertex, max_vertex - min_vertex + 1,
         0, reader->get_num_primitives());

    } else {
      // Indexed, client arrays.

//DBG_SH2 dxgsg9_cat.debug ( ) << "draw_indexed_primitive_up \n"; DBG_E

      const unsigned char *index_pointer = reader->get_read_pointer(force);
      if (index_pointer == NULL) {
        return false;
      }
      D3DFORMAT index_type = get_index_type(reader->get_index_type());
      const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
      if (vertex_pointer == NULL) {
        return false;
      }

      draw_indexed_primitive_up
        (D3DPT_TRIANGLELIST,
         min_vertex, max_vertex,
         reader->get_num_primitives(),
         index_pointer, index_type, vertex_pointer,
         _data_reader->get_format()->get_array(0)->get_stride());
    }
  } else {
    if (_active_vbuffer != NULL) {
      // Nonindexed, vbuffers.

//DBG_SH2 dxgsg9_cat.debug ( ) << "DrawPrimitive D3DPT_TRIANGLELIST \n"; DBG_E

      _d3d_device->DrawPrimitive
        (D3DPT_TRIANGLELIST,
         reader->get_first_vertex(),
         reader->get_num_primitives());

    } else {
      // Nonindexed, client arrays.

//DBG_SH2 dxgsg9_cat.debug ( ) << "draw_primitive_up \n"; DBG_E
      const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
      if (vertex_pointer == NULL) {
        return false;
      }

      draw_primitive_up(D3DPT_TRIANGLELIST, reader->get_num_primitives(),
      reader->get_first_vertex(),
      reader->get_num_vertices(), vertex_pointer,
      _data_reader->get_format()->get_array(0)->get_stride());
    }
  }

//  DBG_SH5 dxgsg9_cat.debug ( ) << "end draw_triangles 1\n"; DBG_E
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_tristrips
//       Access: Public, Virtual
//  Description: Draws a series of triangle strips.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
draw_tristrips(const GeomPrimitivePipelineReader *reader, bool force) {
  PStatTimer timer(_draw_primitive_pcollector);

  DBG_SH5 dxgsg9_cat.debug ( ) << "draw_tristrips\n"; DBG_E

  if (connect_triangle_strips && _current_fill_mode != RenderModeAttrib::M_wireframe) {
    // One long triangle strip, connected by the degenerate vertices
    // that have already been set up within the primitive.
    _vertices_tristrip_pcollector.add_level(reader->get_num_vertices());
    _primitive_batches_tristrip_pcollector.add_level(1);
    if (reader->is_indexed()) {
      int min_vertex = dx_broken_max_index ? 0 : reader->get_min_vertex();
      int max_vertex = reader->get_max_vertex();

      if (_active_vbuffer != NULL) {
        // Indexed, vbuffers, one line triangle strip.
        IndexBufferContext *ibc = ((GeomPrimitive *)(reader->get_object()))->prepare_now(get_prepared_objects(), this);
        nassertr(ibc != (IndexBufferContext *)NULL, false);
        if (!apply_index_buffer(ibc, reader, force)) {
          return false;
        }

//dxgsg9_cat.error ( ) << "DrawIndexedPrimitive D3DPT_TRIANGLESTRIP VERTICES: " << reader->get_num_vertices ( ) << "\n";

        _d3d_device->DrawIndexedPrimitive
          (D3DPT_TRIANGLESTRIP, 0,
           min_vertex, max_vertex - min_vertex + 1,
           0, reader->get_num_vertices() - 2);

      } else {

//dxgsg9_cat.error ( ) << "draw_indexed_primitive_up D3DPT_TRIANGLESTRIP VERTICES: " << reader->get_num_vertices ( ) << "\n";

        // Indexed, client arrays, one long triangle strip.
        const unsigned char *index_pointer = reader->get_read_pointer(force);
        if (index_pointer == NULL) {
          return false;
        }
        D3DFORMAT index_type = get_index_type(reader->get_index_type());
        const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
        if (vertex_pointer == NULL) {
          return false;
        }

        draw_indexed_primitive_up
          (D3DPT_TRIANGLESTRIP,
           min_vertex, max_vertex,
           reader->get_num_vertices() - 2,
           index_pointer, index_type, vertex_pointer,
           _data_reader->get_format()->get_array(0)->get_stride());
      }
    } else {
      if (_active_vbuffer != NULL) {
        // Nonindexed, vbuffers, one long triangle strip.

//dxgsg9_cat.error ( ) << "DrawPrimitive D3DPT_TRIANGLESTRIP " << reader->get_first_vertex ( ) << " VERTICES: " << reader->get_num_vertices ( ) << "\n";

        _d3d_device->DrawPrimitive
          (D3DPT_TRIANGLESTRIP,
           reader->get_first_vertex(),
           reader->get_num_vertices() - 2);

      } else {
        // Indexed, client arrays, one long triangle strip.
        const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
        if (vertex_pointer == NULL) {
          return false;
        }
        draw_primitive_up(D3DPT_TRIANGLESTRIP,
        reader->get_num_vertices() - 2,
        reader->get_first_vertex(),
        reader->get_num_vertices(), vertex_pointer,
        _data_reader->get_format()->get_array(0)->get_stride());
      }
    }

  } else {
    // Send the individual triangle strips, stepping over the
    // degenerate vertices.
    CPTA_int ends = reader->get_ends();
    _primitive_batches_tristrip_pcollector.add_level(ends.size());

    if (reader->is_indexed()) {
      CPTA_int ends = reader->get_ends();
      int index_stride = reader->get_index_stride();
      _primitive_batches_tristrip_pcollector.add_level(ends.size());

      GeomVertexReader mins(reader->get_mins(), 0);
      GeomVertexReader maxs(reader->get_maxs(), 0);
      nassertr(reader->get_mins()->get_num_rows() == (int)ends.size() &&
               reader->get_maxs()->get_num_rows() == (int)ends.size(), false);

      if (_active_vbuffer != NULL) {
        // Indexed, vbuffers, individual triangle strips.
        IndexBufferContext *ibc = ((GeomPrimitive *)(reader->get_object()))->prepare_now(get_prepared_objects(), this);
        nassertr(ibc != (IndexBufferContext *)NULL, false);
        if (!apply_index_buffer(ibc, reader, force)) {
          return false;
        }

        unsigned int start = 0;
        for (size_t i = 0; i < ends.size(); i++) {
          _vertices_tristrip_pcollector.add_level(ends[i] - start);
          unsigned int min = mins.get_data1i();
          unsigned int max = maxs.get_data1i();
          _d3d_device->DrawIndexedPrimitive
            (D3DPT_TRIANGLESTRIP,
             0,
             min, max - min + 1,
             start, ends[i] - start - 2);

          start = ends[i] + 2;
        }

      } else {
        // Indexed, client arrays, individual triangle strips.
        int stride = _data_reader->get_format()->get_array(0)->get_stride();
        const unsigned char *index_pointer = reader->get_read_pointer(force);
        if (index_pointer == NULL) {
          return false;
        }
        D3DFORMAT index_type = get_index_type(reader->get_index_type());
        const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
        if (vertex_pointer == NULL) {
          return false;
        }

        unsigned int start = 0;
        for (size_t i = 0; i < ends.size(); i++) {
          _vertices_tristrip_pcollector.add_level(ends[i] - start);
          unsigned int min = mins.get_data1i();
          unsigned int max = maxs.get_data1i();
          draw_indexed_primitive_up
            (D3DPT_TRIANGLESTRIP,
             min, max,
             ends[i] - start - 2,
             index_pointer + start * index_stride, index_type,
             vertex_pointer, stride);

          start = ends[i] + 2;
        }
      }
    } else {
      unsigned int first_vertex = reader->get_first_vertex();

      if (_active_vbuffer != NULL) {
        // Nonindexed, vbuffers, individual triangle strips.
        unsigned int start = 0;
        for (size_t i = 0; i < ends.size(); i++) {
          _vertices_tristrip_pcollector.add_level(ends[i] - start);
          _d3d_device->DrawPrimitive
            (D3DPT_TRIANGLESTRIP,
             first_vertex + start, ends[i] - start - 2);

          start = ends[i] + 2;
        }

      } else {
        // Nonindexed, client arrays, individual triangle strips.
        const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
        if (vertex_pointer == NULL) {
          return false;
        }
        int stride = _data_reader->get_format()->get_array(0)->get_stride();

        unsigned int start = 0;
        for (size_t i = 0; i < ends.size(); i++) {
          _vertices_tristrip_pcollector.add_level(ends[i] - start);
          draw_primitive_up(D3DPT_TRIANGLESTRIP, ends[i] - start - 2,
          first_vertex + start,
          ends[i] - start,
          vertex_pointer, stride);

          start = ends[i] + 2;
        }
      }
    }
  }
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_trifans
//       Access: Public, Virtual
//  Description: Draws a series of triangle fans.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
draw_trifans(const GeomPrimitivePipelineReader *reader, bool force) {
  PStatTimer timer(_draw_primitive_pcollector);

  DBG_SH5 dxgsg9_cat.debug ( ) << "draw_trifans\n"; DBG_E

  CPTA_int ends = reader->get_ends();
  _primitive_batches_trifan_pcollector.add_level(ends.size());

  if (reader->is_indexed()) {
    int min_vertex = dx_broken_max_index ? 0 : reader->get_min_vertex();
    int max_vertex = reader->get_max_vertex();

    // Send the individual triangle fans.  There's no connecting fans
    // with degenerate vertices, so no worries about that.
    int index_stride = reader->get_index_stride();

    GeomVertexReader mins(reader->get_mins(), 0);
    GeomVertexReader maxs(reader->get_maxs(), 0);
    nassertr(reader->get_mins()->get_num_rows() == (int)ends.size() &&
             reader->get_maxs()->get_num_rows() == (int)ends.size(), false);

    if (_active_vbuffer != NULL) {
      // Indexed, vbuffers.
      IndexBufferContext *ibc = ((GeomPrimitive *)(reader->get_object()))->prepare_now(get_prepared_objects(), this);
      nassertr(ibc != (IndexBufferContext *)NULL, false);
      if (!apply_index_buffer(ibc, reader, force)) {
        return false;
      }

      unsigned int start = 0;
      for (size_t i = 0; i < ends.size(); i++) {
        _vertices_trifan_pcollector.add_level(ends[i] - start);
        unsigned int min = mins.get_data1i();
        unsigned int max = maxs.get_data1i();
        _d3d_device->DrawIndexedPrimitive
          (D3DPT_TRIANGLEFAN, 0,
           min, max - min + 1,
           start, ends[i] - start - 2);

        start = ends[i];
      }

    } else {
      // Indexed, client arrays.
      int stride = _data_reader->get_format()->get_array(0)->get_stride();
      const unsigned char *index_pointer = reader->get_read_pointer(force);
      if (index_pointer == NULL) {
        return false;
      }
      D3DFORMAT index_type = get_index_type(reader->get_index_type());
      const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
      if (vertex_pointer == NULL) {
        return false;
      }

      unsigned int start = 0;
      for (size_t i = 0; i < ends.size(); i++) {
        _vertices_trifan_pcollector.add_level(ends[i] - start);
        unsigned int min = mins.get_data1i();
        unsigned int max = maxs.get_data1i();
        draw_indexed_primitive_up
          (D3DPT_TRIANGLEFAN,
           min, max,
           ends[i] - start - 2,
           index_pointer + start * index_stride, index_type,
           vertex_pointer, stride);

        start = ends[i];
      }
    }
  } else {
    unsigned int first_vertex = reader->get_first_vertex();

    if (_active_vbuffer != NULL) {
      // Nonindexed, vbuffers.
      unsigned int start = 0;
      for (size_t i = 0; i < ends.size(); i++) {
        _vertices_trifan_pcollector.add_level(ends[i] - start);
        _d3d_device->DrawPrimitive
          (D3DPT_TRIANGLEFAN,
           first_vertex + start, ends[i] - start - 2);

        start = ends[i];
      }

    } else {
      // Nonindexed, client arrays.
      const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
      if (vertex_pointer == NULL) {
        return false;
      }
      int stride = _data_reader->get_format()->get_array(0)->get_stride();

      unsigned int start = 0;
      for (size_t i = 0; i < ends.size(); i++) {
        _vertices_trifan_pcollector.add_level(ends[i] - start);
        draw_primitive_up(D3DPT_TRIANGLEFAN,
        ends[i] - start - 2,
        first_vertex,
        ends[i] - start,
        vertex_pointer, stride);
        start = ends[i];
      }
    }
  }
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_lines
//       Access: Public, Virtual
//  Description: Draws a series of disconnected line segments.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
draw_lines(const GeomPrimitivePipelineReader *reader, bool force) {
  PStatTimer timer(_draw_primitive_pcollector);
  _vertices_other_pcollector.add_level(reader->get_num_vertices());
  _primitive_batches_other_pcollector.add_level(1);

  if (reader->is_indexed()) {
    int min_vertex = dx_broken_max_index ? 0 : reader->get_min_vertex();
    int max_vertex = reader->get_max_vertex();

    if (_active_vbuffer != NULL) {
      // Indexed, vbuffers.
      IndexBufferContext *ibc = ((GeomPrimitive *)(reader->get_object()))->prepare_now(get_prepared_objects(), this);
      nassertr(ibc != (IndexBufferContext *)NULL, false);
      if (!apply_index_buffer(ibc, reader, force)) {
        return false;
      }

      _d3d_device->DrawIndexedPrimitive
        (D3DPT_LINELIST,
     0,
         min_vertex, max_vertex - min_vertex + 1,
         0, reader->get_num_primitives());

    } else {
      // Indexed, client arrays.
      const unsigned char *index_pointer = reader->get_read_pointer(force);
      if (index_pointer == NULL) {
        return false;
      }
      D3DFORMAT index_type = get_index_type(reader->get_index_type());
      const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
      if (vertex_pointer == NULL) {
        return false;
      }

      draw_indexed_primitive_up
        (D3DPT_LINELIST,
         min_vertex, max_vertex,
         reader->get_num_primitives(),
         index_pointer, index_type, vertex_pointer,
         _data_reader->get_format()->get_array(0)->get_stride());
    }
  } else {
    if (_active_vbuffer != NULL) {
      // Nonindexed, vbuffers.
      _d3d_device->DrawPrimitive
        (D3DPT_LINELIST,
         reader->get_first_vertex(),
         reader->get_num_primitives());

    } else {
      // Nonindexed, client arrays.
      const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
      if (vertex_pointer == NULL) {
        return false;
      }
      draw_primitive_up(D3DPT_LINELIST, reader->get_num_primitives(),
      reader->get_first_vertex(),
      reader->get_num_vertices(), vertex_pointer,
      _data_reader->get_format()->get_array(0)->get_stride());
    }
  }
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_linestrips
//       Access: Public, Virtual
//  Description: Draws a series of line strips.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
draw_linestrips(const GeomPrimitivePipelineReader *reader, bool force) {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_points
//       Access: Public, Virtual
//  Description: Draws a series of disconnected points.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
draw_points(const GeomPrimitivePipelineReader *reader, bool force) {
  PStatTimer timer(_draw_primitive_pcollector);
  _vertices_other_pcollector.add_level(reader->get_num_vertices());
  _primitive_batches_other_pcollector.add_level(1);

  // The munger should have protected us from indexed points--DirectX
  // doesn't support them.
  nassertr(!reader->is_indexed(), false);

  if (_active_vbuffer != NULL) {
    // Nonindexed, vbuffers.
    _d3d_device->DrawPrimitive
      (D3DPT_POINTLIST,
       reader->get_first_vertex(),
       reader->get_num_primitives());

  } else {
    // Nonindexed, client arrays.
    const unsigned char *vertex_pointer = _data_reader->get_array_reader(0)->get_read_pointer(force);
    if (vertex_pointer == NULL) {
      return false;
    }
    draw_primitive_up(D3DPT_POINTLIST, reader->get_num_primitives(),
                      reader->get_first_vertex(),
                      reader->get_num_vertices(), vertex_pointer,
                      _data_reader->get_format()->get_array(0)->get_stride());
  }
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::end_draw_primitives()
//       Access: Public, Virtual
//  Description: Called after a sequence of draw_primitive()
//               functions are called, this should do whatever cleanup
//               is appropriate.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
end_draw_primitives() {
  // Turn off vertex blending--it seems to cause problems if we leave
  // it on.
  if (_vertex_blending_enabled) {
    set_render_state(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    set_render_state(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    _vertex_blending_enabled = false;
  }

  if (_data_reader->is_vertex_transformed()) {
    // Restore the projection matrix that we wiped out above.
    _d3d_device->SetTransform(D3DTS_PROJECTION,
                              (D3DMATRIX*)_projection_mat->get_mat().get_data());
  }

  GraphicsStateGuardian::end_draw_primitives();
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::framebuffer_copy_to_texture
//       Access: Public, Virtual
//  Description: Copy the pixels within the indicated display
//               region from the framebuffer into texture memory.
//
//               If z > -1, it is the cube map index into which to
//               copy.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
framebuffer_copy_to_texture(Texture *tex, int z, const DisplayRegion *dr,
                            const RenderBuffer &rb) {
  set_read_buffer(rb);

  int orig_x = tex->get_x_size();
  int orig_y = tex->get_y_size();

  HRESULT hr;
  int xo, yo, w, h;
  dr->get_region_pixels_i(xo, yo, w, h);
  tex->set_x_size(Texture::up_to_power_2(w));
  tex->set_y_size(Texture::up_to_power_2(h));

  bool use_stretch_rect;

  use_stretch_rect = true;
  if (use_stretch_rect) {
    // must use a render target type texture for StretchRect
    tex -> set_render_to_texture (true);
  }

  TextureContext *tc = tex->prepare_now(get_prepared_objects(), this);
  if (tc == (TextureContext *)NULL) {
    return;
  }
  DXTextureContext9 *dtc = DCAST(DXTextureContext9, tc);

  if (tex->get_texture_type() != Texture::TT_2d_texture) {
    // For a specialty texture like a cube map, go the slow route
    // through RAM for now.
    framebuffer_copy_to_ram(tex, z, dr, rb);
    return;
  }
  nassertv(dtc->get_d3d_2d_texture() != NULL);

  IDirect3DSurface9 *tex_level_0;
  hr = dtc->get_d3d_2d_texture()->GetSurfaceLevel(0, &tex_level_0);
  if (FAILED(hr)) {
    dxgsg9_cat.error() << "GetSurfaceLev failed in copy_texture" << D3DERRORSTRING(hr);
    return;
  }

  // If the texture is the wrong size, we need to do something about it.
  D3DSURFACE_DESC texdesc;
  hr = tex_level_0->GetDesc(&texdesc);
  if (FAILED(hr)) {
    dxgsg9_cat.error() << "GetDesc failed in copy_texture" << D3DERRORSTRING(hr);
    SAFE_RELEASE(tex_level_0);
    return;
  }
  if ((texdesc.Width != tex->get_x_size())||(texdesc.Height != tex->get_y_size())) {
    if ((orig_x != tex->get_x_size()) || (orig_y != tex->get_y_size())) {
      // Texture might be wrong size because we resized it and need to recreate.
      SAFE_RELEASE(tex_level_0);
      if (!dtc->create_texture(*_screen)) {
        // Oops, we can't re-create the texture for some reason.
        dxgsg9_cat.error()
          << "Unable to re-create texture " << *dtc->get_texture() << endl;
        return;
      }
      hr = dtc->get_d3d_2d_texture()->GetSurfaceLevel(0, &tex_level_0);
      if (FAILED(hr)) {
        dxgsg9_cat.error() << "GetSurfaceLev failed in copy_texture" << D3DERRORSTRING(hr);
        return;
      }
      hr = tex_level_0->GetDesc(&texdesc);
      if (FAILED(hr)) {
        dxgsg9_cat.error() << "GetDesc 2 failed in copy_texture" << D3DERRORSTRING(hr);
        SAFE_RELEASE(tex_level_0);
        return;
      }
    }
    if ((texdesc.Width != tex->get_x_size())||(texdesc.Height != tex->get_y_size())) {
      // If it's still the wrong size, it's because driver can't create size
      // that we want.  In that case, there's no helping it, we have to give up.
      dxgsg9_cat.error()
        << "Unable to copy to texture, texture is wrong size: " << *dtc->get_texture() << endl;
      SAFE_RELEASE(tex_level_0);
      return;
    }
  }

  DWORD render_target_index;
  IDirect3DSurface9 *render_target;

  /* ***** DX9 GetRenderTarget, assume only one render target so index = 0 */
  render_target_index = 0;

  hr = _d3d_device->GetRenderTarget(render_target_index, &render_target);
  if (FAILED(hr)) {
    dxgsg9_cat.error()
      << "GetRenderTarget failed in framebuffer_copy_to_texture"
      << D3DERRORSTRING(hr);
    SAFE_RELEASE(tex_level_0);
    return;
  }

  RECT src_rect;

  src_rect.left = xo;
  src_rect.right = xo+w;
  src_rect.top = yo;
  src_rect.bottom = yo+h;

//  THE DX8 WAY
//  hr = _d3d_device->CopyRects(render_target, &src_rect, 1, tex_level_0, 0);

//  DX9
  if (use_stretch_rect) {
    D3DTEXTUREFILTERTYPE filter;

    filter = D3DTEXF_POINT;

//    dxgsg9_cat.debug () << "framebuffer_copy_to_texture\n";

    hr = _d3d_device->StretchRect (
      render_target,
      &src_rect,
      tex_level_0,
      &src_rect,
      filter);
    if (FAILED(hr)) {
      dxgsg9_cat.error()
        << "StretchRect failed in framebuffer_copy_to_texture"
        << D3DERRORSTRING(hr);
      return;
    }
  }
  else {
    //  this is not efficient since it copies the entire render
    //  target to system memory and then copies the rectangle to the texture
    // create a surface in system memory that is a duplicate of the render target
    D3DPOOL pool;
    IDirect3DSurface9 *temp_surface = NULL;
    D3DSURFACE_DESC surface_description;

    render_target -> GetDesc (&surface_description);

    pool = D3DPOOL_SYSTEMMEM;
    hr = _d3d_device->CreateOffscreenPlainSurface(
           surface_description.Width,
           surface_description.Height,
           surface_description.Format,
           pool,
           &temp_surface,
           NULL);
    if (FAILED(hr)) {
      dxgsg9_cat.error()
        << "CreateOffscreenPlainSurface failed in framebuffer_copy_to_texture"
        << D3DERRORSTRING(hr);
      return;
    }

    // this copies the entire render target into system memory
    hr = _d3d_device -> GetRenderTargetData (render_target, temp_surface);

    if (FAILED(hr)) {
      dxgsg9_cat.error() << "GetRenderTargetData failed" << D3DERRORSTRING(hr);
      RELEASE(temp_surface, dxgsg9, "temp_surface", RELEASE_ONCE);
      return;
    }

    // copy system memory version to texture
    DXTextureContext9::d3d_surface_to_texture(src_rect, temp_surface,
                false, tex, z);

    RELEASE(temp_surface, dxgsg9, "temp_surface", RELEASE_ONCE);
  }

  SAFE_RELEASE(render_target);
  SAFE_RELEASE(tex_level_0);
}


////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::framebuffer_copy_to_ram
//       Access: Public, Virtual
//  Description: Copy the pixels within the indicated display region
//               from the framebuffer into system memory, not texture
//               memory.  Returns true on success, false on failure.
//
//               This completely redefines the ram image of the
//               indicated texture.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
framebuffer_copy_to_ram(Texture *tex, int z, const DisplayRegion *dr, const RenderBuffer &rb) {
  set_read_buffer(rb);

  RECT rect;
  nassertr(tex != NULL && dr != NULL, false);

  int xo, yo, w, h;
  dr->get_region_pixels_i(xo, yo, w, h);

  Texture::Format format = tex->get_format();
  Texture::ComponentType component_type = tex->get_component_type();

  switch (format) {
  case Texture::F_depth_stencil:
    // Sorry, not (yet?) supported in pandadx.
    return false;

  default:
    format = Texture::F_rgb;
    component_type = Texture::T_unsigned_byte;
  }

  Texture::TextureType texture_type;
  if (z >= 0) {
    texture_type = Texture::TT_cube_map;
  } else {
    texture_type = Texture::TT_2d_texture;
  }

  if (tex->get_x_size() != w || tex->get_y_size() != h ||
      tex->get_component_type() != component_type ||
      tex->get_format() != format ||
      tex->get_texture_type() != texture_type) {
    // Re-setup the texture; its properties have changed.
    tex->setup_texture(texture_type, w, h, tex->get_z_size(),
                       component_type, format);
  }

  rect.top = yo;
  rect.left = xo;
  rect.right = xo + w;
  rect.bottom = yo + h;
  bool copy_inverted = false;

  IDirect3DSurface9 *temp_surface = NULL;
  HRESULT hr;

  // Note if you try to grab the backbuffer and full-screen
  // anti-aliasing is on, the backbuffer might be larger than the
  // window size.  For screenshots it's safer to get the front buffer.
  if (_cur_read_pixel_buffer & RenderBuffer::T_back) {
    DWORD render_target_index;
    IDirect3DSurface9 *backbuffer = NULL;
    // GetRenderTarget() seems to be a little more reliable than
    // GetBackBuffer().  Might just be related to the swap_chain
    // thing.

  render_target_index = 0;
    hr = _d3d_device->GetRenderTarget(render_target_index, &backbuffer);

    if (FAILED(hr)) {
      dxgsg9_cat.error() << "GetRenderTarget failed" << D3DERRORSTRING(hr);
      return false;
    }

    // Since we might not be able to Lock the back buffer, we will
    // need to copy it to a temporary surface of the appropriate type
    // first.
  D3DPOOL pool;
  D3DSURFACE_DESC surface_description;

  backbuffer -> GetDesc (&surface_description);

  pool = D3DPOOL_SYSTEMMEM;
    hr = _d3d_device->CreateOffscreenPlainSurface(
          surface_description.Width,
          surface_description.Height,
          surface_description.Format,
          pool,
          &temp_surface,
          NULL);
    if (FAILED(hr)) {
      dxgsg9_cat.error()
  << "CreateImageSurface failed in copy_pixel_buffer()"
  << D3DERRORSTRING(hr);
      backbuffer->Release();
      return false;
    }

    // Now we must copy from the backbuffer to our temporary surface.

//  DX8 VERSION  hr = _d3d_device->CopyRects(backbuffer, &rect, 1, temp_surface, NULL);
    hr = _d3d_device -> GetRenderTargetData (backbuffer, temp_surface);

    if (FAILED(hr)) {
      dxgsg9_cat.error() << "GetRenderTargetData failed" << D3DERRORSTRING(hr);
      temp_surface->Release();
      backbuffer->Release();
      return false;
    }

    copy_inverted = true;

    RELEASE(backbuffer, dxgsg9, "backbuffer", RELEASE_ONCE);

  } else if (_cur_read_pixel_buffer & RenderBuffer::T_front) {

    if (_screen->_presentation_params.Windowed) {
      // GetFrontBuffer() retrieves the entire desktop for a monitor,
      // so we need to reserve space for that.

      // We have to use GetMonitorInfo(), since this GSG may not be
      // for the primary monitor.
      MONITORINFO minfo;
      minfo.cbSize = sizeof(MONITORINFO);
      GetMonitorInfo(_screen->_monitor, &minfo);

      w = RECT_XSIZE(minfo.rcMonitor);
      h = RECT_YSIZE(minfo.rcMonitor);

      // set rect to client area of window in scrn coords
      ClientToScreen(_screen->_window, (POINT*)&rect.left);
      ClientToScreen(_screen->_window, (POINT*)&rect.right);
    }

    // For GetFrontBuffer(), we need a temporary surface of type
    // A8R8G8B8.  Unlike GetBackBuffer(), GetFrontBuffer() implicitly
    // performs a copy.
    hr = _d3d_device->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &temp_surface, NULL);
    if (FAILED(hr)) {
      dxgsg9_cat.error()
  << "CreateImageSurface failed in copy_pixel_buffer()"
  << D3DERRORSTRING(hr);
      return false;
    }

    UINT swap_chain;

/* ***** DX9 swap chain ??? */
  swap_chain = 0;
    hr = _d3d_device->GetFrontBufferData(swap_chain,temp_surface);

    if (hr == D3DERR_DEVICELOST) {
      dxgsg9_cat.error()
  << "copy_pixel_buffer failed: device lost\n";
      temp_surface->Release();
      return false;
    }

    // For some reason the front buffer comes out inverted, but the
    // back buffer does not.
    copy_inverted = true;

  } else {
    dxgsg9_cat.error()
      << "copy_pixel_buffer: unhandled current_read_pixel_buffer type\n";
    temp_surface->Release();
    return false;
  }

  DXTextureContext9::d3d_surface_to_texture(rect, temp_surface,
              copy_inverted, tex, z);

  RELEASE(temp_surface, dxgsg9, "temp_surface", RELEASE_ONCE);

  nassertr(tex->has_ram_image(), false);
  return true;
}


bool vertex_buffer_page_in_function (LruPage *lru_page)
{
  DXGraphicsStateGuardian9 *gsg;
  DXVertexBufferContext9 *vertex_buffer;

  gsg = (DXGraphicsStateGuardian9 *) (lru_page -> _m.lru -> _m.context);
  vertex_buffer = (DXVertexBufferContext9 *) lru_page -> _m.lru_page_type.pointer;

  // allocate vertex buffer
  Thread *current_thread = Thread::get_current_thread();
  CPT(GeomVertexArrayDataHandle) reader = vertex_buffer->get_data()->get_handle(current_thread);

  /*
    Not sure if this is the correct thing to do.  Can we return false
    from the page_in function?  Will we get called again next frame if
    we do?
  if (allow_incomplete_render) {
    // Check if the data is resident before continuing.
    const unsigned char *data_pointer = reader->get_read_pointer(false);
    if (data_pointer == NULL) {
      return false;
    }
  }
  */
  vertex_buffer -> allocate_vbuffer (*(gsg->_screen), reader);

  // update vertex buffer
  vertex_buffer -> upload_data(reader, true);
  vertex_buffer -> set_resident(true);

  if (DEBUG_LRU && dxgsg9_cat.is_debug())
  {
    dxgsg9_cat.debug() << "  *** page IN VB " << lru_page -> _m.identifier << " size " << lru_page -> _m.size << "\n";
  }

  return true;
}

bool vertex_buffer_page_out_function (LruPage *lru_page)
{
  DXVertexBufferContext9 *vertex_buffer;

  vertex_buffer = (DXVertexBufferContext9 *) lru_page -> _m.lru_page_type.pointer;
  vertex_buffer -> free_vbuffer ( );
  vertex_buffer -> set_resident(false);

  if (DEBUG_LRU && dxgsg9_cat.is_debug())
  {
    dxgsg9_cat.debug() << "  *** page OUT VB " << lru_page -> _m.identifier << " size " << lru_page -> _m.size << "\n";
  }

  return true;
}

bool index_buffer_page_in_function (LruPage *lru_page)
{
  DXGraphicsStateGuardian9 *gsg;
  DXIndexBufferContext9 *index_buffer;

  gsg = (DXGraphicsStateGuardian9 *) (lru_page -> _m.lru -> _m.context);
  index_buffer = (DXIndexBufferContext9 *) lru_page -> _m.lru_page_type.pointer;

  // allocate vertex buffer
  Thread *current_thread = Thread::get_current_thread();
  GeomPrimitivePipelineReader reader(index_buffer->get_data(), current_thread);

  /*
    Not sure if this is the correct thing to do.  Can we return false
    from the page_in function?  Will we get called again next frame if
    we do?
  if (allow_incomplete_render) {
    // Check if the data is resident before continuing.
    const unsigned char *data_pointer = reader.get_read_pointer(false);
    if (data_pointer == NULL) {
      return false;
    }
  }
  */

  index_buffer -> allocate_ibuffer (*(gsg->_screen), &reader);

  // update vertex buffer
  index_buffer -> upload_data (&reader, true);
  index_buffer -> set_resident(true);

  if (DEBUG_LRU && dxgsg9_cat.is_debug())
  {
    dxgsg9_cat.debug() << "  *** page IN IB " << lru_page -> _m.identifier << " size " << lru_page -> _m.size << "\n";
  }

  return true;
}

bool index_buffer_page_out_function (LruPage *lru_page)
{
  DXIndexBufferContext9 *index_buffer;

  index_buffer = (DXIndexBufferContext9 *) lru_page -> _m.lru_page_type.pointer;
  index_buffer -> free_ibuffer ( );
  index_buffer -> set_resident(false);

  if (DEBUG_LRU && dxgsg9_cat.is_debug())
  {
    dxgsg9_cat.debug() << "  *** page OUT IB " << lru_page -> _m.identifier << " size " << lru_page -> _m.size << "\n";
  }

  return true;
}

bool texture_page_in_function (LruPage *lru_page)
{
  DXGraphicsStateGuardian9 *gsg;
  DXTextureContext9 *texture;

  gsg = (DXGraphicsStateGuardian9 *) (lru_page -> _m.lru -> _m.context);
  texture = (DXTextureContext9 *) lru_page -> _m.lru_page_type.pointer;

  texture -> create_texture (*(gsg->_screen));
  texture -> set_resident(true);

  if (DEBUG_LRU && dxgsg9_cat.is_debug())
  {
    dxgsg9_cat.debug() << "  *** page IN TEX " << lru_page -> _m.identifier << " size " << lru_page -> _m.size << "\n";
  }

  return true;
}

bool texture_page_out_function (LruPage *lru_page)
{
  DXTextureContext9 *texture;

  texture = (DXTextureContext9 *) lru_page -> _m.lru_page_type.pointer;
  texture -> delete_texture ( );
  texture -> set_resident(false);

  if (DEBUG_LRU && dxgsg9_cat.is_debug())
  {
    dxgsg9_cat.debug() << "  *** page OUT TEX " << lru_page -> _m.identifier << " size " << lru_page -> _m.size << "\n";
  }

  return true;
}

void DXGraphicsStateGuardian9::reset_render_states (void)
{
  int index;
  int maximum_texture_stages;

  maximum_texture_stages = D3D_MAXTEXTURESTAGES;

  // set to invalid values so that the state will always be set the first time
  memset (_render_state_array, -1, sizeof (_render_state_array));
  memset (_texture_stage_states_array, -1, sizeof (_texture_stage_states_array));

  // states that may be set intially to -1 by the user, so set it to D3D's default value
  _render_state_array [D3DRS_FOGCOLOR] = 0;
  _render_state_array [D3DRS_AMBIENT] = 0;

  // set to D3D default values or invalid values so that the state will always be set the first time
  memset (_texture_render_states_array, 0, sizeof (_texture_render_states_array));

  // states that may be set intially to 0 by the user, so set it to D3D's default value
  for (index = 0; index < MAXIMUM_TEXTURES; index++) {
    TextureRenderStates *texture_render_states;

    texture_render_states = &_texture_render_states_array [index];
    texture_render_states -> state_array [D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
    texture_render_states -> state_array [D3DSAMP_MINFILTER] = D3DTEXF_POINT;
    texture_render_states -> state_array [D3DSAMP_MAXANISOTROPY] = 1;
  }
  _num_active_texture_stages = 0;

  set_render_state(D3DRS_NORMALIZENORMALS, false);

  _last_fvf = 0;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::reset
//       Access: Public, Virtual
//  Description: Resets all internal state as if the gsg were newly
//               created.  The GraphicsWindow pointer represents a
//               typical window that might be used for this context;
//               it may be required to set up the frame buffer
//               properly the first time.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
reset() {
  GraphicsStateGuardian::reset();

  _auto_rescale_normal = false;

  // overwrite gsg defaults with these values

  HRESULT hr;

  // make sure gsg passes all current state down to us
  // set_state_and_transform(RenderState::make_empty(),
  // TransformState::make_identity());
  // want gsg to pass all state settings down so any non-matching defaults we set here get overwritten

  nassertv(_screen->_d3d9 != NULL);
  
  if (_d3d_device == NULL) {
    return;
  }

  D3DCAPS9 d3d_caps;
  _d3d_device->GetDeviceCaps(&d3d_caps);

  _vertex_shader_version_major = D3DSHADER_VERSION_MAJOR (d3d_caps.VertexShaderVersion);
  _vertex_shader_version_minor = D3DSHADER_VERSION_MINOR (d3d_caps.VertexShaderVersion);
  _pixel_shader_version_major = D3DSHADER_VERSION_MAJOR (d3d_caps.PixelShaderVersion);
  _pixel_shader_version_minor = D3DSHADER_VERSION_MINOR (d3d_caps.PixelShaderVersion);

  _vertex_shader_profile = (char *) D3DXGetVertexShaderProfile (_d3d_device);
  _pixel_shader_profile = (char *) D3DXGetPixelShaderProfile (_d3d_device);

  _vertex_shader_maximum_constants = d3d_caps.MaxVertexShaderConst;

  switch (_pixel_shader_version_major)
  {
    case 0:
      _shader_model = SM_00;
      break;
    case 1:
      _shader_model = SM_11;
      break;
    case 2:
      // minimim specification for pixel shader 2.0 is 96 instruction slots
      _shader_model = SM_20;
      if (d3d_caps.PS20Caps.NumInstructionSlots >= 512) {
        _shader_model = SM_2X;
      }
      break;
    case 3:
      _shader_model = SM_30;
      break;
    case 4:
    default:
      _shader_model = SM_40;
      break;
  }

  _auto_detect_shader_model = _shader_model;

#ifdef HAVE_CG
  cgD3D9SetDevice (_d3d_device);

  if (cgD3D9IsProfileSupported(CG_PROFILE_PS_2_0) &&
      cgD3D9IsProfileSupported(CG_PROFILE_VS_2_0)) {
    _supports_basic_shaders = true;
    _shader_caps._active_vprofile = (int)cgD3D9GetLatestVertexProfile();
    _shader_caps._active_fprofile = (int)cgD3D9GetLatestPixelProfile();
    _shader_caps._ultimate_vprofile = (int)CG_PROFILE_VS_3_0;
    _shader_caps._ultimate_fprofile = (int)CG_PROFILE_PS_3_0;
  }
  
  if (dxgsg9_cat.is_debug()) {
    
    CGprofile vertex_profile;
    CGprofile pixel_profile;
    
    vertex_profile = cgD3D9GetLatestVertexProfile( );
    pixel_profile = cgD3D9GetLatestPixelProfile( );
    
    const char *vertex_profile_str =
      cgGetProfileString(vertex_profile);
    const char *pixel_profile_str =
      cgGetProfileString(pixel_profile);
    
    if (vertex_profile_str == NULL) {
      vertex_profile_str = "(null)";
    }
    if (pixel_profile_str == NULL) {
      pixel_profile_str = "(null)";
    }
    
    dxgsg9_cat.debug()
      << "\nCg vertex profile = " << vertex_profile_str << "  id = " << vertex_profile
      << "\nCg pixel profile = " << pixel_profile_str << "  id = " << pixel_profile
      << "\nshader model = " << _shader_model
      << "\n";
  }
#endif

  _supports_stream_offset = (d3d_caps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET) != 0;
  _screen->_supports_dynamic_textures = ((d3d_caps.Caps2 & D3DCAPS2_DYNAMICTEXTURES) != 0);
  _screen->_supports_automatic_mipmap_generation = ((d3d_caps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP) != 0);

  _supports_stencil_wrap = (d3d_caps.StencilCaps & D3DSTENCILCAPS_INCR) && (d3d_caps.StencilCaps & D3DSTENCILCAPS_DECR);
  _supports_two_sided_stencil = ((d3d_caps.StencilCaps & D3DSTENCILCAPS_TWOSIDED) != 0);

  _supports_depth_bias = ((d3d_caps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0);

  _supports_gamma_calibration = ((d3d_caps.Caps2 & D3DCAPS2_CANCALIBRATEGAMMA) != 0);

  // Test for occlusion query support
  hr = _d3d_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, NULL);
  _supports_occlusion_query = !FAILED(hr);

  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug()
      << "\nHwTransformAndLight = " << ((d3d_caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0)
      << "\nMaxTextureWidth = " << d3d_caps.MaxTextureWidth
      << "\nMaxTextureHeight = " << d3d_caps.MaxTextureHeight
      << "\nMaxVolumeExtent = " << d3d_caps.MaxVolumeExtent
      << "\nMaxTextureAspectRatio = " << d3d_caps.MaxTextureAspectRatio
      << "\nTexCoordCount = " << (d3d_caps.FVFCaps & D3DFVFCAPS_TEXCOORDCOUNTMASK)
      << "\nMaxTextureBlendStages = " << d3d_caps.MaxTextureBlendStages
      << "\nMaxSimultaneousTextures = " << d3d_caps.MaxSimultaneousTextures
      << "\nMaxActiveLights = " << d3d_caps.MaxActiveLights
      << "\nMaxUserClipPlanes = " << d3d_caps.MaxUserClipPlanes
      << "\nMaxVertexBlendMatrices = " << d3d_caps.MaxVertexBlendMatrices
      << "\nMaxVertexBlendMatrixIndex = " << d3d_caps.MaxVertexBlendMatrixIndex
      << "\nMaxPointSize = " << d3d_caps.MaxPointSize
      << "\nMaxPrimitiveCount = " << d3d_caps.MaxPrimitiveCount
      << "\nMaxVertexIndex = " << d3d_caps.MaxVertexIndex
      << "\nMaxStreams = " << d3d_caps.MaxStreams
      << "\nMaxStreamStride = " << d3d_caps.MaxStreamStride
      << "\nD3DTEXOPCAPS_MULTIPLYADD = " << ((d3d_caps.TextureOpCaps & D3DTEXOPCAPS_MULTIPLYADD) != 0)
      << "\nD3DTEXOPCAPS_LERP = " << ((d3d_caps.TextureOpCaps & D3DTEXOPCAPS_LERP) != 0)
      << "\nD3DPMISCCAPS_TSSARGTEMP = " << ((d3d_caps.PrimitiveMiscCaps & D3DPMISCCAPS_TSSARGTEMP) != 0)
      << "\nD3DPRASTERCAPS_DEPTHBIAS = " << ((d3d_caps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0)
      << "\nD3DPRASTERCAPS_SLOPESCALEDEPTHBIAS = " << ((d3d_caps.RasterCaps & D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS) != 0)
      << "\nVertexShaderVersion = " << _vertex_shader_version_major << "." << _vertex_shader_version_minor
      << "\nPixelShaderVersion = " << _pixel_shader_version_major << "." << _pixel_shader_version_minor
      << "\nMaxVertexShaderConst = " << _vertex_shader_maximum_constants
      << "\nsupports_stream_offset = " << _supports_stream_offset
      << "\nsupports_dynamic_textures = " << _screen->_supports_dynamic_textures
      << "\nsupports_automatic_mipmap_generation = " << _screen->_supports_automatic_mipmap_generation
      << "\nsupports_stencil_wrap = " << _supports_stencil_wrap
      << "\nsupports_two_sided_stencil = " << _supports_two_sided_stencil
      << "\nsupports_occlusion_query = " << _supports_occlusion_query
      << "\nsupports_gamma_calibration = " << _supports_gamma_calibration
      << "\nMaxAnisotropy = " << d3d_caps.MaxAnisotropy
      << "\nDirectX SDK version " DIRECTX_SDK_VERSION
      << "\n";
  }

  // OVERRIDE SUPPORT SINCE IT DOES NOT WORK WELL
  _screen->_supports_automatic_mipmap_generation = false;

  this -> reset_render_states ( );

  _max_vertices_per_array = d3d_caps.MaxVertexIndex;
  _max_vertices_per_primitive = d3d_caps.MaxPrimitiveCount;

  _max_texture_stages = d3d_caps.MaxSimultaneousTextures;

  _max_texture_dimension = min(d3d_caps.MaxTextureWidth, d3d_caps.MaxTextureHeight);

  _supports_texture_combine = ((d3d_caps.TextureOpCaps & D3DTEXOPCAPS_LERP) != 0);
  _supports_texture_saved_result = ((d3d_caps.PrimitiveMiscCaps & D3DPMISCCAPS_TSSARGTEMP) != 0);
  _supports_texture_constant_color = ((d3d_caps.PrimitiveMiscCaps & D3DPMISCCAPS_PERSTAGECONSTANT) != 0);
  _supports_texture_dot3 = true;

  if (_supports_texture_constant_color) {
    _constant_color_operand = D3DTA_CONSTANT;
  } else {
    _constant_color_operand = D3DTA_TFACTOR;
  }

  _screen->_managed_textures = _gsg_managed_textures;
  _screen->_managed_vertex_buffers = _gsg_managed_vertex_buffers;
  _screen->_managed_index_buffers = _gsg_managed_index_buffers;

  UINT available_texture_memory;

  available_texture_memory = _d3d_device->GetAvailableTextureMem ( );
  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug() << "*** GetAvailableTextureMem = " <<  available_texture_memory << "\n";
  }
  _available_texture_memory = available_texture_memory;

  if (_lru == 0) {
    if (dx_management == false && _enable_lru) {
      int error;
      int maximum_memory;
      int maximum_pages;
      Lru *lru;

      int absolute_minimum_memory_requirement;
      int minimum_memory_requirement;
      int maximum_memory_requirement;
      int free_memory_requirement;

      maximum_pages = dx_lru_maximum_pages;

      absolute_minimum_memory_requirement = 5000000;
      minimum_memory_requirement = dx_lru_minimum_memory_requirement;
      maximum_memory_requirement = dx_lru_maximum_memory_requirement;
      free_memory_requirement = dx_lru_free_memory_requirement;

      error = false;
      maximum_memory = available_texture_memory - free_memory_requirement;
      if (maximum_memory < minimum_memory_requirement) {
         if (maximum_memory < absolute_minimum_memory_requirement) {
           // video memory size is too small

            dxgsg9_cat.error() << "Available video memory is too low " << _available_texture_memory << "\n";

            error = true;
         }
         else {
           // video memory is way too low, so take all of it
           maximum_memory = available_texture_memory;

           // need to warn user about low video memory
           dxgsg9_cat.warning() << "Available video memory " << maximum_memory << " is below the minimum requirement of " << minimum_memory_requirement << "\n";
        }
      }
      else {
        if (maximum_memory_requirement != 0) {
          if (maximum_memory >= maximum_memory_requirement) {
            // cap video memory used
            maximum_memory = maximum_memory_requirement;
          }
        } else {
          // take all video memory

        }
      }

      // no LRU if there is an error, go back to DirectX managed
      if (error) {
        _gsg_managed_textures = true;
        _gsg_managed_vertex_buffers = true;
        _gsg_managed_index_buffers = true;
      } else {

        int maximum_page_types;

        maximum_page_types = GPT_TotalPageTypes;
        lru = new Lru (maximum_memory, maximum_pages, maximum_page_types);
        if (lru) {
          // register page in and out callbacks
          lru -> register_lru_page_type (GPT_VertexBuffer, vertex_buffer_page_in_function, vertex_buffer_page_out_function);
          lru -> register_lru_page_type (GPT_IndexBuffer, index_buffer_page_in_function, index_buffer_page_out_function);
          lru -> register_lru_page_type (GPT_Texture, texture_page_in_function, texture_page_out_function);

          lru -> _m.context = (void *) this;
        }

        _lru = lru;
      }
    }
  }

  // check for render to texture support
  D3DDEVICE_CREATION_PARAMETERS creation_parameters;

  _supports_render_texture = false;
  _screen->_render_to_texture_d3d_format = D3DFMT_UNKNOWN;
  _screen->_framebuffer_d3d_format = D3DFMT_UNKNOWN;

  #define TOTAL_RENDER_TO_TEXTURE_FORMATS 3

  D3DFORMAT render_to_texture_formats [TOTAL_RENDER_TO_TEXTURE_FORMATS] =
  {
    D3DFMT_A8R8G8B8,  // check for this format first
    D3DFMT_X8R8G8B8,
    D3DFMT_UNKNOWN,   // place holder for _screen->_display_mode.Format
  };

  render_to_texture_formats [TOTAL_RENDER_TO_TEXTURE_FORMATS - 1] = _screen->_display_mode.Format;

  hr = _d3d_device->GetCreationParameters (&creation_parameters);
  if (SUCCEEDED (hr)) {
    _screen->_framebuffer_d3d_format = _screen->_display_mode.Format;

    int index;
    for (index = 0; index < TOTAL_RENDER_TO_TEXTURE_FORMATS; index++) {
      hr = _screen->_d3d9->CheckDeviceFormat (
          creation_parameters.AdapterOrdinal,
          creation_parameters.DeviceType,
          _screen->_display_mode.Format,
          D3DUSAGE_RENDERTARGET,
          D3DRTYPE_TEXTURE,
          render_to_texture_formats [index]);
      if (SUCCEEDED (hr)) {
        _screen->_render_to_texture_d3d_format = render_to_texture_formats [index];
        _supports_render_texture = true;
      }
      if (_supports_render_texture) {
        break;
      }
    }
  }
  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug() << "Render to Texture Support = " << _supports_render_texture << "\n";
  }

  // override default config setting since it is really supported or not ???
//  support_render_texture = _supports_render_texture;

  _supports_3d_texture = ((d3d_caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0);
  if (_supports_3d_texture) {
    _max_3d_texture_dimension = d3d_caps.MaxVolumeExtent;
  }
  _supports_cube_map = ((d3d_caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0);
  if (_supports_cube_map) {
    _max_cube_map_dimension = _max_texture_dimension;
  }

  _max_lights = (int)d3d_caps.MaxActiveLights;
  _max_clip_planes = (int)d3d_caps.MaxUserClipPlanes;
  _max_vertex_transforms = d3d_caps.MaxVertexBlendMatrices;
  _max_vertex_transform_indices = d3d_caps.MaxVertexBlendMatrixIndex;

  set_render_state(D3DRS_AMBIENT, 0x0);

  _clip_plane_bits = 0;
  set_render_state(D3DRS_CLIPPLANEENABLE , 0x0);

  set_render_state(D3DRS_CLIPPING, true);

  set_render_state(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

  set_render_state(D3DRS_ZWRITEENABLE, TRUE);

/* ***** DX9 ??? D3DRS_EDGEANTIALIAS NOT IN DX9 */
//  set_render_state(D3DRS_EDGEANTIALIAS, false);

  set_render_state(D3DRS_ZENABLE, D3DZB_FALSE);

  set_render_state(D3DRS_ALPHABLENDENABLE, FALSE);

  set_render_state(D3DRS_FOGENABLE, FALSE);

  _has_scene_graph_color = false;

  _last_testcooplevel_result = D3D_OK;

  for(int i = 0; i < MAX_POSSIBLE_TEXFMTS; i++) {
    // look for all possible DX9 texture fmts
    D3DFORMAT_FLAG fmtflag = D3DFORMAT_FLAG(1 << i);
    hr = _screen->_d3d9->CheckDeviceFormat(_screen->_card_id, D3DDEVTYPE_HAL, _screen->_display_mode.Format,
                                          0x0, D3DRTYPE_TEXTURE, g_D3DFORMATmap[fmtflag]);
    if (SUCCEEDED(hr)){
      _screen->_supported_tex_formats_mask |= fmtflag;
    }
  }

  // check if compressed textures are supported
  #define CHECK_FOR_DXTVERSION(num) \
  if (_screen->_supported_tex_formats_mask & DXT##num##_FLAG) {\
    if (dxgsg9_cat.is_debug()) {\
      dxgsg9_cat.debug() << "Compressed texture format DXT" << #num << " supported \n";\
    }\
    _supports_compressed_texture = true;\
    _compressed_texture_formats.set_bit(Texture::CM_dxt##num);\
  }

  CHECK_FOR_DXTVERSION(1)
  CHECK_FOR_DXTVERSION(2)
  CHECK_FOR_DXTVERSION(3)
  CHECK_FOR_DXTVERSION(4)
  CHECK_FOR_DXTVERSION(5)

  #undef CHECK_FOR_DXTVERSION

  _screen->_supports_rgba16f_texture_format = false;
  hr = _screen->_d3d9->CheckDeviceFormat(_screen->_card_id, D3DDEVTYPE_HAL, _screen->_display_mode.Format, 0x0, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
  if (SUCCEEDED(hr)){
    _screen->_supports_rgba16f_texture_format = true;
  }
  _screen->_supports_rgba32f_texture_format = false;
  hr = _screen->_d3d9->CheckDeviceFormat(_screen->_card_id, D3DDEVTYPE_HAL, _screen->_display_mode.Format, 0x0, D3DRTYPE_TEXTURE, D3DFMT_A32B32G32R32F);
  if (SUCCEEDED(hr)){
    _screen->_supports_rgba32f_texture_format = true;
  }

  // s3 virge drivers sometimes give crap values for these
  if (_screen->_d3dcaps.MaxTextureWidth == 0)
    _screen->_d3dcaps.MaxTextureWidth = 256;

  if (_screen->_d3dcaps.MaxTextureHeight == 0)
    _screen->_d3dcaps.MaxTextureHeight = 256;

  if (_screen->_d3dcaps.RasterCaps & D3DPRASTERCAPS_FOGTABLE) {
    // Watch out for drivers that emulate per-pixel fog with
    // per-vertex fog (Riva128, Matrox Millen G200).  Some of these
    // require gouraud-shading to be set to work, as if you were using
    // vertex fog
    _do_fog_type = PerPixelFog;
  } else {
    // every card is going to have vertex fog, since it's implemented
    // in d3d runtime.
    nassertv((_screen->_d3dcaps.RasterCaps & D3DPRASTERCAPS_FOGVERTEX) != 0);

    // vertex fog may look crappy if you have large polygons in the
    // foreground and they get clipped, so you may want to disable it

    if (dx_no_vertex_fog) {
      _do_fog_type = None;
    } else {
      _do_fog_type = PerVertexFog;

      // range-based fog only works with vertex fog in dx7/8
      if (dx_use_rangebased_fog && (_screen->_d3dcaps.RasterCaps & D3DPRASTERCAPS_FOGRANGE)) {
        set_render_state(D3DRS_RANGEFOGENABLE, true);
      }
    }
  }

  _screen->_can_direct_disable_color_writes = ((_screen->_d3dcaps.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE) != 0);

  // Lighting, let's turn it off initially.
  set_render_state(D3DRS_LIGHTING, false);

  // turn on dithering if the rendertarget is < 8bits/color channel
  bool dither_enabled = ((!dx_no_dithering) && IS_16BPP_DISPLAY_FORMAT(_screen->_presentation_params.BackBufferFormat)
       && (_screen->_d3dcaps.RasterCaps & D3DPRASTERCAPS_DITHER));
  set_render_state(D3DRS_DITHERENABLE, dither_enabled);

  set_render_state(D3DRS_CLIPPING, true);

  // Stencil test is off by default
  set_render_state(D3DRS_STENCILENABLE, FALSE);
  if (_supports_two_sided_stencil) {
    set_render_state(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
  }

  // Antialiasing.
/* ***** DX9 ??? D3DRS_EDGEANTIALIAS NOT IN DX9 */
//  set_render_state(D3DRS_EDGEANTIALIAS, FALSE);

  _current_fill_mode = RenderModeAttrib::M_filled;
  set_render_state(D3DRS_FILLMODE, D3DFILL_SOLID);

  // must do SetTSS here because redundant states are filtered out by
  // our code based on current values above, so initial conditions
  // must be correct
  set_texture_stage_state(0, D3DTSS_COLOROP, D3DTOP_DISABLE);  // disables texturing

  _cull_face_mode = CullFaceAttrib::M_cull_none;
  set_render_state(D3DRS_CULLMODE, D3DCULL_NONE);

  set_render_state(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
  set_render_state(D3DRS_ALPHAREF, 255);
  set_render_state(D3DRS_ALPHATESTENABLE, FALSE);

  // this is a new DX8 state that lets you do additional operations other than ADD (e.g. subtract/max/min)
  // must check (_screen->_d3dcaps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) (yes on GF2/Radeon8500, no on TNT)
  set_render_state(D3DRS_BLENDOP, D3DBLENDOP_ADD);

  _current_shader = (Shader *)NULL;
  _current_shader_context = (CLP(ShaderContext) *)NULL;
  _vertex_array_shader = (Shader *)NULL;
  _vertex_array_shader_context = (CLP(ShaderContext) *)NULL;
  _texture_binding_shader = (Shader *)NULL;
  _texture_binding_shader_context = (CLP(ShaderContext) *)NULL;

  PRINT_REFCNT(dxgsg9, _d3d_device);

  void dx_set_stencil_functions (StencilRenderStates *stencil_render_states);
  dx_set_stencil_functions (_stencil_render_states);

  // Now that the GSG has been initialized, make it available for
  // optimizations.
  add_gsg(this);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::apply_fog
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
apply_fog(Fog *fog) {
  if (_do_fog_type == None)
    return;

  Fog::Mode panda_fogmode = fog->get_mode();
  D3DFOGMODE d3dfogmode = get_fog_mode_type(panda_fogmode);

  set_render_state((D3DRENDERSTATETYPE)_do_fog_type, d3dfogmode);

  const Colorf &fog_colr = fog->get_color();
  set_render_state(D3DRS_FOGCOLOR,
                              MY_D3DRGBA(fog_colr[0], fog_colr[1], fog_colr[2], 0.0f));  // Alpha bits are not used

  // do we need to adjust fog start/end values based on D3DPRASTERCAPS_WFOG/D3DPRASTERCAPS_ZFOG ?
  // if not WFOG, then docs say we need to adjust values to range [0, 1]

  switch (panda_fogmode) {
  case Fog::M_linear:
    {
      float onset, opaque;
      fog->get_linear_range(onset, opaque);

      set_render_state(D3DRS_FOGSTART,
                                   *((LPDWORD) (&onset)));
      set_render_state(D3DRS_FOGEND,
                                   *((LPDWORD) (&opaque)));
    }
    break;
  case Fog::M_exponential:
  case Fog::M_exponential_squared:
    {
      // Exponential fog is always camera-relative.
      float fog_density = fog->get_exp_density();
      set_render_state(D3DRS_FOGDENSITY,
                                   *((LPDWORD) (&fog_density)));
    }
    break;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_transform
//       Access: Protected
//  Description: Sends the indicated transform matrix to the graphics
//               API to be applied to future vertices.
//
//               This transform is the internal_transform, already
//               converted into the GSG's internal coordinate system.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_transform() {
  const TransformState *transform = _internal_transform;
  DO_PSTATS_STUFF(_transform_state_pcollector.add_level(1));

  if (_current_shader_context) {
//    _current_shader_context->issue_transform(this);
    _current_shader_context->issue_parameters(this, Shader::SSD_transform);

// ??? NO NEED TO SET THE D3D TRANSFORM VIA SetTransform SINCE THE TRANSFORM IS ONLY USED IN THE SHADER
    const D3DMATRIX *d3d_mat = (const D3DMATRIX *)transform->get_mat().get_data();
    _d3d_device->SetTransform(D3DTS_WORLD, d3d_mat);

  }
  else {
    const D3DMATRIX *d3d_mat = (const D3DMATRIX *)transform->get_mat().get_data();
    _d3d_device->SetTransform(D3DTS_WORLD, d3d_mat);

// DEBUG PRINT
/*
    const float *data;
    data = &d3d_mat -> _11;
        dxgsg9_cat.debug ( ) << "do_issue_transform\n" <<
          data[ 0] << " " << data[ 1] << " " << data[ 2] << " " << data[ 3] << "\n" <<
          data[ 4] << " " << data[ 5] << " " << data[ 6] << " " << data[ 7] << "\n" <<
          data[ 8] << " " << data[ 9] << " " << data[10] << " " << data[11] << "\n" <<
          data[12] << " " << data[13] << " " << data[14] << " " << data[15] << "\n";
*/

  }

  _transform_stale = false;

  if (_auto_rescale_normal) {
    do_auto_rescale_normal();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_alpha_test
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_alpha_test() {
  const AlphaTestAttrib *attrib = _target._alpha_test;
  AlphaTestAttrib::PandaCompareFunc mode = attrib->get_mode();
  if (mode == AlphaTestAttrib::M_none) {
    set_render_state(D3DRS_ALPHATESTENABLE, FALSE);

  } else {
    //  AlphaTestAttrib::PandaCompareFunc === D3DCMPFUNC
    set_render_state(D3DRS_ALPHAFUNC, (D3DCMPFUNC)mode);
    set_render_state(D3DRS_ALPHAREF, (UINT) (attrib->get_reference_alpha()*255.0f));  //d3d uses 0x0-0xFF, not a float
    set_render_state(D3DRS_ALPHATESTENABLE, TRUE);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_shader
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_shader() {

  DBG_SH3  dxgsg9_cat.debug ( ) << "SHADER: do_issue_shader\n"; DBG_E

  CLP(ShaderContext) *context = 0;
  Shader *shader = 0;
  if (_target._shader) {
    shader = (Shader *)(_target._shader->get_shader());
  }
  if (shader) {
    context = (CLP(ShaderContext) *)(shader->prepare_now(get_prepared_objects(), this));
  }

  if (context == 0 || (context && context -> valid (this) == false)) {
    if (_current_shader_context != 0) {
      _current_shader_context->unbind(this);
      _current_shader = 0;
      _current_shader_context = 0;
      disable_standard_texture_bindings();
    }
    return;
  }

  if (context != _current_shader_context) {
    // Use a completely different shader than before.
    // Unbind old shader, bind the new one.
    if (_current_shader_context != 0) {
      _current_shader_context->unbind(this);
      _current_shader_context = 0;
      _current_shader = 0;
      disable_standard_texture_bindings();
    }
    if (context != 0) {
      context->bind(this);
      _current_shader = shader;
      _current_shader_context = context;
    }
  } else {
    // Use the same shader as before, but with new input arguments.
    context->issue_parameters(this, Shader::SSD_shaderinputs);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_render_mode
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_render_mode() {
  const RenderModeAttrib *attrib = _target._render_mode;
  RenderModeAttrib::Mode mode = attrib->get_mode();

  switch (mode) {
  case RenderModeAttrib::M_unchanged:
  case RenderModeAttrib::M_filled:
    set_render_state(D3DRS_FILLMODE, D3DFILL_SOLID);
    break;

  case RenderModeAttrib::M_wireframe:
    set_render_state(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    break;

  case RenderModeAttrib::M_point:
    set_render_state(D3DRS_FILLMODE, D3DFILL_POINT);
    break;

  default:
    dxgsg9_cat.error()
      << "Unknown render mode " << (int)mode << endl;
  }

  // This might also specify the point size.
  float point_size = attrib->get_thickness();
  set_render_state(D3DRS_POINTSIZE, *((DWORD*)&point_size));

  if (attrib->get_perspective()) {
    set_render_state(D3DRS_POINTSCALEENABLE, TRUE);

    LVector3f height(0.0f, point_size, 1.0f);
    height = height * _projection_mat->get_mat();
    float s = height[1] / point_size;

    float zero = 0.0f;
    float one_over_s2 = 1.0f / (s * s);
    set_render_state(D3DRS_POINTSCALE_A, *((DWORD*)&zero));
    set_render_state(D3DRS_POINTSCALE_B, *((DWORD*)&zero));
    set_render_state(D3DRS_POINTSCALE_C, *((DWORD*)&one_over_s2));

  } else {
    set_render_state(D3DRS_POINTSCALEENABLE, FALSE);
  }

  _current_fill_mode = mode;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_rescale_normal
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_rescale_normal() {
  const RescaleNormalAttrib *attrib = _target._rescale_normal;
  RescaleNormalAttrib::Mode mode = attrib->get_mode();

  _auto_rescale_normal = false;

  switch (mode) {
  case RescaleNormalAttrib::M_none:
    set_render_state(D3DRS_NORMALIZENORMALS, false);
    break;

  case RescaleNormalAttrib::M_rescale:
  case RescaleNormalAttrib::M_normalize:
    set_render_state(D3DRS_NORMALIZENORMALS, true);
    break;

  case RescaleNormalAttrib::M_auto:
    _auto_rescale_normal = true;
    do_auto_rescale_normal();
    break;

  default:
    dxgsg9_cat.error()
      << "Unknown rescale_normal mode " << (int)mode << endl;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_depth_test
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_depth_test() {
  const DepthTestAttrib *attrib = _target._depth_test;
  DepthTestAttrib::PandaCompareFunc mode = attrib->get_mode();
  if (mode == DepthTestAttrib::M_none) {
    set_render_state(D3DRS_ZENABLE, D3DZB_FALSE);
  } else {
    set_render_state(D3DRS_ZENABLE, D3DZB_TRUE);
    set_render_state(D3DRS_ZFUNC, (D3DCMPFUNC) mode);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_depth_write
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_depth_write() {
  const DepthWriteAttrib *attrib = _target._depth_write;
  if (attrib->get_mode() == DepthWriteAttrib::M_on) {
    set_render_state(D3DRS_ZWRITEENABLE, TRUE);
  } else {
    set_render_state(D3DRS_ZWRITEENABLE, FALSE);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_cull_face
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_cull_face() {
  const CullFaceAttrib *attrib = _target._cull_face;
  _cull_face_mode = attrib->get_effective_mode();

  switch (_cull_face_mode) {
  case CullFaceAttrib::M_cull_none:
    set_render_state(D3DRS_CULLMODE, D3DCULL_NONE);
    break;
  case CullFaceAttrib::M_cull_clockwise:
    set_render_state(D3DRS_CULLMODE, D3DCULL_CW);
    break;
  case CullFaceAttrib::M_cull_counter_clockwise:
    set_render_state(D3DRS_CULLMODE, D3DCULL_CCW);
    break;
  default:
    dxgsg9_cat.error()
      << "invalid cull face mode " << (int)_cull_face_mode << endl;
    break;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_fog
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_fog() {
  const FogAttrib *attrib = _target._fog;
  if (!attrib->is_off()) {
    set_render_state(D3DRS_FOGENABLE, TRUE);
    Fog *fog = attrib->get_fog();
    nassertv(fog != (Fog *)NULL);
    apply_fog(fog);
  } else {
    set_render_state(D3DRS_FOGENABLE, FALSE);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_depth_offset
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_depth_offset() {
  const DepthOffsetAttrib *attrib = _target._depth_offset;
  int offset = attrib->get_offset();

  if (_supports_depth_bias) {
    set_render_state(D3DRS_DEPTHBIAS, offset);

  } else {
    // DirectX depth bias isn't directly supported by the driver.
    // Cheese a depth bias effect by sliding the viewport backward a
    // bit.
    static const float bias_scale = 0.0001;
    D3DVIEWPORT9 vp = _current_viewport;
    vp.MinZ -= bias_scale * offset;
    vp.MaxZ -= bias_scale * offset;
    _d3d_device->SetViewport(&vp);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_shade_model
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_shade_model() {
  const ShadeModelAttrib *attrib = _target._shade_model;
  switch (attrib->get_mode()) {
  case ShadeModelAttrib::M_smooth:
    set_render_state(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    break;

  case ShadeModelAttrib::M_flat:
    set_render_state(D3DRS_SHADEMODE, D3DSHADE_FLAT);
    break;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_state_and_transform
//       Access: Public, Virtual
//  Description: Simultaneously resets the render state and the
//               transform state.
//
//               This transform specified is the "internal" net
//               transform, already converted into the GSG's internal
//               coordinate space by composing it to
//               get_cs_transform().  (Previously, this used to be the
//               "external" net transform, with the assumption that
//               that GSG would convert it internally, but that is no
//               longer the case.)
//
//               Special case: if (state==NULL), then the target
//               state is already stored in _target.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_state_and_transform(const RenderState *target,
                        const TransformState *transform) {
#ifndef NDEBUG
  if (gsg_cat.is_spam()) {
    gsg_cat.spam() << "Setting GSG state to " << (void *)target << ":\n";
    target->write(gsg_cat.spam(false), 2);
  }
#endif
  _state_pcollector.add_level(1);

  if (transform != _internal_transform) {
    _state_pcollector.add_level(1);
    _internal_transform = transform;
    do_issue_transform();
  }

  if (target == _state_rs) {
    return;
  }
  _target_rs = target;
  _target.clear_to_defaults();
  target->store_into_slots(&_target);
  _state_rs = 0;

  if (_target._shader && (_target._shader->auto_shader())) {
    _target._shader = _target_rs->get_generated_shader();
  }
  
  if (_target._alpha_test != _state._alpha_test) {
    do_issue_alpha_test();
    _state._alpha_test = _target._alpha_test;
  }

  if (_target._antialias != _state._antialias) {
    // Antialias not implemented under DX8
    _state._antialias = _target._antialias;
  }

  if (_target._clip_plane != _state._clip_plane) {
    do_issue_clip_plane();
    _state._clip_plane = _target._clip_plane;
  }

  if (_target._color != _state._color ||
      _target._color_scale != _state._color_scale) {
    do_issue_color();
    do_issue_color_scale();
    _state._color = _target._color;
    _state._color_scale = _target._color_scale;
    if (_current_shader_context) {
      _current_shader_context->issue_parameters(this, Shader::SSD_color);
    }
  }

  if (_target._cull_face != _state._cull_face) {
    do_issue_cull_face();
    _state._cull_face = _target._cull_face;
  }

  if (_target._depth_offset != _state._depth_offset) {
    do_issue_depth_offset();
    _state._depth_offset = _target._depth_offset;
  }

  if (_target._depth_test != _state._depth_test) {
    do_issue_depth_test();
    _state._depth_test = _target._depth_test;
  }

  if (_target._depth_write != _state._depth_write) {
    do_issue_depth_write();
    _state._depth_write = _target._depth_write;
  }

  if (_target._fog != _state._fog) {
    do_issue_fog();
    _state._fog = _target._fog;
  }

  if (_target._render_mode != _state._render_mode) {
    do_issue_render_mode();
    _state._render_mode = _target._render_mode;
  }

  if (_target._rescale_normal != _state._rescale_normal) {
    do_issue_rescale_normal();
    _state._rescale_normal = _target._rescale_normal;
  }

  if (_target._shade_model != _state._shade_model) {
    do_issue_shade_model();
    _state._shade_model = _target._shade_model;
  }

  if ((_target._transparency != _state._transparency)||
      (_target._color_write != _state._color_write)||
      (_target._color_blend != _state._color_blend)) {
    do_issue_blending();
    _state._transparency = _target._transparency;
    _state._color_write = _target._color_write;
    _state._color_blend = _target._color_blend;
  }

  if (_target._shader != _state._shader) {
    do_issue_shader();
    _state._shader = _target._shader;
    _state._texture = 0;
  }

  if (_target._tex_matrix != _state._tex_matrix) {
    _state._texture = 0;
    _state._tex_matrix = _target._tex_matrix;
  }

  if (_target._texture != _state._texture ||
      _target._tex_gen != _state._tex_gen) {
    determine_effective_texture();
    do_issue_texture();
    _state._texture = _target._texture;
    _state._tex_gen = _target._tex_gen;
  }

  if (_target._material != _state._material) {
    do_issue_material();
    _state._material = _target._material;
    if (_current_shader_context) {
      _current_shader_context->issue_parameters(this, Shader::SSD_material);
    }
  }

  if (_target._light != _state._light) {
    do_issue_light();
    _state._light = _target._light;
  }

  if (_target._stencil != _state._stencil) {
    do_issue_stencil();
    _state._stencil = _target._stencil;
  }

  _state_rs = _target_rs;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::bind_light
//       Access: Public, Virtual
//  Description: Called the first time a particular light has been
//               bound to a given id within a frame, this should set
//               up the associated hardware light with the light's
//               properties.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
bind_light(PointLight *light_obj, const NodePath &light, int light_id) {
  // Get the light in "world coordinates" (actually, view
  // coordinates).  This means the light in the coordinate space of
  // the camera, converted to DX's coordinate system.
  CPT(TransformState) transform = light.get_transform(_scene_setup->get_camera_path());
  const LMatrix4f &light_mat = transform->get_mat();
  LMatrix4f rel_mat = light_mat * LMatrix4f::convert_mat(CS_yup_left, CS_default);
  LPoint3f pos = light_obj->get_point() * rel_mat;

  D3DCOLORVALUE black;
  black.r = black.g = black.b = black.a = 0.0f;
  D3DLIGHT9 alight;
  alight.Type =  D3DLIGHT_POINT;
  alight.Diffuse  = get_light_color(light_obj);
  alight.Ambient  =  black ;
  alight.Specular = *(D3DCOLORVALUE *)(light_obj->get_specular_color().get_data());

  // Position needs to specify x, y, z, and w
  // w == 1 implies non-infinite position
  alight.Position = *(D3DVECTOR *)pos.get_data();

  alight.Range =  __D3DLIGHT_RANGE_MAX;
  alight.Falloff =  1.0f;

  const LVecBase3f &att = light_obj->get_attenuation();
  alight.Attenuation0 = att[0];
  alight.Attenuation1 = att[1];
  alight.Attenuation2 = att[2];

  HRESULT hr = _d3d_device->SetLight(light_id, &alight);
  if (FAILED(hr)) {
    wdxdisplay9_cat.warning()
      << "Could not set light properties for " << light
      << " to id " << light_id << ": " << D3DERRORSTRING(hr) << "\n";
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::bind_light
//       Access: Public, Virtual
//  Description: Called the first time a particular light has been
//               bound to a given id within a frame, this should set
//               up the associated hardware light with the light's
//               properties.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
bind_light(DirectionalLight *light_obj, const NodePath &light, int light_id) {
  // Get the light in "world coordinates" (actually, view
  // coordinates).  This means the light in the coordinate space of
  // the camera, converted to DX's coordinate system.
  CPT(TransformState) transform = light.get_transform(_scene_setup->get_camera_path());
  const LMatrix4f &light_mat = transform->get_mat();
  LMatrix4f rel_mat = light_mat * LMatrix4f::convert_mat(CS_yup_left, CS_default);
  LVector3f dir = light_obj->get_direction() * rel_mat;

  D3DCOLORVALUE black;
  black.r = black.g = black.b = black.a = 0.0f;

  D3DLIGHT9 alight;
  ZeroMemory(&alight, sizeof(D3DLIGHT9));

  alight.Type =  D3DLIGHT_DIRECTIONAL;
  alight.Diffuse  = get_light_color(light_obj);
  alight.Ambient  =  black ;
  alight.Specular = *(D3DCOLORVALUE *)(light_obj->get_specular_color().get_data());

  alight.Direction = *(D3DVECTOR *)dir.get_data();

  alight.Range =  __D3DLIGHT_RANGE_MAX;
  alight.Falloff =  1.0f;

  alight.Attenuation0 = 1.0f;       // constant
  alight.Attenuation1 = 0.0f;       // linear
  alight.Attenuation2 = 0.0f;       // quadratic

  HRESULT hr = _d3d_device->SetLight(light_id, &alight);
  if (FAILED(hr)) {
    wdxdisplay9_cat.warning()
      << "Could not set light properties for " << light
      << " to id " << light_id << ": " << D3DERRORSTRING(hr) << "\n";
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::bind_light
//       Access: Public, Virtual
//  Description: Called the first time a particular light has been
//               bound to a given id within a frame, this should set
//               up the associated hardware light with the light's
//               properties.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
bind_light(Spotlight *light_obj, const NodePath &light, int light_id) {
  Lens *lens = light_obj->get_lens();
  nassertv(lens != (Lens *)NULL);

  // Get the light in "world coordinates" (actually, view
  // coordinates).  This means the light in the coordinate space of
  // the camera, converted to DX's coordinate system.
  CPT(TransformState) transform = light.get_transform(_scene_setup->get_camera_path());
  const LMatrix4f &light_mat = transform->get_mat();
  LMatrix4f rel_mat = light_mat * LMatrix4f::convert_mat(CS_yup_left, CS_default);
  LPoint3f pos = lens->get_nodal_point() * rel_mat;
  LVector3f dir = lens->get_view_vector() * rel_mat;

  D3DCOLORVALUE black;
  black.r = black.g = black.b = black.a = 0.0f;

  D3DLIGHT9  alight;
  ZeroMemory(&alight, sizeof(D3DLIGHT9));

  alight.Type =  D3DLIGHT_SPOT;
  alight.Ambient  =  black ;
  alight.Diffuse  = get_light_color(light_obj);
  alight.Specular = *(D3DCOLORVALUE *)(light_obj->get_specular_color().get_data());

  alight.Position = *(D3DVECTOR *)pos.get_data();

  alight.Direction = *(D3DVECTOR *)dir.get_data();

  alight.Range =  __D3DLIGHT_RANGE_MAX;

  // I determined this formular empirically.  It seems to mostly
  // approximate the OpenGL spotlight equation, for a reasonable range
  // of values for FOV.
  float fov = lens->get_hfov();
  alight.Falloff =  light_obj->get_exponent() * (fov * fov * fov) / 1620000.0f;

  alight.Theta =  0.0f;
  alight.Phi = deg_2_rad(fov);

  const LVecBase3f &att = light_obj->get_attenuation();
  alight.Attenuation0 = att[0];
  alight.Attenuation1 = att[1];
  alight.Attenuation2 = att[2];

  HRESULT hr = _d3d_device->SetLight(light_id, &alight);
  if (FAILED(hr)) {
    wdxdisplay9_cat.warning()
      << "Could not set light properties for " << light
      << " to id " << light_id << ": " << D3DERRORSTRING(hr) << "\n";
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_index_type
//       Access: Protected, Static
//  Description: Maps from the Geom's internal numeric type symbols
//               to DirectX's.
////////////////////////////////////////////////////////////////////
D3DFORMAT DXGraphicsStateGuardian9::
get_index_type(Geom::NumericType numeric_type) {
  switch (numeric_type) {
  case Geom::NT_uint16:
    return D3DFMT_INDEX16;

  case Geom::NT_uint32:
    return D3DFMT_INDEX32;
  }

  dxgsg9_cat.error()
    << "Invalid index NumericType value (" << (int)numeric_type << ")\n";
  return D3DFMT_INDEX16;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_material
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_material() {
  static Material empty;
  const Material *material;
  if (_target._material == (MaterialAttrib *)NULL ||
      _target._material->is_off()) {
    material = &empty;
  } else {
    material = _target._material->get_material();
  }

  D3DMATERIAL9 cur_material;
  cur_material.Diffuse = *(D3DCOLORVALUE *)(material->get_diffuse().get_data());
  cur_material.Ambient = *(D3DCOLORVALUE *)(material->get_ambient().get_data());
  cur_material.Specular = *(D3DCOLORVALUE *)(material->get_specular().get_data());
  cur_material.Emissive = *(D3DCOLORVALUE *)(material->get_emission().get_data());
  cur_material.Power = material->get_shininess();

  if (material->has_diffuse()) {
    // If the material specifies an diffuse color, use it.
    set_render_state(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
  } else {
    // Otherwise, the diffuse color comes from the object color.
    if (_has_material_force_color) {
      cur_material.Diffuse = *(D3DCOLORVALUE *)_material_force_color.get_data();
      set_render_state(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    } else {
      set_render_state(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    }
  }
  if (material->has_ambient()) {
    // If the material specifies an ambient color, use it.
    set_render_state(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
  } else {
    // Otherwise, the ambient color comes from the object color.
    if (_has_material_force_color) {
      cur_material.Ambient = *(D3DCOLORVALUE *)_material_force_color.get_data();
      set_render_state(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
    } else {
      set_render_state(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    }
  }

  if (material->has_specular()) {
    set_render_state(D3DRS_SPECULARENABLE, TRUE);
  } else {
    set_render_state(D3DRS_SPECULARENABLE, FALSE);
  }

  if (material->get_local()) {
    set_render_state(D3DRS_LOCALVIEWER, TRUE);
  } else {
    set_render_state(D3DRS_LOCALVIEWER, FALSE);
  }

  _d3d_device->SetMaterial(&cur_material);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_texture
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_texture() {
  DO_PSTATS_STUFF(_texture_state_pcollector.add_level(1));

  if (_texture_binding_shader_context==0) {
    if (_current_shader_context==0) {
      update_standard_texture_bindings();
    } else {
      disable_standard_texture_bindings();
      _current_shader_context->update_shader_texture_bindings(NULL,this);
    }
  } else {
    if (_current_shader_context==0) {
      _texture_binding_shader_context->disable_shader_texture_bindings(this);
      update_standard_texture_bindings();
    } else {
      _current_shader_context->
        update_shader_texture_bindings(_texture_binding_shader_context,this);
    }
  }
  _texture_binding_shader = _current_shader;
  _texture_binding_shader_context = _current_shader_context;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::disable_standard_texture_bindings
//       Access: Private
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
disable_standard_texture_bindings() {
  // Disable the texture stages that are no longer used.
  for (int i = 0; i < _num_active_texture_stages; i++) {
    HRESULT hr;

    hr = _d3d_device -> SetTexture (i, NULL);
    if (FAILED (hr)) {
      dxgsg9_cat.error()
        << "SetTexture ("
        << i
        << ", NULL) failed "
        << D3DERRORSTRING(hr);
    }
    set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
  }

  _num_active_texture_stages = 0;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::update_standard_texture_bindings
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
update_standard_texture_bindings() {
  DO_PSTATS_STUFF(_texture_state_pcollector.add_level(1));

  int num_stages = _effective_texture->get_num_on_ff_stages();
  int num_old_stages = _max_texture_stages;
  if (_state._texture != (TextureAttrib *)NULL) {
    num_old_stages = _state._texture->get_num_on_ff_stages();
  }

  nassertv(num_stages <= _max_texture_stages &&
           _num_active_texture_stages <= _max_texture_stages);

  _texture_involves_color_scale = false;

  // We have to match up the texcoord stage index to the order written
  // out by the DXGeomMunger.  This means the texcoord names are
  // written in the order they are referenced by the TextureAttrib,
  // except that if a name is repeated its index number is reused from
  // the first time.
  typedef pmap<const InternalName *, int> UsedTexcoordIndex;
  UsedTexcoordIndex used_texcoord_index;

  int i;
  for (i = 0; i < num_stages; i++) {
    TextureStage *stage = _effective_texture->get_on_ff_stage(i);
    Texture *texture = _effective_texture->get_on_texture(stage);
    nassertv(texture != (Texture *)NULL);

    const InternalName *name = stage->get_texcoord_name();

    // This pair of lines will get the next consecutive texcoord index
    // number if this is the first time we have referenced this
    // particular texcoord name; otherwise, it will return the same
    // index number it returned before.
    UsedTexcoordIndex::iterator ti = used_texcoord_index.insert(UsedTexcoordIndex::value_type(name, (int)used_texcoord_index.size())).first;
    int texcoord_index = (*ti).second;

    // We always reissue every stage in DX, just in case the texcoord
    // index or texgen mode or some other property has changed.
    TextureContext *tc = texture->prepare_now(_prepared_objects, this);
    apply_texture(i, tc);
    set_texture_blend_mode(i, stage);

    int texcoord_dimensions = 2;

    CPT(TransformState) tex_mat = TransformState::make_identity();
    if (_state._tex_matrix->has_stage(stage)) {
      tex_mat = _state._tex_matrix->get_transform(stage);
    }

    // Issue the texgen mode.
    TexGenAttrib::Mode mode = _effective_tex_gen->get_mode(stage);
    bool any_point_sprite = false;

    switch (mode) {
    case TexGenAttrib::M_off:
    case TexGenAttrib::M_light_vector:
      set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX, texcoord_index);
      break;

    case TexGenAttrib::M_eye_sphere_map:
      {
        set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                          texcoord_index | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        // This texture matrix, applied on top of the texcoord
        // computed by D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR,
        // approximates the effect produced by OpenGL's GL_SPHERE_MAP.
        static CPT(TransformState) sphere_map =
          TransformState::make_mat(LMatrix4f(0.33f, 0.0f, 0.0f, 0.0f,
                                             0.0f, 0.33f, 0.0f, 0.0f,
                                             0.0f, 0.0f, 1.0f, 0.0f,
                                             0.5f, 0.5f, 0.0f, 1.0f));
        tex_mat = tex_mat->compose(sphere_map);
        texcoord_dimensions = 3;
      }
      break;

    case TexGenAttrib::M_world_cube_map:
      // To achieve world reflection vector, we must transform camera
      // coordinates to world coordinates; i.e. apply the camera
      // transform.  In the case of a vector, we should not apply the
      // pos component of the transform.
      {
        set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                          texcoord_index | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        texcoord_dimensions = 3;
        CPT(TransformState) camera_transform = _scene_setup->get_camera_transform()->compose(_inv_cs_transform);
        tex_mat = tex_mat->compose(camera_transform->set_pos(LVecBase3f::zero()));
      }
      break;

    case TexGenAttrib::M_eye_cube_map:
      set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                        texcoord_index | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
      tex_mat = tex_mat->compose(_inv_cs_transform);
      texcoord_dimensions = 3;
      break;

    case TexGenAttrib::M_world_normal:
      // To achieve world normal, we must transform camera coordinates
      // to world coordinates; i.e. apply the camera transform.  In
      // the case of a normal, we should not apply the pos component
      // of the transform.
      {
        set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                          texcoord_index | D3DTSS_TCI_CAMERASPACENORMAL);
        texcoord_dimensions = 3;
        CPT(TransformState) camera_transform = _scene_setup->get_camera_transform()->compose(_inv_cs_transform);
        tex_mat = tex_mat->compose(camera_transform->set_pos(LVecBase3f::zero()));
      }
      break;

    case TexGenAttrib::M_eye_normal:
      set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                        texcoord_index | D3DTSS_TCI_CAMERASPACENORMAL);
      texcoord_dimensions = 3;
      tex_mat = tex_mat->compose(_inv_cs_transform);
      break;

    case TexGenAttrib::M_world_position:
      // To achieve world position, we must transform camera
      // coordinates to world coordinates; i.e. apply the
      // camera transform.
      {
        set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                          texcoord_index | D3DTSS_TCI_CAMERASPACEPOSITION);
        texcoord_dimensions = 3;
        CPT(TransformState) camera_transform = _scene_setup->get_camera_transform()->compose(_inv_cs_transform);
        tex_mat = tex_mat->compose(camera_transform);
      }
      break;

    case TexGenAttrib::M_eye_position:
      set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                        texcoord_index | D3DTSS_TCI_CAMERASPACEPOSITION);
      texcoord_dimensions = 3;
      tex_mat = tex_mat->compose(_inv_cs_transform);
      break;

    case TexGenAttrib::M_point_sprite:
      set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX, texcoord_index);
      any_point_sprite = true;
      break;

    case TexGenAttrib::M_constant:
      // To generate a constant UV(w) coordinate everywhere, we use
      // CAMERASPACEPOSITION coordinates, but we construct a special
      // matrix that flattens the existing values to zero and then
      // adds our desired value.

      // The only reason we need to specify CAMERASPACEPOSITION at
      // all, instead of using whatever texture coordinates (if any)
      // happen to be on the vertices, is because we need to guarantee
      // that there are 3-d texture coordinates, because of the
      // 3-component texture coordinate in get_constant_value().
      {
        set_texture_stage_state(i, D3DTSS_TEXCOORDINDEX,
                                texcoord_index | D3DTSS_TCI_CAMERASPACEPOSITION);
        texcoord_dimensions = 3;

        const TexCoord3f &v = _effective_tex_gen->get_constant_value(stage);
        CPT(TransformState) squash =
          TransformState::make_pos_hpr_scale(v, LVecBase3f::zero(),
                                             LVecBase3f::zero());
        tex_mat = tex_mat->compose(squash);
      }
      break;
    }

    set_render_state(D3DRS_POINTSPRITEENABLE, any_point_sprite);

    if (!tex_mat->is_identity()) {
      if (/*tex_mat->is_2d() &&*/ texcoord_dimensions <= 2) {
        // For 2-d texture coordinates, we have to reorder the matrix.
        LMatrix4f m = tex_mat->get_mat();
        m.set(m(0, 0), m(0, 1), m(0, 3), 0.0f,
              m(1, 0), m(1, 1), m(1, 3), 0.0f,
              m(3, 0), m(3, 1), m(3, 3), 0.0f,
              0.0f, 0.0f, 0.0f, 1.0f);
        _d3d_device->SetTransform(get_tex_mat_sym(i), (D3DMATRIX *)m.get_data());
        set_texture_stage_state(i, D3DTSS_TEXTURETRANSFORMFLAGS,
                                          D3DTTFF_COUNT2);
      } else {
        LMatrix4f m = tex_mat->get_mat();
        _d3d_device->SetTransform(get_tex_mat_sym(i), (D3DMATRIX *)m.get_data());
        DWORD transform_flags = texcoord_dimensions;
        if (m.get_col(3) != LVecBase4f(0.0f, 0.0f, 0.0f, 1.0f)) {
          // If we have a projected texture matrix, we also need to
          // set D3DTTFF_COUNT4.
          transform_flags = D3DTTFF_COUNT4 | D3DTTFF_PROJECTED;
        }
        set_texture_stage_state(i, D3DTSS_TEXTURETRANSFORMFLAGS,
                                          transform_flags);
      }

    } else {
      set_texture_stage_state(i, D3DTSS_TEXTURETRANSFORMFLAGS,
                                        D3DTTFF_DISABLE);
      // For some reason, "disabling" texture coordinate transforms
      // doesn't seem to be sufficient.  We'll load an identity matrix
      // to underscore the point.
      _d3d_device->SetTransform(get_tex_mat_sym(i), &_d3d_ident_mat);
    }
  }

  // Disable the texture stages that are no longer used.
  for (i = num_stages; i < _num_active_texture_stages; i++) {
    set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
    _d3d_device->SetTexture(i, NULL);
  }

  // Save the count of texture stages for next time.
  _num_active_texture_stages = num_stages;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_blending
//       Access: Protected, Virtual
//  Description: Called after any of the things that might change
//               blending state have changed, this function is
//               responsible for setting the appropriate color
//               blending mode based on the current properties.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_blending() {
  // Handle the color_write attrib.  If color_write is off, then
  // all the other blending-related stuff doesn't matter.  If the
  // device doesn't support color-write, we use blending tricks
  // to effectively disable color write.
  unsigned int color_channels =
    _target._color_write->get_channels() & _color_write_mask;
  if (color_channels == ColorWriteAttrib::C_off) {
    if (_target._color_write != _state._color_write) {
      if (_screen->_can_direct_disable_color_writes) {
        set_render_state(D3DRS_ALPHABLENDENABLE, FALSE);
        set_render_state(D3DRS_COLORWRITEENABLE, (DWORD)0x0);
      } else {
        set_render_state(D3DRS_ALPHABLENDENABLE, TRUE);
        set_render_state(D3DRS_SRCBLEND, D3DBLEND_ZERO);
        set_render_state(D3DRS_DESTBLEND, D3DBLEND_ONE);
      }
    }
    return;
  } else {
    if (_target._color_write != _state._color_write) {
      if (_screen->_can_direct_disable_color_writes) {
        set_render_state(D3DRS_COLORWRITEENABLE, color_channels);
      }
    }
  }

  CPT(ColorBlendAttrib) color_blend = _target._color_blend;
  ColorBlendAttrib::Mode color_blend_mode = _target._color_blend->get_mode();
  TransparencyAttrib::Mode transparency_mode = _target._transparency->get_mode();

  // Is there a color blend set?
  if (color_blend_mode != ColorBlendAttrib::M_none) {
    set_render_state(D3DRS_ALPHABLENDENABLE, TRUE);

    switch (color_blend_mode) {
    case ColorBlendAttrib::M_add:
      set_render_state(D3DRS_BLENDOP, D3DBLENDOP_ADD);
      break;

    case ColorBlendAttrib::M_subtract:
      set_render_state(D3DRS_BLENDOP, D3DBLENDOP_SUBTRACT);
      break;

    case ColorBlendAttrib::M_inv_subtract:
      set_render_state(D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT);
      break;

    case ColorBlendAttrib::M_min:
      set_render_state(D3DRS_BLENDOP, D3DBLENDOP_MIN);
      break;

    case ColorBlendAttrib::M_max:
      set_render_state(D3DRS_BLENDOP, D3DBLENDOP_MAX);
      break;
    }

    set_render_state(D3DRS_SRCBLEND,
        get_blend_func(color_blend->get_operand_a()));
    set_render_state(D3DRS_DESTBLEND,
        get_blend_func(color_blend->get_operand_b()));
    return;
  }

  // No color blend; is there a transparency set?
  switch (transparency_mode) {
  case TransparencyAttrib::M_none:
  case TransparencyAttrib::M_binary:
    break;

  case TransparencyAttrib::M_alpha:
  case TransparencyAttrib::M_multisample:
  case TransparencyAttrib::M_multisample_mask:
  case TransparencyAttrib::M_dual:
    set_render_state(D3DRS_ALPHABLENDENABLE, TRUE);
    set_render_state(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    set_render_state(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    set_render_state(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    return;

  default:
    dxgsg9_cat.error()
      << "invalid transparency mode " << (int)transparency_mode << endl;
    break;
  }

  // Nothing's set, so disable blending.
  set_render_state(D3DRS_ALPHABLENDENABLE, FALSE);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::enable_lighting
//       Access: Protected, Virtual
//  Description: Intended to be overridden by a derived class to
//               enable or disable the use of lighting overall.  This
//               is called by issue_light() according to whether any
//               lights are in use or not.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
enable_lighting(bool enable) {
  set_render_state(D3DRS_LIGHTING, (DWORD)enable);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_ambient_light
//       Access: Protected, Virtual
//  Description: Intended to be overridden by a derived class to
//               indicate the color of the ambient light that should
//               be in effect.  This is called by issue_light() after
//               all other lights have been enabled or disabled.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_ambient_light(const Colorf &color) {
  Colorf c = color;
  c.set(c[0] * _light_color_scale[0],
        c[1] * _light_color_scale[1],
        c[2] * _light_color_scale[2],
        c[3] * _light_color_scale[3]);

  set_render_state(D3DRS_AMBIENT, Colorf_to_D3DCOLOR(c));
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::enable_light
//       Access: Protected, Virtual
//  Description: Intended to be overridden by a derived class to
//               enable the indicated light id.  A specific Light will
//               already have been bound to this id via bind_light().
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
enable_light(int light_id, bool enable) {
  HRESULT hr = _d3d_device->LightEnable(light_id, enable);

  if (FAILED(hr)) {
    wdxdisplay9_cat.warning()
      << "Could not enable light " << light_id << ": "
      << D3DERRORSTRING(hr) << "\n";
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::enable_clip_plane
//       Access: Protected, Virtual
//  Description: Intended to be overridden by a derived class to
//               enable the indicated clip_plane id.  A specific
//               PlaneNode will already have been bound to this id via
//               bind_clip_plane().
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
enable_clip_plane(int plane_id, bool enable) {
  if (enable) {
    _clip_plane_bits |= ((DWORD)1 << plane_id);
  } else {
    _clip_plane_bits &= ~((DWORD)1 << plane_id);
  }
  set_render_state(D3DRS_CLIPPLANEENABLE, _clip_plane_bits);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::bind_clip_plane
//       Access: Protected, Virtual
//  Description: Called the first time a particular clip_plane has been
//               bound to a given id within a frame, this should set
//               up the associated hardware clip_plane with the clip_plane's
//               properties.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
bind_clip_plane(const NodePath &plane, int plane_id) {
  // Get the plane in "world coordinates" (actually, view
  // coordinates).  This means the plane in the coordinate space of
  // the camera, converted to DX's coordinate system.
  CPT(TransformState) transform = plane.get_transform(_scene_setup->get_camera_path());
  const LMatrix4f &plane_mat = transform->get_mat();
  LMatrix4f rel_mat = plane_mat * LMatrix4f::convert_mat(CS_yup_left, CS_default);
  const PlaneNode *plane_node;
  DCAST_INTO_V(plane_node, plane.node());
  Planef world_plane = plane_node->get_plane() * rel_mat;

  HRESULT hr = _d3d_device->SetClipPlane(plane_id, world_plane.get_data());
  if (FAILED(hr)) {
    wdxdisplay9_cat.warning()
      << "Could not set clip plane for " << plane
      << " to id " << plane_id << ": " << D3DERRORSTRING(hr) << "\n";
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::close_gsg
//       Access: Protected, Virtual
//  Description: This is called by the associated GraphicsWindow when
//               close_window() is called.  It should null out the
//               _win pointer and possibly free any open resources
//               associated with the GSG.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
close_gsg() {
  GraphicsStateGuardian::close_gsg();

  if (dxgsg9_cat.is_debug()) {
    dxgsg9_cat.debug()
      << "Closing GSG, prepared_objects count = " 
      << _prepared_objects->get_ref_count() << "\n";
  }

  // Unlike in OpenGL, in DX9 it is safe to try to explicitly release
  // any textures here.  And it may even be a good idea.
  if (_prepared_objects->get_ref_count() == 1) {
    release_all();

    // Now we need to actually delete all of the objects we just
    // released.
    Thread *current_thread = Thread::get_current_thread();
    _prepared_objects->begin_frame(this, current_thread);
    _prepared_objects->end_frame(current_thread);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::free_nondx_resources
//       Access: Public
//  Description: Frees some memory that was explicitly allocated
//               within the dxgsg.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
free_nondx_resources() {
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::free_d3d_device
//       Access: Public
//  Description: setup for re-calling dx_init(), this is not the final
//               exit cleanup routine (see dx_cleanup)
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
free_d3d_device() {
  // dont want a full reset of gsg, just a state clear
  _state_rs = 0;
  _state.clear_to_zero();
  // want gsg to pass all state settings through

  _dx_is_ready = false;

  if (_d3d_device != NULL) {
    for(int i = 0; i < D3D_MAXTEXTURESTAGES; i++) {
      // d3d should release this stuff internally anyway, but whatever
      _d3d_device->SetTexture(i, NULL);
    }
  }

  release_all();

  if (_d3d_device != NULL) {
    RELEASE(_d3d_device, dxgsg9, "d3dDevice", RELEASE_DOWN_TO_ZERO);
  }

  free_nondx_resources();

  // obviously we dont release ID3D9, just ID3DDevice9
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_draw_buffer
//       Access: Protected
//  Description: Sets up the glDrawBuffer to render into the buffer
//               indicated by the RenderBuffer object.  This only sets
//               up the color bits; it does not affect the depth,
//               stencil, accum layers.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_draw_buffer(const RenderBuffer &rb) {
  dxgsg9_cat.fatal() << "DX set_draw_buffer unimplemented!!!";
  return;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_read_buffer
//       Access: Protected
//  Description: Vestigial analog of glReadBuffer
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_read_buffer(const RenderBuffer &rb) {
  if (rb._buffer_type & RenderBuffer::T_front) {
    _cur_read_pixel_buffer = RenderBuffer::T_front;
  } else  if (rb._buffer_type & RenderBuffer::T_back) {
    _cur_read_pixel_buffer = RenderBuffer::T_back;
  } else {
    dxgsg9_cat.error() << "Invalid or unimplemented Argument to set_read_buffer!\n";
  }
  return;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_auto_rescale_normal
//       Access: Protected
//  Description: Issues the appropriate DX commands to either rescale
//               or normalize the normals according to the current
//               transform.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_auto_rescale_normal() {
  if (_internal_transform->has_identity_scale()) {
    // If there's no scale, don't normalize anything.
    set_render_state(D3DRS_NORMALIZENORMALS, false);
  } else {
    // If there is a scale, turn on normalization.
    set_render_state(D3DRS_NORMALIZENORMALS, true);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_light_color
//       Access: Public
//  Description: Returns the array of four floats that should be
//               issued as the light's color, as scaled by the current
//               value of _light_color_scale, in the case of
//               color_scale_via_lighting.
////////////////////////////////////////////////////////////////////
const D3DCOLORVALUE &DXGraphicsStateGuardian9::
get_light_color(Light *light) const {
  static Colorf c;
  c = light->get_color();
  c.set(c[0] * _light_color_scale[0],
        c[1] * _light_color_scale[1],
        c[2] * _light_color_scale[2],
        c[3] * _light_color_scale[3]);
  return *(D3DCOLORVALUE *)c.get_data();
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_blend_func
//       Access: Protected, Static
//  Description: Maps from ColorBlendAttrib::Operand to D3DBLEND
//               value.
////////////////////////////////////////////////////////////////////
D3DBLEND DXGraphicsStateGuardian9::
get_blend_func(ColorBlendAttrib::Operand operand) {
  switch (operand) {
  case ColorBlendAttrib::O_zero:
    return D3DBLEND_ZERO;

  case ColorBlendAttrib::O_one:
    return D3DBLEND_ONE;

  case ColorBlendAttrib::O_incoming_color:
    return D3DBLEND_SRCCOLOR;

  case ColorBlendAttrib::O_one_minus_incoming_color:
    return D3DBLEND_INVSRCCOLOR;

  case ColorBlendAttrib::O_fbuffer_color:
    return D3DBLEND_DESTCOLOR;

  case ColorBlendAttrib::O_one_minus_fbuffer_color:
    return D3DBLEND_INVDESTCOLOR;

  case ColorBlendAttrib::O_incoming_alpha:
    return D3DBLEND_SRCALPHA;

  case ColorBlendAttrib::O_one_minus_incoming_alpha:
    return D3DBLEND_INVSRCALPHA;

  case ColorBlendAttrib::O_fbuffer_alpha:
    return D3DBLEND_DESTALPHA;

  case ColorBlendAttrib::O_one_minus_fbuffer_alpha:
    return D3DBLEND_INVDESTALPHA;

  case ColorBlendAttrib::O_constant_color:
    // Not supported by DX.
    return D3DBLEND_SRCCOLOR;

  case ColorBlendAttrib::O_one_minus_constant_color:
    // Not supported by DX.
    return D3DBLEND_INVSRCCOLOR;

  case ColorBlendAttrib::O_constant_alpha:
    // Not supported by DX.
    return D3DBLEND_SRCALPHA;

  case ColorBlendAttrib::O_one_minus_constant_alpha:
    // Not supported by DX.
    return D3DBLEND_INVSRCALPHA;

  case ColorBlendAttrib::O_incoming_color_saturate:
    return D3DBLEND_SRCALPHASAT;
  }

  dxgsg9_cat.error()
    << "Unknown color blend operand " << (int)operand << endl;
  return D3DBLEND_ZERO;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::report_texmgr_stats
//       Access: Protected
//  Description: Reports the DX texture manager's activity to PStats.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
report_texmgr_stats() {

#ifdef DO_PSTATS
  HRESULT hr;
  hr = 0;

#ifdef TEXMGRSTATS_USES_GETAVAILVIDMEM
  DWORD dwTexTotal, dwTexFree, dwVidTotal, dwVidFree;

  if (_total_texmem_pcollector.is_active()) {
    DDSCAPS2 ddsCaps;

    ZeroMemory(&ddsCaps, sizeof(ddsCaps));

    ddsCaps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE;
    if (FAILED( hr = _d3d_device->GetAvailableVidMem(&ddsCaps, &dwVidTotal, &dwVidFree))) {
      dxgsg9_cat.fatal() << "report_texmgr GetAvailableVidMem for VIDMEM failed : result = " << D3DERRORSTRING(hr);
      throw_event("panda3d-render-error");
      return;
    }

    ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    if (FAILED( hr = _d3d_device->GetAvailableVidMem(&ddsCaps, &dwTexTotal, &dwTexFree))) {
      dxgsg9_cat.fatal() << "report_texmgr GetAvailableVidMem for TEXTURE failed : result = " << D3DERRORSTRING(hr);
      throw_event("panda3d-render-error");
      return;
    }
  }
#endif  // TEXMGRSTATS_USES_GETAVAILVIDMEM

  D3DDEVINFO_RESOURCEMANAGER all_resource_stats;
  ZeroMemory(&all_resource_stats, sizeof(D3DDEVINFO_RESOURCEMANAGER));

/* ***** DX9, GetInfo ( ) NOT IN DX9 */
/*
  if (!_tex_stats_retrieval_impossible) {
    hr = _d3d_device->GetInfo(D3DDEVINFOID_RESOURCEMANAGER, &all_resource_stats, sizeof(D3DDEVINFO_RESOURCEMANAGER));
    if (hr != D3D_OK) {
      if (hr == S_FALSE) {
        static int PrintedMsg = 2;
        if (PrintedMsg>0) {
          if (dxgsg9_cat.is_debug()) {
            dxgsg9_cat.debug()
              << "texstats GetInfo() requires debug DX DLLs to be installed!!\n";
          }
          ZeroMemory(&all_resource_stats, sizeof(D3DDEVINFO_RESOURCEMANAGER));
          _tex_stats_retrieval_impossible = true;
        }
      } else {
        dxgsg9_cat.error() << "GetInfo(RESOURCEMANAGER) failed to get tex stats: result = " << D3DERRORSTRING(hr);
        return;
      }
    }
  }
*/

  // Tell PStats about the state of the texture memory.

  if (_texmgrmem_total_pcollector.is_active()) {
    // report zero if no debug dlls, to signal this info is invalid
    _texmgrmem_total_pcollector.set_level(all_resource_stats.stats[D3DRTYPE_TEXTURE].TotalBytes);
    _texmgrmem_resident_pcollector.set_level(all_resource_stats.stats[D3DRTYPE_TEXTURE].WorkingSetBytes);
  }
#ifdef TEXMGRSTATS_USES_GETAVAILVIDMEM
  if (_total_texmem_pcollector.is_active()) {
    _total_texmem_pcollector.set_level(dwTexTotal);
    _used_texmem_pcollector.set_level(dwTexTotal - dwTexFree);
  }
#endif  // TEXMGRSTATS_USES_GETAVAILVIDMEM
#endif  // DO_PSTATS
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_context
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_context(DXScreenData *new_context) {
  nassertv(new_context != NULL);
  _screen = new_context;
  _d3d_device = _screen->_d3d_device;   //copy this one field for speed of deref
  _swap_chain = _screen->_swap_chain;   //copy this one field for speed of deref

  _screen->_dxgsg9 = this;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_render_target
//       Access: Protected
//  Description: Set render target to the backbuffer of current swap
//               chain.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_render_target() {
  if (_d3d_device == NULL) {
    return;
  }

  LPDIRECT3DSURFACE9 back = NULL, stencil = NULL;

  UINT swap_chain;

  /* ***** DX9 swap_chain ??? */
  swap_chain = 0;

  if (!_swap_chain)  //maybe fullscreen mode or main/single window
    _d3d_device->GetBackBuffer(swap_chain, 0, D3DBACKBUFFER_TYPE_MONO, &back);
  else
    _swap_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back);

  //wdxdisplay9_cat.debug() << "swapchain is " << _swap_chain << "\n";
  //wdxdisplay9_cat.debug() << "back buffer is " << back << "\n";

  _d3d_device->GetDepthStencilSurface(&stencil);

//  _d3d_device->SetRenderTarget(back, stencil);
  DWORD render_target_index;
  render_target_index = 0;
  _d3d_device->SetRenderTarget(render_target_index, back);

  if (back) {
    back->Release();
  }
  if (stencil) {
    stencil->Release();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_texture_blend_mode
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
set_texture_blend_mode(int i, const TextureStage *stage) {
  switch (stage->get_mode()) {
  case TextureStage::M_modulate:
  case TextureStage::M_modulate_glow:
  case TextureStage::M_modulate_gloss:
    // emulates GL_MODULATE glTexEnv mode
    set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_MODULATE);
    set_texture_stage_state(i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    set_texture_stage_state(i, D3DTSS_COLORARG2, D3DTA_CURRENT);
    set_texture_stage_state(i, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    set_texture_stage_state(i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    set_texture_stage_state(i, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    break;

  case TextureStage::M_decal:
    // emulates GL_DECAL glTexEnv mode
    set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHA);
    set_texture_stage_state(i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    set_texture_stage_state(i, D3DTSS_COLORARG2, D3DTA_CURRENT);

    set_texture_stage_state(i, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    set_texture_stage_state(i, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
    break;

  case TextureStage::M_replace:
    set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    set_texture_stage_state(i, D3DTSS_COLORARG1, D3DTA_TEXTURE);

    set_texture_stage_state(i, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    set_texture_stage_state(i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    break;

  case TextureStage::M_add:
    set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_ADD);
    set_texture_stage_state(i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    set_texture_stage_state(i, D3DTSS_COLORARG2, D3DTA_CURRENT);

    set_texture_stage_state(i, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    set_texture_stage_state(i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    set_texture_stage_state(i, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    break;

  case TextureStage::M_blend:
  case TextureStage::M_blend_color_scale:
    {
      set_texture_stage_state(i, D3DTSS_COLOROP, D3DTOP_LERP);
      set_texture_stage_state(i, D3DTSS_COLORARG0, D3DTA_TEXTURE);
      set_texture_stage_state(i, D3DTSS_COLORARG2, D3DTA_CURRENT);
      set_texture_stage_state(i, D3DTSS_COLORARG1, _constant_color_operand);

      set_texture_stage_state(i, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
      set_texture_stage_state(i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
      set_texture_stage_state(i, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    }
    break;

  case TextureStage::M_combine:
    // M_combine mode begins a collection of more sophisticated modes,
    // which match up more closely with DirectX's built-in modes.
    set_texture_stage_state
      (i, D3DTSS_COLOROP,
       get_texture_operation(stage->get_combine_rgb_mode(),
                             stage->get_rgb_scale()));

    switch (stage->get_num_combine_rgb_operands()) {
    case 3:
      set_texture_stage_state
        (i, D3DTSS_COLORARG0,
         get_texture_argument(stage->get_combine_rgb_source2(),
                              stage->get_combine_rgb_operand2()));
      // fall through

    case 2:
      set_texture_stage_state
        (i, D3DTSS_COLORARG2,
         get_texture_argument(stage->get_combine_rgb_source1(),
                              stage->get_combine_rgb_operand1()));
      // fall through

    case 1:
      set_texture_stage_state
        (i, D3DTSS_COLORARG1,
         get_texture_argument(stage->get_combine_rgb_source0(),
                              stage->get_combine_rgb_operand0()));
      // fall through

    default:
      break;
    }

    set_texture_stage_state
      (i, D3DTSS_ALPHAOP,
       get_texture_operation(stage->get_combine_alpha_mode(),
                             stage->get_alpha_scale()));

    switch (stage->get_num_combine_alpha_operands()) {
    case 3:
      set_texture_stage_state
        (i, D3DTSS_ALPHAARG0,
         get_texture_argument(stage->get_combine_alpha_source2(),
                              stage->get_combine_alpha_operand2()));
      // fall through

    case 2:
      set_texture_stage_state
        (i, D3DTSS_ALPHAARG2,
         get_texture_argument(stage->get_combine_alpha_source1(),
                              stage->get_combine_alpha_operand1()));
      // fall through

    case 1:
      set_texture_stage_state
        (i, D3DTSS_ALPHAARG1,
         get_texture_argument(stage->get_combine_alpha_source0(),
                              stage->get_combine_alpha_operand0()));
      // fall through

    default:
      break;
    }
    break;

  default:
    dxgsg9_cat.error()
      << "Unknown texture mode " << (int)stage->get_mode() << endl;
    break;
  }

  if (stage->get_saved_result()) {
    set_texture_stage_state(i, D3DTSS_RESULTARG, D3DTA_TEMP);
  } else {
    set_texture_stage_state(i, D3DTSS_RESULTARG, D3DTA_CURRENT);
  }

  if (stage->uses_color()) {
    // Set up the constant color for this stage.

    D3DCOLOR constant_color;
    if (stage->involves_color_scale() && _color_scale_enabled) {
      Colorf color = stage->get_color();
      color.set(color[0] * _current_color_scale[0],
                color[1] * _current_color_scale[1],
                color[2] * _current_color_scale[2],
                color[3] * _current_color_scale[3]);
      _texture_involves_color_scale = true;
      constant_color = Colorf_to_D3DCOLOR(color);
    } else {
      constant_color = Colorf_to_D3DCOLOR(stage->get_color());
    }
    if (_supports_texture_constant_color) {
      set_texture_stage_state(i, D3DTSS_CONSTANT, constant_color);
    } else {
      // This device doesn't supoprt a per-stage constant color, so we
      // have to fall back to a single constant color for the overall
      // texture pipeline.
      set_render_state(D3DRS_TEXTUREFACTOR, constant_color);
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::dx_cleanup
//       Access: Protected
//  Description: Clean up the DirectX environment, accounting for exit()
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
dx_cleanup() {
  if (!_d3d_device) {
    return;
  }

  free_nondx_resources();
  PRINT_REFCNT(dxgsg9, _d3d_device);

  // Do a safe check for releasing the D3DDEVICE. RefCount should be zero.
  // if we're called from exit(), _d3d_device may already have been released
  RELEASE(_d3d_device, dxgsg9, "d3dDevice", RELEASE_DOWN_TO_ZERO);
  _screen->_d3d_device = NULL;

  // Releasing pD3D is now the responsibility of the GraphicsPipe destructor
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::reset_d3d_device
//       Access: Protected
//  Description: This function checks current device's framebuffer
//               dimension against passed p_presentation_params backbuffer
//               dimension to determine a device reset if there is
//               only one window or it is the main window or
//               fullscreen mode then, it resets the device. Finally
//               it returns the new DXScreenData through parameter
//               screen
////////////////////////////////////////////////////////////////////
HRESULT DXGraphicsStateGuardian9::
reset_d3d_device(D3DPRESENT_PARAMETERS *presentation_params,
                 DXScreenData **screen) {
  HRESULT hr;

  nassertr(IS_VALID_PTR(presentation_params), E_FAIL);
  nassertr(IS_VALID_PTR(_screen->_d3d9), E_FAIL);
  nassertr(IS_VALID_PTR(_d3d_device), E_FAIL);

  // for windowed mode make sure our format matches the desktop fmt,
  // in case the desktop mode has been changed
  _screen->_d3d9->GetAdapterDisplayMode(_screen->_card_id, &_screen->_display_mode);
  presentation_params->BackBufferFormat = _screen->_display_mode.Format;

  // here we have to look at the _presentation_reset frame buffer dimension
  // if current window's dimension is bigger than _presentation_reset
  // we have to reset the device before creating new swapchain.
  // inorder to reset properly, we need to release all swapchains

  if (true || !(_screen->_swap_chain)
      || (_presentation_reset.BackBufferWidth < presentation_params->BackBufferWidth)
      || (_presentation_reset.BackBufferHeight < presentation_params->BackBufferHeight)) {
    if (wdxdisplay9_cat.is_debug()) {
      wdxdisplay9_cat.debug()
        << "swap_chain = " << _screen->_swap_chain << " _presentation_reset = "
        << _presentation_reset.BackBufferWidth << "x" << _presentation_reset.BackBufferHeight
        << " presentation_params = "
        << presentation_params->BackBufferWidth << "x" << presentation_params->BackBufferHeight << "\n";
    }

    get_engine()->reset_all_windows(false);// reset old swapchain by releasing

    if (_screen->_swap_chain) {  //other windows might be using bigger buffers
      _presentation_reset.BackBufferWidth = max(_presentation_reset.BackBufferWidth, presentation_params->BackBufferWidth);
      _presentation_reset.BackBufferHeight = max(_presentation_reset.BackBufferHeight, presentation_params->BackBufferHeight);

    } else {  // single window, must reset to the new presentation_params dimension
      _presentation_reset.BackBufferWidth = presentation_params->BackBufferWidth;
      _presentation_reset.BackBufferHeight = presentation_params->BackBufferHeight;
    }

    // Calling this forces all of the textures and vbuffers to be
    // regenerated, a prerequisite to calling Reset().
    release_all();

    // Just to be extra-conservative for now, we'll go ahead and
    // release the vbuffers and ibuffers at least; they're relatively
    // cheap to replace.
    release_all_vertex_buffers();
    release_all_index_buffers();

    // must be called before reset
    Thread *current_thread = Thread::get_current_thread();
    _prepared_objects->begin_frame(this, current_thread);

    // release graphics buffer surfaces
    {
      wdxGraphicsBuffer9 *graphics_buffer;
      list <wdxGraphicsBuffer9 *>::iterator graphics_buffer_iterator;

      for (graphics_buffer_iterator = _graphics_buffer_list.begin( ); graphics_buffer_iterator != _graphics_buffer_list.end( ); graphics_buffer_iterator++)
      {
        graphics_buffer = (*graphics_buffer_iterator);
        if (graphics_buffer -> _color_backing_store)
        {
          graphics_buffer -> _color_backing_store -> Release ( );
          graphics_buffer -> _color_backing_store = 0;
        }
        if (graphics_buffer -> _depth_backing_store)
        {
          graphics_buffer -> _depth_backing_store -> Release ( );
          graphics_buffer -> _depth_backing_store = 0;
        }
      }
    }

    this -> mark_new();
    hr = _d3d_device->Reset(&_presentation_reset);
    if (FAILED(hr) && hr != D3DERR_DEVICELOST) {
      return hr;
    }

    get_engine()->reset_all_windows(true);// reset with new swapchains by creating
    if (screen) {
      *screen = NULL;
    }

    if (presentation_params != &_screen->_presentation_params) {
      memcpy(&_screen->_presentation_params, presentation_params, sizeof(D3DPRESENT_PARAMETERS));
    }

    return hr;
  }

  // release the old swapchain and create a new one
  if (_screen && _screen->_swap_chain) {
    _screen->_swap_chain->Release();
    wdxdisplay9_cat.debug()
      << "swap chain " << _screen->_swap_chain << " is released\n";
    _screen->_swap_chain = NULL;
    hr = _d3d_device->CreateAdditionalSwapChain(presentation_params, &_screen->_swap_chain);
  }
  if (SUCCEEDED(hr)) {
    if (presentation_params != &_screen->_presentation_params) {
      memcpy(&_screen->_presentation_params, presentation_params, sizeof(D3DPRESENT_PARAMETERS));
    }
    if (screen) {
      *screen = _screen;
    }
  }
  return hr;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::check_cooperative_level
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
check_cooperative_level() {
  bool bDoReactivateWindow = false;
  if (_d3d_device == NULL) {
    return false;
  }

  HRESULT hr = _d3d_device->TestCooperativeLevel();

  if (SUCCEEDED(hr)) {
    nassertr(SUCCEEDED(_last_testcooplevel_result), false);
    return true;
  }

  switch (hr) {
  case D3DERR_DEVICENOTRESET:
    _dx_is_ready = false;

    // call this just in case
    _prepared_objects->begin_frame(this, Thread::get_current_thread());

    hr = reset_d3d_device(&_screen->_presentation_params);
    if (FAILED(hr)) {
      // I think this shouldnt fail unless I've screwed up the
      // _presentation_params from the original working ones somehow
      dxgsg9_cat.error()
        << "check_cooperative_level Reset() failed, hr = " << D3DERRORSTRING(hr);
    }

    hr = _d3d_device->TestCooperativeLevel();
    if (FAILED(hr)) {
      // internal chk, shouldnt fail
      dxgsg9_cat.error()
        << "TestCooperativeLevel following Reset() failed, hr = " << D3DERRORSTRING(hr);

    }

    _dx_is_ready = TRUE;
    break;

  case D3DERR_DEVICELOST:
    // sleep while the device is lost to free up the CPU
    Sleep (10);
    
    if (SUCCEEDED(_last_testcooplevel_result)) {
      if (_dx_is_ready) {
        _dx_is_ready = false;
        if (dxgsg9_cat.is_debug()) {
          dxgsg9_cat.debug() << "D3D Device was Lost, waiting...\n";
        }
      }
    }
  }

  _last_testcooplevel_result = hr;
  return SUCCEEDED(hr);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::show_frame
//       Access: Protected
//  Description: redraw primary buffer
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
show_frame() {
  if (_d3d_device == NULL) {
    return;
  }

DBG_S dxgsg9_cat.debug ( ) << "- - - - - DXGraphicsStateGuardian9::show_frame\n"; DBG_E

  // DEBUG
  /*
    EXPCL_DTOOL void reset_memory_stats (void);
    EXPCL_DTOOL void print_memory_stats (void);

    print_memory_stats ( );
    reset_memory_stats ( );
  */

  HRESULT hr;

  if (_swap_chain) {
    DWORD flags;
    flags = 0;
    
    hr = _swap_chain->Present((CONST RECT*)NULL, (CONST RECT*)NULL, (HWND)NULL, NULL, flags);
  } else {
    hr = _d3d_device->Present((CONST RECT*)NULL, (CONST RECT*)NULL, (HWND)NULL, NULL);
  }

  if (FAILED(hr)) {
    if (hr == D3DERR_DEVICELOST) {
      check_cooperative_level();
    } else {
      dxgsg9_cat.error()
        << "show_frame() - Present() failed" << D3DERRORSTRING(hr);
      throw_event("panda3d-render-error");
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::create_swap_chain
//       Access: Protected
//  Description:
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
create_swap_chain(DXScreenData *new_context) {
  // Instead of creating a device and rendering as d3ddevice->present()
  // we should render using SwapChain->present(). This is done to support
  // multiple windows rendering. For that purpose, we need to set additional
  // swap chains here.

  HRESULT hr;
  hr = new_context->_d3d_device->CreateAdditionalSwapChain(&new_context->_presentation_params, &new_context->_swap_chain);
  if (FAILED(hr)) {
    wdxdisplay9_cat.debug() << "Swapchain creation failed :"<<D3DERRORSTRING(hr)<<"\n";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::release_swap_chain
//       Access: Protected
//  Description: Release the swap chain on this DXScreenData
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
release_swap_chain(DXScreenData *new_context) {
  HRESULT hr;
  if (new_context->_swap_chain) {
    hr = new_context->_swap_chain->Release();
    if (FAILED(hr)) {
      wdxdisplay9_cat.debug() << "Swapchain release failed:" << D3DERRORSTRING(hr) << "\n";
      return false;
    }
  }
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::copy_pres_reset
//       Access: Protected
//  Description: copies the PresReset from passed DXScreenData
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
copy_pres_reset(DXScreenData *screen) {
  memcpy(&_presentation_reset, &_screen->_presentation_params, sizeof(D3DPRESENT_PARAMETERS));
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_d3d_min_type
//       Access: Protected, Static
//  Description:
////////////////////////////////////////////////////////////////////
D3DTEXTUREFILTERTYPE DXGraphicsStateGuardian9::
get_d3d_min_type(Texture::FilterType filter_type,
                 Texture::Format format) {
  switch (filter_type) {
  case Texture::FT_nearest:
    return D3DTEXF_POINT;

  case Texture::FT_linear:
    return D3DTEXF_LINEAR;

  case Texture::FT_nearest_mipmap_nearest:
    return D3DTEXF_POINT;

  case Texture::FT_linear_mipmap_nearest:
    return D3DTEXF_LINEAR;

  case Texture::FT_nearest_mipmap_linear:
    return D3DTEXF_POINT;

  case Texture::FT_linear_mipmap_linear:
    return D3DTEXF_LINEAR;

  case Texture::FT_shadow:
  case Texture::FT_default:
    return D3DTEXF_LINEAR;
  }

  dxgsg9_cat.error()
    << "Invalid FilterType value (" << (int)filter_type << ")\n";
  return D3DTEXF_POINT;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_d3d_mip_type
//       Access: Protected, Static
//  Description:
////////////////////////////////////////////////////////////////////
D3DTEXTUREFILTERTYPE DXGraphicsStateGuardian9::
get_d3d_mip_type(Texture::FilterType filter_type,
                 Texture::Format format) {
  switch (filter_type) {
  case Texture::FT_nearest:
    return D3DTEXF_NONE;

  case Texture::FT_linear:
    return D3DTEXF_NONE;

  case Texture::FT_nearest_mipmap_nearest:
    return D3DTEXF_POINT;

  case Texture::FT_linear_mipmap_nearest:
    return D3DTEXF_POINT;

  case Texture::FT_nearest_mipmap_linear:
    return D3DTEXF_LINEAR;

  case Texture::FT_linear_mipmap_linear:
    return D3DTEXF_LINEAR;

  case Texture::FT_shadow:
  case Texture::FT_default:
    return D3DTEXF_NONE;
  }

  dxgsg9_cat.error()
    << "Invalid FilterType value (" << (int)filter_type << ")\n";
  return D3DTEXF_NONE;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_texture_operation
//       Access: Protected, Static
//  Description: Returns the D3DTEXTUREOP value corresponding to the
//               indicated TextureStage::CombineMode enumerated type.
////////////////////////////////////////////////////////////////////
D3DTEXTUREOP DXGraphicsStateGuardian9::
get_texture_operation(TextureStage::CombineMode mode, int scale) {
  switch (mode) {
  case TextureStage::CM_undefined:
  case TextureStage::CM_replace:
    return D3DTOP_SELECTARG1;

  case TextureStage::CM_modulate:
    if (scale < 2) {
      return D3DTOP_MODULATE;
    } else if (scale < 4) {
      return D3DTOP_MODULATE2X;
    } else {
      return D3DTOP_MODULATE4X;
    }

  case TextureStage::CM_add:
    return D3DTOP_ADD;

  case TextureStage::CM_add_signed:
    if (scale < 2) {
      return D3DTOP_ADDSIGNED;
    } else {
      return D3DTOP_ADDSIGNED2X;
    }

  case TextureStage::CM_interpolate:
    return D3DTOP_LERP;

  case TextureStage::CM_subtract:
    return D3DTOP_SUBTRACT;

  case TextureStage::CM_dot3_rgb:
  case TextureStage::CM_dot3_rgba:
    return D3DTOP_DOTPRODUCT3;
  }

  dxgsg9_cat.error()
    << "Invalid TextureStage::CombineMode value (" << (int)mode << ")\n";
  return D3DTOP_DISABLE;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_texture_argument
//       Access: Protected
//  Description: Returns the D3DTA value corresponding to the
//               indicated TextureStage::CombineSource and
//               TextureStage::CombineOperand enumerated types.
////////////////////////////////////////////////////////////////////
DWORD DXGraphicsStateGuardian9::
get_texture_argument(TextureStage::CombineSource source,
                     TextureStage::CombineOperand operand) const {
  switch (source) {
  case TextureStage::CS_undefined:
  case TextureStage::CS_texture:
    return D3DTA_TEXTURE | get_texture_argument_modifier(operand);

  case TextureStage::CS_constant:
  case TextureStage::CS_constant_color_scale:
    return _constant_color_operand | get_texture_argument_modifier(operand);

  case TextureStage::CS_primary_color:
    return D3DTA_DIFFUSE | get_texture_argument_modifier(operand);

  case TextureStage::CS_previous:
    return D3DTA_CURRENT | get_texture_argument_modifier(operand);

  case TextureStage::CS_last_saved_result:
    return D3DTA_TEMP | get_texture_argument_modifier(operand);
  }
  dxgsg9_cat.error()
    << "Invalid TextureStage::CombineSource value (" << (int)source << ")\n";
  return D3DTA_CURRENT;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_texture_argument_modifier
//       Access: Protected, Static
//  Description: Returns the extra bits that modify the D3DTA
//               argument, according to the indicated
//               TextureStage::CombineOperand enumerated type.
////////////////////////////////////////////////////////////////////
DWORD DXGraphicsStateGuardian9::
get_texture_argument_modifier(TextureStage::CombineOperand operand) {
  switch (operand) {
  case TextureStage::CO_src_color:
    return 0;

  case TextureStage::CO_one_minus_src_color:
    return D3DTA_COMPLEMENT;

  case TextureStage::CO_src_alpha:
    return D3DTA_ALPHAREPLICATE;

  case TextureStage::CO_one_minus_src_alpha:
    return D3DTA_ALPHAREPLICATE | D3DTA_COMPLEMENT;

  case TextureStage::CO_undefined:
    break;
  }
  dxgsg9_cat.error()
    << "Invalid TextureStage::CombineOperand value (" << (int)operand << ")\n";
  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_primitive_up
//       Access: Protected
//  Description: Issues the DrawPrimitiveUP call to draw the indicated
//               primitive_type from the given buffer.  We add the
//               num_vertices parameter, so we can determine the size
//               of the buffer.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
draw_primitive_up(D3DPRIMITIVETYPE primitive_type,
      unsigned int primitive_count,
      unsigned int first_vertex,
      unsigned int num_vertices,
      const unsigned char *buffer, size_t stride) {

  // It appears that the common ATI driver seems to fail to draw
  // anything in the DrawPrimitiveUP() call if the address range of
  // the buffer supplied crosses over a multiple of 0x10000.  That's
  // incredibly broken, yet it undeniably appears to be true.  We'll
  // have to hack around it.

  const unsigned char *buffer_start = buffer + stride * first_vertex;
  const unsigned char *buffer_end = buffer_start + stride * num_vertices;

  if (buffer_end - buffer_start > 0x10000) {
    // Actually, the buffer doesn't fit within the required limit
    // anyway.  Go ahead and draw it and hope for the best.
    _d3d_device->DrawPrimitiveUP(primitive_type, primitive_count,
         buffer_start, stride);

  } else if ((((long)buffer_end ^ (long)buffer_start) & ~0xffff) == 0) {
    // No problem; we can draw the buffer directly.
    _d3d_device->DrawPrimitiveUP(primitive_type, primitive_count,
         buffer_start, stride);

  } else {
    // We have a problem--the buffer crosses over a 0x10000 boundary.
    // We have to copy the buffer to a temporary buffer that we can
    // draw from.
    unsigned char *safe_buffer_start = get_safe_buffer_start();
    memcpy(safe_buffer_start, buffer_start, buffer_end - buffer_start);
    _d3d_device->DrawPrimitiveUP(primitive_type, primitive_count,
         safe_buffer_start, stride);

  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::draw_indexed_primitive_up
//       Access: Protected
//  Description: Issues the DrawIndexedPrimitiveUP call to draw the
//               indicated primitive_type from the given buffer.  As
//               in draw_primitive_up(), above, the parameter list is
//               not exactly one-for-one with the
//               DrawIndexedPrimitiveUP() call, but it's similar (in
//               particular, we pass max_index instead of NumVertices,
//               which always seemed ambiguous to me).
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
draw_indexed_primitive_up(D3DPRIMITIVETYPE primitive_type,
        unsigned int min_index, unsigned int max_index,
        unsigned int num_primitives,
        const unsigned char *index_data,
        D3DFORMAT index_type,
        const unsigned char *buffer, size_t stride) {
  // As above, we'll hack the case of the buffer crossing the 0x10000
  // boundary.
  const unsigned char *buffer_start = buffer + stride * min_index;
  const unsigned char *buffer_end = buffer + stride * (max_index + 1);

  if (buffer_end - buffer_start > 0x10000) {
    // Actually, the buffer doesn't fit within the required limit
    // anyway.  Go ahead and draw it and hope for the best.
    _d3d_device->DrawIndexedPrimitiveUP
      (primitive_type, min_index, max_index - min_index + 1, num_primitives,
       index_data, index_type, buffer, stride);

  } else if ((((long)buffer_end ^ (long)buffer_start) & ~0xffff) == 0) {
    // No problem; we can draw the buffer directly.
    _d3d_device->DrawIndexedPrimitiveUP
      (primitive_type, min_index, max_index - min_index + 1, num_primitives,
       index_data, index_type, buffer, stride);

  } else {
    // We have a problem--the buffer crosses over a 0x10000 boundary.
    // We have to copy the buffer to a temporary buffer that we can
    // draw from.
    unsigned char *safe_buffer_start = get_safe_buffer_start();
    memcpy(safe_buffer_start, buffer_start, buffer_end - buffer_start);
    _d3d_device->DrawIndexedPrimitiveUP
      (primitive_type, min_index, max_index - min_index + 1, num_primitives,
       index_data, index_type, safe_buffer_start - stride * min_index, stride);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::check_dx_allocation
//       Access:
//  Description: This function is called after the creation of
//               textures, vertex buffers, and index buffers to
//               check if DirectX is out of memory. If DirectX is
//               out of memory and the LRU is being used, then
//               page out some memory. This function is a fail-safe
//               just in case another process allocates video
//               memory, DirectX is fragmented, or there are some
//               borderline memory allocation cases, ...
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
check_dx_allocation (HRESULT result, int allocation_size, int attempts)
{
  bool retry;

  retry = false;
  if (attempts <= 4)
  {
    switch (result) {
      case D3D_OK:
        break;

      case D3DERR_OUTOFVIDEOMEMORY:
      case E_OUTOFMEMORY:
        if (_lru) {
          // increase the page out size as the number of attempts increases
          if (_lru -> page_out_lru (allocation_size * attempts)) {
            retry = true;
          }
        }
        break;

      default:
        break;
    }
  }

  return retry;
}

////////////////////////////////////////////////////////////////////
//  DX stencil code section
////////////////////////////////////////////////////////////////////

static int dx_stencil_comparison_function_array [ ] =
{
  D3DCMP_NEVER,
  D3DCMP_LESS,
  D3DCMP_EQUAL,
  D3DCMP_LESSEQUAL,
  D3DCMP_GREATER,
  D3DCMP_NOTEQUAL,
  D3DCMP_GREATEREQUAL,
  D3DCMP_ALWAYS,
};

static int dx_stencil_operation_array [ ] =
{
  D3DSTENCILOP_KEEP,
  D3DSTENCILOP_ZERO,
  D3DSTENCILOP_REPLACE,
  D3DSTENCILOP_INCR,
  D3DSTENCILOP_DECR,
  D3DSTENCILOP_INVERT,

  D3DSTENCILOP_INCRSAT,
  D3DSTENCILOP_DECRSAT,
};

void dx_stencil_function (StencilRenderStates::StencilRenderState stencil_render_state, StencilRenderStates *stencil_render_states) {
  StencilType render_state_value;

  DXGraphicsStateGuardian9 *gsg;

  gsg = (DXGraphicsStateGuardian9 *) stencil_render_states -> _gsg;

  render_state_value = stencil_render_states -> get_stencil_render_state (stencil_render_state);

  // DEBUG
  if (false) {
    dxgsg9_cat.debug()
      << "SRS: " <<  StencilAttrib::stencil_render_state_name_array [stencil_render_state] << ", " << render_state_value << "\n";
  }

  switch (stencil_render_state)
  {
    case StencilRenderStates::SRS_front_enable:
      gsg -> set_render_state (D3DRS_STENCILENABLE, render_state_value);
      break;

    case StencilRenderStates::SRS_back_enable:
      if (gsg -> get_supports_two_sided_stencil()) {
        gsg -> set_render_state (D3DRS_TWOSIDEDSTENCILMODE, render_state_value);
      }
      break;

    case StencilRenderStates::SRS_front_comparison_function:
      gsg -> set_render_state (D3DRS_STENCILFUNC, dx_stencil_comparison_function_array [render_state_value]);
      break;
    case StencilRenderStates::SRS_front_stencil_fail_operation:
      gsg -> set_render_state (D3DRS_STENCILFAIL, dx_stencil_operation_array [render_state_value]);
      break;
    case StencilRenderStates::SRS_front_stencil_pass_z_fail_operation:
      gsg -> set_render_state (D3DRS_STENCILZFAIL, dx_stencil_operation_array [render_state_value]);
      break;
    case StencilRenderStates::SRS_front_stencil_pass_z_pass_operation:
      gsg -> set_render_state (D3DRS_STENCILPASS, dx_stencil_operation_array [render_state_value]);
      break;

    case StencilRenderStates::SRS_reference:
      gsg -> set_render_state (D3DRS_STENCILREF, render_state_value);
      break;

    case StencilRenderStates::SRS_read_mask:
      gsg -> set_render_state (D3DRS_STENCILMASK, render_state_value);
      break;
    case StencilRenderStates::SRS_write_mask:
      gsg -> set_render_state (D3DRS_STENCILWRITEMASK, render_state_value);
      break;

    case StencilRenderStates::SRS_back_comparison_function:
      if (gsg -> get_supports_two_sided_stencil()) {
        gsg -> set_render_state (D3DRS_CCW_STENCILFUNC, dx_stencil_comparison_function_array [render_state_value]);
      }
      break;
    case StencilRenderStates::SRS_back_stencil_fail_operation:
      if (gsg -> get_supports_two_sided_stencil()) {
        gsg -> set_render_state (D3DRS_CCW_STENCILFAIL, dx_stencil_operation_array [render_state_value]);
      }
      break;
    case StencilRenderStates::SRS_back_stencil_pass_z_fail_operation:
      if (gsg -> get_supports_two_sided_stencil()) {
        gsg -> set_render_state (D3DRS_CCW_STENCILZFAIL, dx_stencil_operation_array [render_state_value]);
      }
      break;
    case StencilRenderStates::SRS_back_stencil_pass_z_pass_operation:
      if (gsg -> get_supports_two_sided_stencil()) {
        gsg -> set_render_state (D3DRS_CCW_STENCILPASS, dx_stencil_operation_array [render_state_value]);
      }
      break;

    default:
      break;
  }
}

void dx_set_stencil_functions (StencilRenderStates *stencil_render_states) {
  if (stencil_render_states) {
    StencilRenderStates::StencilRenderState stencil_render_state;

    for (stencil_render_state = StencilRenderStates::SRS_first; stencil_render_state < StencilRenderStates::SRS_total; stencil_render_state = (StencilRenderStates::StencilRenderState) ((int) stencil_render_state + 1)) {
      stencil_render_states -> set_stencil_function (stencil_render_state, dx_stencil_function);
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::do_issue_stencil
//       Access: Protected
//  Description: Set stencil render states.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
do_issue_stencil() {

  const StencilAttrib *stencil;
  StencilRenderStates *stencil_render_states;

  stencil = _target._stencil;
  stencil_render_states = this -> _stencil_render_states;
  if (stencil && stencil_render_states) {

    // DEBUG
    if (false) {
      dxgsg9_cat.debug() << "STENCIL STATE CHANGE\n";
      dxgsg9_cat.debug() << "\n"
        << "SRS_front_enable " << stencil -> get_render_state (StencilAttrib::SRS_front_enable) << "\n"
        << "SRS_back_enable " << stencil -> get_render_state (StencilAttrib::SRS_back_enable) << "\n"
        << "SRS_front_comparison_function " << stencil -> get_render_state (StencilAttrib::SRS_front_comparison_function) << "\n"
        << "SRS_front_stencil_fail_operation " << stencil -> get_render_state (StencilAttrib::SRS_front_stencil_fail_operation) << "\n"
        << "SRS_front_stencil_pass_z_fail_operation " << stencil -> get_render_state (StencilAttrib::SRS_front_stencil_pass_z_fail_operation) << "\n"
        << "SRS_front_stencil_pass_z_pass_operation " << stencil -> get_render_state (StencilAttrib::SRS_front_stencil_pass_z_pass_operation) << "\n"
        << "SRS_reference " << stencil -> get_render_state (StencilAttrib::SRS_reference) << "\n"
        << "SRS_read_mask " << stencil -> get_render_state (StencilAttrib::SRS_read_mask) << "\n"
        << "SRS_write_mask " << stencil -> get_render_state (StencilAttrib::SRS_write_mask) << "\n"
        << "SRS_back_comparison_function " << stencil -> get_render_state (StencilAttrib::SRS_back_comparison_function) << "\n"
        << "SRS_back_stencil_fail_operation " << stencil -> get_render_state (StencilAttrib::SRS_back_stencil_fail_operation) << "\n"
        << "SRS_back_stencil_pass_z_fail_operation " << stencil -> get_render_state (StencilAttrib::SRS_back_stencil_pass_z_fail_operation) << "\n"
        << "SRS_back_stencil_pass_z_pass_operation " << stencil -> get_render_state (StencilAttrib::SRS_back_stencil_pass_z_pass_operation) << "\n";
    }

    bool on;

    on = false;
    stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_front_enable, stencil -> get_render_state (StencilAttrib::SRS_front_enable));
    if (stencil -> get_render_state (StencilAttrib::SRS_front_enable)) {
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_front_comparison_function, stencil -> get_render_state (StencilAttrib::SRS_front_comparison_function));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_front_stencil_fail_operation, stencil -> get_render_state (StencilAttrib::SRS_front_stencil_fail_operation));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_front_stencil_pass_z_fail_operation, stencil -> get_render_state (StencilAttrib::SRS_front_stencil_pass_z_fail_operation));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_front_stencil_pass_z_pass_operation, stencil -> get_render_state (StencilAttrib::SRS_front_stencil_pass_z_pass_operation));
      on = true;
    }

    stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_back_enable, stencil -> get_render_state (StencilAttrib::SRS_back_enable));
    if (stencil -> get_render_state (StencilAttrib::SRS_back_enable)) {
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_back_comparison_function, stencil -> get_render_state (StencilAttrib::SRS_back_comparison_function));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_back_stencil_fail_operation, stencil -> get_render_state (StencilAttrib::SRS_back_stencil_fail_operation));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_back_stencil_pass_z_fail_operation, stencil -> get_render_state (StencilAttrib::SRS_back_stencil_pass_z_fail_operation));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_back_stencil_pass_z_pass_operation, stencil -> get_render_state (StencilAttrib::SRS_back_stencil_pass_z_pass_operation));
      on = true;
    }

    if (on) {
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_reference, stencil -> get_render_state (StencilAttrib::SRS_reference));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_read_mask, stencil -> get_render_state (StencilAttrib::SRS_read_mask));
      stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_write_mask, stencil -> get_render_state (StencilAttrib::SRS_write_mask));
    }

    if (stencil -> get_render_state (StencilAttrib::SRS_clear)) {
      _d3d_device->Clear(0, NULL, D3DCLEAR_STENCIL, 0, 0.0f, stencil -> get_render_state (StencilAttrib::SRS_clear_value));
    }
  }
  else {

    // DEBUG
    if (false) {
      dxgsg9_cat.debug() << "STENCIL STATE CHANGE TO OFF \n";
    }

    stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_front_enable, 0);
    stencil_render_states -> set_stencil_render_state (true, StencilRenderStates::SRS_back_enable, 0);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: dxGraphicsStateGuardian9::calc_fb_properties
//       Access: Public
//  Description: Convert DirectX framebuffer format ids into a
//               FrameBufferProperties structure.
////////////////////////////////////////////////////////////////////
FrameBufferProperties DXGraphicsStateGuardian9::
calc_fb_properties(DWORD cformat, DWORD dformat,
                   DWORD multisampletype, DWORD multisamplequality) {
  FrameBufferProperties props;
  int index=0;
  int alpha=0;
  int color=0;
  switch (cformat) {
  case D3DFMT_R8G8B8:      index=0; color=24; alpha=0; break;
  case D3DFMT_A8R8G8B8:    index=0; color=24; alpha=8; break;
  case D3DFMT_X8R8G8B8:    index=0; color=24; alpha=0; break;
  case D3DFMT_R5G6B5:      index=0; color=16; alpha=0; break;
  case D3DFMT_X1R5G5B5:    index=0; color=15; alpha=0; break;
  case D3DFMT_A1R5G5B5:    index=0; color=15; alpha=1; break;
  case D3DFMT_A4R4G4B4:    index=0; color=12; alpha=4; break;
  case D3DFMT_R3G3B2:      index=0; color= 8; alpha=0; break;
  case D3DFMT_A8R3G3B2:    index=0; color= 8; alpha=8; break;
  case D3DFMT_X4R4G4B4:    index=0; color=12; alpha=0; break;
  case D3DFMT_A2B10G10R10: index=0; color=30; alpha=2; break;
  case D3DFMT_A8P8:        index=1; color= 8; alpha=8; break;
  case D3DFMT_P8:          index=1; color= 8; alpha=0; break;
  }
  props.set_color_bits(color);
  props.set_alpha_bits(alpha);
  if (index) {
    props.set_rgb_color(0);
    props.set_indexed_color(1);
  } else if (color) {
    props.set_rgb_color(1);
    props.set_indexed_color(0);
  }
  int depth=0;
  int stencil=0;
  switch (dformat) {
  case D3DFMT_D32:     depth=32; stencil=0; break;
  case D3DFMT_D15S1:   depth=15; stencil=1; break;
  case D3DFMT_D24S8:   depth=24; stencil=8; break;
  case D3DFMT_D16:     depth=16; stencil=0; break;
  case D3DFMT_D24X8:   depth=24; stencil=0; break;
  case D3DFMT_D24X4S4: depth=24; stencil=4; break;
  }
  props.set_depth_bits(depth);
  props.set_stencil_bits(stencil);
  if (multisampletype == D3DMULTISAMPLE_NONMASKABLE) {
    props.set_multisamples(2);
  } else {
    props.set_multisamples(multisampletype);
  }
  return props;
}


#define GAMMA_1 (255.0 * 256.0)

static bool _gamma_table_initialized = false;
static unsigned short _orignial_gamma_table [256 * 3];

void _create_gamma_table (float gamma, unsigned short *original_red_table, unsigned short *original_green_table, unsigned short *original_blue_table, unsigned short *red_table, unsigned short *green_table, unsigned short *blue_table) {
  int i;
  double gamma_correction;

  if (gamma <= 0.0) {
    // avoid divide by zero and negative exponents
    gamma = 1.0;
  }
  gamma_correction = 1.0 / (double) gamma;    
  
  for (i = 0; i < 256; i++) {
    double r;
    double g;
    double b;

    if (original_red_table) {
      r = (double) original_red_table [i] / GAMMA_1;
      g = (double) original_green_table [i] / GAMMA_1;
      b = (double) original_blue_table [i] / GAMMA_1;
    }
    else {    
      r = ((double) i / 255.0);
      g = r;
      b = r;
    }    

    r = pow (r, gamma_correction);
    g = pow (g, gamma_correction);
    b = pow (b, gamma_correction);

    if (r > 1.00) {
      r = 1.0;
    }
    if (g > 1.00) {
      g = 1.0;
    }
    if (b > 1.00) {
      b = 1.0;
    }

    r = r * GAMMA_1;    
    g = g * GAMMA_1;    
    b = b * GAMMA_1;    

    red_table [i] = r;
    green_table [i] = g;
    blue_table [i] = b;
  }    
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::get_gamma_table
//       Access: Public, Static
//  Description: Static function for getting the original gamma.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
get_gamma_table(void) {
  bool get;  

  get = false;
  if (_gamma_table_initialized == false) {
    HDC hdc = GetDC(NULL);

    if (hdc) {   
      if (GetDeviceGammaRamp (hdc, (LPVOID) _orignial_gamma_table)) {
        _gamma_table_initialized = true;
        get = true;
      }

      ReleaseDC (NULL, hdc);
    }
  }
  
  return get;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::static_set_gamma
//       Access: Public, Static
//  Description: Static function for setting gamma which is needed 
//               for atexit.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
static_set_gamma(bool restore, float gamma) {
  bool set;  
  HDC hdc = GetDC(NULL);

  set = false;
  if (hdc) {   
    unsigned short ramp [256 * 3];

    if (restore && _gamma_table_initialized) {    
      _create_gamma_table (gamma, &_orignial_gamma_table [0], &_orignial_gamma_table [256], &_orignial_gamma_table [512], &ramp [0], &ramp [256], &ramp [512]);
    }
    else {
      _create_gamma_table (gamma, 0, 0, 0, &ramp [0], &ramp [256], &ramp [512]);
    }

    if (SetDeviceGammaRamp (hdc, ramp)) {
      set = true;
    }
    
    ReleaseDC (NULL, hdc);
  }

  return set;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::set_gamma
//       Access: Published
//  Description: Non static version of setting gamma.  Returns true
//               on success.
////////////////////////////////////////////////////////////////////
bool DXGraphicsStateGuardian9::
set_gamma(float gamma) {
  bool set;

  set = static_set_gamma(false, gamma);
  if (set) {
    _gamma = gamma;  
  }

  return set;
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::restore_gamma
//       Access: Published
//  Description: Restore original gamma.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
restore_gamma() {
  static_set_gamma(true, 1.0f);
}

////////////////////////////////////////////////////////////////////
//     Function: DXGraphicsStateGuardian9::atexit_function
//       Access: Public, Static
//  Description: This function is passed to the atexit function.
////////////////////////////////////////////////////////////////////
void DXGraphicsStateGuardian9::
atexit_function(void) {
  static_set_gamma(true, 1.0f);
}
