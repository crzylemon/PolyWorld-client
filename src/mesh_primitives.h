/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: mesh_primitives.h                                                                   |
|   Purpose: boxes, spheres, cylinders, wedges, quads                                         |
\*-------------------------------------------------------------------------------------------*/

#ifndef MESH_PRIMITIVES_H
#define MESH_PRIMITIVES_H

#include "mesh_loader.h"
#include <stdbool.h>

bool create_sphere_mesh(MeshData* out, float radius, int segments, int rings);
bool create_cylinder_mesh(MeshData* out, float radius, float height, int segments);
bool create_box_mesh(MeshData* out, float hx, float hy, float hz);

#define MESH_CURVE_LOD_COUNT 7
int mesh_curve_lod_index(float radius_studs, int quality);
int mesh_curve_lod_sphere_segments(int lod);
int mesh_curve_lod_cylinder_segments(int lod);

bool create_wedge_mesh(MeshData* out, float hx, float hy, float hz);

bool create_quad_mesh(MeshData* out);

#endif
