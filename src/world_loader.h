/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: world_loader.h                                                                      |
|   Purpose: place XML -> entities                                                            |
\*-------------------------------------------------------------------------------------------*/

#ifndef WORLD_LOADER_H
#define WORLD_LOADER_H

#include "scene.h"
#include "physics.h"
#include <stdbool.h>
#include <stddef.h>

int world_load_from_xml(const char* xml_data, size_t len,
                        Scene* scene, PhysicsWorld* physics,
                        void* renderer);

void world_loader_invalidate_unit_meshes(void);

EntityID world_loader_display_ui_id(void);

#endif
