// Filename: config_egg2sg.h
// Created by:  drose (01Oct99)
//
////////////////////////////////////////////////////////////////////

#ifndef CONFIG_EGG2SG_H
#define CONFIG_EGG2SG_H

#include <pandabase.h>

#include <coordinateSystem.h>
#include <typedef.h>
#include <notifyCategoryProxy.h>
#include <dconfig.h>

ConfigureDecl(config_egg2sg, EXPCL_PANDAEGG, EXPTP_PANDAEGG);
NotifyCategoryDecl(egg2sg, EXPCL_PANDAEGG, EXPTP_PANDAEGG);

extern EXPCL_PANDAEGG bool egg_mesh;
extern EXPCL_PANDAEGG bool egg_retesselate_coplanar;
extern EXPCL_PANDAEGG bool egg_unroll_fans;
extern EXPCL_PANDAEGG bool egg_show_tstrips;
extern EXPCL_PANDAEGG bool egg_show_qsheets;
extern EXPCL_PANDAEGG bool egg_show_quads;
extern EXPCL_PANDAEGG bool egg_false_color;
extern EXPCL_PANDAEGG bool egg_show_normals;
extern EXPCL_PANDAEGG double egg_normal_scale;
extern EXPCL_PANDAEGG bool egg_subdivide_polys;
extern EXPCL_PANDAEGG bool egg_consider_fans;
extern EXPCL_PANDAEGG double egg_max_tfan_angle;
extern EXPCL_PANDAEGG int egg_min_tfan_tris;
extern EXPCL_PANDAEGG double egg_coplanar_threshold;
extern EXPCL_PANDAEGG CoordinateSystem egg_coordinate_system;
extern EXPCL_PANDAEGG bool egg_ignore_mipmaps;
extern EXPCL_PANDAEGG bool egg_ignore_filters;
extern EXPCL_PANDAEGG bool egg_ignore_clamp;
extern EXPCL_PANDAEGG bool egg_always_decal_textures;
extern EXPCL_PANDAEGG bool egg_ignore_decals;
extern EXPCL_PANDAEGG bool egg_flatten;
extern EXPCL_PANDAEGG bool egg_flatten_siblings;
extern EXPCL_PANDAEGG bool egg_show_collision_solids;
extern EXPCL_PANDAEGG bool egg_keep_texture_pathnames;
extern EXPCL_PANDAEGG bool egg_load_classic_nurbs_curves;
extern EXPCL_PANDAEGG bool egg_accept_errors;

extern EXPCL_PANDAEGG void init_libegg2sg();

#endif
