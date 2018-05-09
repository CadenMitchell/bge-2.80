/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. 
 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2009 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/render/render_update.c
 *  \ingroup edrend
 */

#include <stdlib.h>
#include <string.h>

#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_workspace_types.h"
#include "DNA_world_types.h"
#include "DNA_windowmanager_types.h"

#include "DRW_engine.h"

#include "BLI_listbase.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_DerivedMesh.h"
#include "BKE_icons.h"
#include "BKE_layer.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_node.h"
#include "BKE_paint.h"
#include "BKE_scene.h"
#include "BKE_workspace.h"

#include "GPU_lamp.h"
#include "GPU_material.h"
#include "GPU_buffers.h"

#include "RE_engine.h"
#include "RE_pipeline.h"

#include "ED_node.h"
#include "ED_render.h"
#include "ED_view3d.h"

#include "DEG_depsgraph.h"

#include "WM_api.h"

#include "render_intern.h"  // own include

extern Material defmaterial;

/***************************** Render Engines ********************************/

void ED_render_scene_update(const DEGEditorUpdateContext *update_ctx, int updated)
{
	/* viewport rendering update on data changes, happens after depsgraph
	 * updates if there was any change. context is set to the 3d view */
	Main *bmain = update_ctx->bmain;
	Scene *scene = update_ctx->scene;
	ViewLayer *view_layer = update_ctx->view_layer;
	bContext *C;
	wmWindowManager *wm;
	wmWindow *win;
	static bool recursive_check = false;

	/* don't do this render engine update if we're updating the scene from
	 * other threads doing e.g. rendering or baking jobs */
	if (!BLI_thread_is_main())
		return;

	/* don't call this recursively for frame updates */
	if (recursive_check)
		return;

	/* Do not call if no WM available, see T42688. */
	if (BLI_listbase_is_empty(&bmain->wm))
		return;

	recursive_check = true;

	C = CTX_create();
	CTX_data_main_set(C, bmain);
	CTX_data_scene_set(C, scene);

	CTX_wm_manager_set(C, bmain->wm.first);
	wm = bmain->wm.first;
	
	for (win = wm->windows.first; win; win = win->next) {
		bScreen *sc = WM_window_get_active_screen(win);
		ScrArea *sa;
		ARegion *ar;
		
		CTX_wm_window_set(C, win);
		WorkSpace *workspace = BKE_workspace_active_get(win->workspace_hook);
		ViewRender *view_render = BKE_viewrender_get(win->scene, workspace);

		for (sa = sc->areabase.first; sa; sa = sa->next) {
			if (sa->spacetype != SPACE_VIEW3D)
				continue;

			for (ar = sa->regionbase.first; ar; ar = ar->next) {
				if (ar->regiontype != RGN_TYPE_WINDOW) {
					continue;
				}
				RegionView3D *rv3d = ar->regiondata;
				RenderEngine *engine = rv3d->render_engine;
				/* call update if the scene changed, or if the render engine
				 * tagged itself for update (e.g. because it was busy at the
				 * time of the last update) */
				if (engine && (updated || (engine->flag & RE_ENGINE_DO_UPDATE))) {

					CTX_wm_screen_set(C, sc);
					CTX_wm_area_set(C, sa);
					CTX_wm_region_set(C, ar);

					engine->flag &= ~RE_ENGINE_DO_UPDATE;
					engine->type->view_update(engine, C);

				}
				else {
					RenderEngineType *engine_type = RE_engines_find(view_render->engine_id);
					if ((engine_type->flag & RE_USE_LEGACY_PIPELINE) == 0) {
						if (updated) {
							DRW_notify_view_update(
							        (&(DRWUpdateContext){
							            .bmain = bmain,
							            .depsgraph = update_ctx->depsgraph,
							            .scene = scene,
							            .view_layer = view_layer,
							            .ar = ar,
							            .v3d = (View3D *)sa->spacedata.first,
							            .engine_type = engine_type
							        }));
						}
					}
				}
			}
		}
	}

	CTX_free(C);

	recursive_check = false;
}

void ED_render_engine_area_exit(Main *bmain, ScrArea *sa)
{
	/* clear all render engines in this area */
	ARegion *ar;
	wmWindowManager *wm = bmain->wm.first;

	if (sa->spacetype != SPACE_VIEW3D)
		return;

	for (ar = sa->regionbase.first; ar; ar = ar->next) {
		if (ar->regiontype != RGN_TYPE_WINDOW || !(ar->regiondata))
			continue;
		ED_view3d_stop_render_preview(wm, ar);
	}
}

void ED_render_engine_changed(Main *bmain)
{
	/* on changing the render engine type, clear all running render engines */
	for (bScreen *sc = bmain->screen.first; sc; sc = sc->id.next) {
		for (ScrArea *sa = sc->areabase.first; sa; sa = sa->next) {
			ED_render_engine_area_exit(bmain, sa);
		}
	}
	RE_FreePersistentData();
	/* Inform all render engines and draw managers. */
	DEGEditorUpdateContext update_ctx = {NULL};
	update_ctx.bmain = bmain;
	for (Scene *scene = bmain->scene.first; scene; scene = scene->id.next) {
		update_ctx.scene = scene;
		LISTBASE_FOREACH(ViewLayer *, view_layer, &scene->view_layers) {
			/* TDODO(sergey): Iterate over depsgraphs instead? */
			update_ctx.depsgraph = BKE_scene_get_depsgraph(scene, view_layer, true);
			update_ctx.view_layer = view_layer;
			ED_render_id_flush_update(&update_ctx, &scene->id);
		}
		if (scene->nodetree) {
			ntreeCompositUpdateRLayers(scene->nodetree);
		}
	}
}

/***************************** Updates ***********************************
 * ED_render_id_flush_update gets called from DEG_id_tag_update, to do   *
 * editor level updates when the ID changes. when these ID blocks are in *
 * the dependency graph, we can get rid of the manual dependency checks  */

static void render_engine_flag_changed(Main *bmain, int update_flag)
{
	bScreen *sc;
	ScrArea *sa;
	ARegion *ar;
	
	for (sc = bmain->screen.first; sc; sc = sc->id.next) {
		for (sa = sc->areabase.first; sa; sa = sa->next) {
			if (sa->spacetype != SPACE_VIEW3D)
				continue;
			
			for (ar = sa->regionbase.first; ar; ar = ar->next) {
				RegionView3D *rv3d;
				
				if (ar->regiontype != RGN_TYPE_WINDOW)
					continue;
				
				rv3d = ar->regiondata;
				if (rv3d->render_engine)
					rv3d->render_engine->update_flag |= update_flag;
				
			}
		}
	}
}

static int mtex_use_tex(MTex **mtex, int tot, Tex *tex)
{
	int a;

	if (!mtex)
		return 0;

	for (a = 0; a < tot; a++)
		if (mtex[a] && mtex[a]->tex == tex)
			return 1;
	
	return 0;
}

static int nodes_use_tex(bNodeTree *ntree, Tex *tex)
{
	bNode *node;

	for (node = ntree->nodes.first; node; node = node->next) {
		if (node->id) {
			if (node->id == (ID *)tex) {
				return 1;
			}
			else if (GS(node->id->name) == ID_MA) {
				if (mtex_use_tex(((Material *)node->id)->mtex, MAX_MTEX, tex))
					return 1;
			}
			else if (node->type == NODE_GROUP) {
				if (nodes_use_tex((bNodeTree *)node->id, tex))
					return 1;
			}
		}
	}

	return 0;
}

static int nodes_use_material(bNodeTree *ntree, Material *ma)
{
	bNode *node;

	for (node = ntree->nodes.first; node; node = node->next) {
		if (node->id) {
			if (node->id == (ID *)ma) {
				return 1;
			}
			else if (node->type == NODE_GROUP) {
				if (nodes_use_material((bNodeTree *)node->id, ma))
					return 1;
			}
		}
	}

	return 0;
}

static void material_changed(Main *bmain, Material *ma)
{
	Material *parent;
	Object *ob;
	Scene *scene;
	int texture_draw = false;

	/* icons */
	BKE_icon_changed(BKE_icon_id_ensure(&ma->id));

	/* glsl */
	if (ma->id.recalc & ID_RECALC) {
		if (!BLI_listbase_is_empty(&ma->gpumaterial)) {
			GPU_material_free(&ma->gpumaterial);
		}
	}

	/* find node materials using this */
	for (parent = bmain->mat.first; parent; parent = parent->id.next) {
		if (parent->use_nodes && parent->nodetree && nodes_use_material(parent->nodetree, ma)) {
			/* pass */
		}
		else {
			continue;
		}

		BKE_icon_changed(BKE_icon_id_ensure(&parent->id));

		if (parent->gpumaterial.first)
			GPU_material_free(&parent->gpumaterial);
	}

	/* find if we have a scene with textured display */
	for (scene = bmain->scene.first; scene; scene = scene->id.next) {
		if (scene->customdata_mask & CD_MASK_MTFACE) {
			texture_draw = true;
			break;
		}
	}

	/* find textured objects */
	if (texture_draw) {
		for (ob = bmain->object.first; ob; ob = ob->id.next) {
			DerivedMesh *dm = ob->derivedFinal;
			Material ***material = give_matarar(ob);
			short a, *totmaterial = give_totcolp(ob);

			if (dm && totmaterial && material) {
				for (a = 0; a < *totmaterial; a++) {
					if ((*material)[a] == ma) {
						GPU_drawobject_free(dm);
						break;
					}
				}
			}
		}
	}

}

static void lamp_changed(Main *bmain, Lamp *la)
{
	Object *ob;

	/* icons */
	BKE_icon_changed(BKE_icon_id_ensure(&la->id));

	/* glsl */
	for (ob = bmain->object.first; ob; ob = ob->id.next)
		if (ob->data == la && ob->gpulamp.first)
			GPU_lamp_free(ob);

	if (defmaterial.gpumaterial.first)
		GPU_material_free(&defmaterial.gpumaterial);
}

static int material_uses_texture(Material *ma, Tex *tex)
{
	if (mtex_use_tex(ma->mtex, MAX_MTEX, tex))
		return true;
	else if (ma->use_nodes && ma->nodetree && nodes_use_tex(ma->nodetree, tex))
		return true;
	
	return false;
}

static void texture_changed(Main *bmain, Tex *tex)
{
	Material *ma;
	Lamp *la;
	World *wo;
	Scene *scene;
	ViewLayer *view_layer;
	Object *ob;
	bNode *node;
	bool texture_draw = false;

	/* icons */
	BKE_icon_changed(BKE_icon_id_ensure(&tex->id));

	const eObjectMode object_mode = WM_windows_object_mode_get(bmain->wm.first);

	/* paint overlays */
	for (scene = bmain->scene.first; scene; scene = scene->id.next) {
		for (view_layer = scene->view_layers.first; view_layer; view_layer = view_layer->next) {
			BKE_paint_invalidate_overlay_tex(scene, view_layer, tex, object_mode);
		}
	}

	/* find materials */
	for (ma = bmain->mat.first; ma; ma = ma->id.next) {
		if (!material_uses_texture(ma, tex))
			continue;

		BKE_icon_changed(BKE_icon_id_ensure(&ma->id));

		if (ma->gpumaterial.first)
			GPU_material_free(&ma->gpumaterial);
	}

	/* find lamps */
	for (la = bmain->lamp.first; la; la = la->id.next) {
		if (mtex_use_tex(la->mtex, MAX_MTEX, tex)) {
			lamp_changed(bmain, la);
		}
		else if (la->nodetree && nodes_use_tex(la->nodetree, tex)) {
			lamp_changed(bmain, la);
		}
		else {
			continue;
		}
	}

	/* find worlds */
	for (wo = bmain->world.first; wo; wo = wo->id.next) {
		if (mtex_use_tex(wo->mtex, MAX_MTEX, tex)) {
			/* pass */
		}
		else if (wo->nodetree && nodes_use_tex(wo->nodetree, tex)) {
			/* pass */
		}
		else {
			continue;
		}

		BKE_icon_changed(BKE_icon_id_ensure(&wo->id));
		
		if (wo->gpumaterial.first)
			GPU_material_free(&wo->gpumaterial);		
	}

	/* find compositing nodes */
	for (scene = bmain->scene.first; scene; scene = scene->id.next) {
		if (scene->use_nodes && scene->nodetree) {
			for (node = scene->nodetree->nodes.first; node; node = node->next) {
				if (node->id == &tex->id)
					ED_node_tag_update_id(&scene->id);
			}
		}

		if (scene->customdata_mask & CD_MASK_MTFACE)
			texture_draw = true;
	}

	/* find textured objects */
	if (texture_draw) {
		for (ob = bmain->object.first; ob; ob = ob->id.next) {
			DerivedMesh *dm = ob->derivedFinal;
			Material ***material = give_matarar(ob);
			short a, *totmaterial = give_totcolp(ob);

			if (dm && totmaterial && material) {
				for (a = 0; a < *totmaterial; a++) {
					if (ob->matbits && ob->matbits[a])
						ma = ob->mat[a];
					else
						ma = (*material)[a];

					if (ma && material_uses_texture(ma, tex)) {
						GPU_drawobject_free(dm);
						break;
					}
				}
			}
		}
	}
}

static void world_changed(Main *UNUSED(bmain), World *wo)
{
	/* icons */
	BKE_icon_changed(BKE_icon_id_ensure(&wo->id));

	/* XXX temporary flag waiting for depsgraph proper tagging */
	wo->update_flag = 1;

	/* glsl */
	if (wo->id.recalc & ID_RECALC) {
		if (!BLI_listbase_is_empty(&defmaterial.gpumaterial)) {
			GPU_material_free(&defmaterial.gpumaterial);
		}
		if (!BLI_listbase_is_empty(&wo->gpumaterial)) {
			GPU_material_free(&wo->gpumaterial);
		}
	}
}

static void image_changed(Main *bmain, Image *ima)
{
	Tex *tex;

	/* icons */
	BKE_icon_changed(BKE_icon_id_ensure(&ima->id));

	/* textures */
	for (tex = bmain->tex.first; tex; tex = tex->id.next)
		if (tex->ima == ima)
			texture_changed(bmain, tex);
}

static void scene_changed(Main *bmain, Scene *scene)
{
	Object *ob;

	/* glsl */
	bool has_texture_mode = false;
	wmWindowManager *wm = bmain->wm.first;
	for (wmWindow *win = wm->windows.first; win; win = win->next) {
		WorkSpace *workspace = WM_window_get_active_workspace(win);
		if (workspace->object_mode & OB_MODE_TEXTURE_PAINT) {
			has_texture_mode = true;
			break;
		}
	}

	if (has_texture_mode) {
		for (ob = bmain->object.first; ob; ob = ob->id.next) {
			BKE_texpaint_slots_refresh_object(scene, ob);
			BKE_paint_proj_mesh_data_check(scene, ob, NULL, NULL, NULL, NULL);
			GPU_drawobject_free(ob->derivedFinal);
		}
	}
}

void ED_render_id_flush_update(const DEGEditorUpdateContext *update_ctx, ID *id)
{
	/* this can be called from render or baking thread when a python script makes
	 * changes, in that case we don't want to do any editor updates, and making
	 * GPU changes is not possible because OpenGL only works in the main thread */
	if (!BLI_thread_is_main()) {
		return;
	}
	Main *bmain = update_ctx->bmain;
	/* Internal ID update handlers. */
	switch (GS(id->name)) {
		case ID_MA:
			material_changed(bmain, (Material *)id);
			render_engine_flag_changed(bmain, RE_ENGINE_UPDATE_MA);
			break;
		case ID_TE:
			texture_changed(bmain, (Tex *)id);
			break;
		case ID_WO:
			world_changed(bmain, (World *)id);
			break;
		case ID_LA:
			lamp_changed(bmain, (Lamp *)id);
			break;
		case ID_IM:
		{
			image_changed(bmain, (Image *)id);
			break;
		}
		case ID_SCE:
			scene_changed(bmain, (Scene *)id);
			render_engine_flag_changed(bmain, RE_ENGINE_UPDATE_OTHER);
			break;
		default:
			render_engine_flag_changed(bmain, RE_ENGINE_UPDATE_OTHER);
			break;
	}
}


void ED_render_internal_init(void)
{
	RenderEngineType *ret = RE_engines_find(RE_engine_id_BLENDER_RENDER);
	
	ret->view_update = render_view3d_update;
	ret->render_to_view = render_view3d_draw;
	
}