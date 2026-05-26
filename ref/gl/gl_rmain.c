/* XASH_I73770_ENGINE_PATCH_BEGIN */
#if defined(XASH_I73770_ENGINE_PATCH) && (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O3,omit-frame-pointer")
#pragma GCC target ("sse4.1,sse4.2,avx")
#endif
/* XASH_I73770_ENGINE_PATCH_END */

/*
gl_rmain.c - renderer main loop
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gl_local.h"
#include "canvas.h"  // Canvas 2D API integration

// Prototype for the Canvas getter implemented in canvas_gl.c
canvas_t *R_GetCanvas( void );
#include "xash3d_mathlib.h"
#include "library.h"
#include "beamdef.h"
#include "entity_types.h"

#define MAX_PACKET_ENTITIES 1024
#define IsLiquidContents( cnt )	( cnt == CONTENTS_WATER || cnt == CONTENTS_SLIME || cnt == CONTENTS_LAVA )

float		gldepthmin, gldepthmax;
ref_instance_t	RI;

cvar_t *cl_walltrans = NULL;  // legacy visibility cvar (kept for compatibility)

/*
================
aspect_ratio

Manual 3D horizontal projection stretch.
1.0 = engine/default aspect
0.1..0.9 = stretched/wider-looking 3D image
>1.0 = opposite direction/squeezed horizontally

This intentionally affects only the 3D projection/frustum. HUD/menu/2D
remain controlled by the existing 2D path.
================
*/
static cvar_t *r_manual_aspect_ratio = NULL;

static int R_RankForRenderMode( int rendermode )
{
	switch( rendermode )
	{
	case kRenderTransTexture:
		return 1;	// draw second
	case kRenderTransAdd:
		return 2;	// draw third
	case kRenderGlow:
		return 3;	// must be last!
	}
	return 0;
}

void R_AllowFog( qboolean allowed )
{
	if( allowed )
	{
		if( glState.isFogEnabled && gl_fog.value )
			pglEnable( GL_FOG );
	}
	else
	{
		if( glState.isFogEnabled )
			pglDisable( GL_FOG );
	}
}

/*
===============
R_OpaqueEntity

Opaque entity can be brush or studio model but sprite
===============
*/
qboolean R_OpaqueEntity( cl_entity_t *ent )
{
	if( R_GetEntityRenderMode( ent ) == kRenderNormal )
	{
		switch( ent->curstate.renderfx )
		{
		case kRenderFxNone:
		case kRenderFxDeadPlayer:
		case kRenderFxLightMultiplier:
		case kRenderFxExplode:
			return true;
		}
	}
	return false;
}

/*
===============
R_TransEntityCompare

Sorting translucent entities by rendermode then by distance
===============
*/
static int R_TransEntityCompare( const void *a, const void *b )
{
	cl_entity_t	*ent1, *ent2;
	vec3_t		vecLen, org;
	float		dist1, dist2;
	int		rendermode1;
	int		rendermode2;
	int		rank1, rank2; // FPS-FIX #6: precomputar ranks evita 4 chamadas extras por comparação

	ent1 = *(cl_entity_t **)a;
	ent2 = *(cl_entity_t **)b;
	rendermode1 = R_GetEntityRenderMode( ent1 );
	rendermode2 = R_GetEntityRenderMode( ent2 );
	rank1 = R_RankForRenderMode( rendermode1 ); // calculado UMA vez
	rank2 = R_RankForRenderMode( rendermode2 ); // calculado UMA vez

	// sort by distance
	if( ent1->model->type != mod_brush || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else dist1 = 1000000000;

	if( ent2->model->type != mod_brush || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist2 = DotProduct( vecLen, vecLen );
	}
	else dist2 = 1000000000;

	if( dist1 > dist2 )
		return -1;
	if( dist1 < dist2 )
		return 1;

	// then sort by rendermode
	if( rank1 > rank2 )
		return 1;
	if( rank1 < rank2 )
		return -1;

	return 0;
}

/*
===============
R_WorldToScreen

Convert a given point from world into screen space
Returns true if we behind to screen
===============
*/
int R_WorldToScreen( const vec3_t point, vec3_t screen )
{
	// FPS-FIX #11: remover Matrix4x4_Copy desnecessário.
	// A matriz não é modificada aqui, então lemos direto de RI.worldviewProjectionMatrix.
	qboolean	behind;
	float		w;

	if( !point || !screen )
		return true;

	screen[0] = RI.worldviewProjectionMatrix[0][0] * point[0] + RI.worldviewProjectionMatrix[0][1] * point[1] + RI.worldviewProjectionMatrix[0][2] * point[2] + RI.worldviewProjectionMatrix[0][3];
	screen[1] = RI.worldviewProjectionMatrix[1][0] * point[0] + RI.worldviewProjectionMatrix[1][1] * point[1] + RI.worldviewProjectionMatrix[1][2] * point[2] + RI.worldviewProjectionMatrix[1][3];
	w         = RI.worldviewProjectionMatrix[3][0] * point[0] + RI.worldviewProjectionMatrix[3][1] * point[1] + RI.worldviewProjectionMatrix[3][2] * point[2] + RI.worldviewProjectionMatrix[3][3];
	screen[2] = 0.0f; // just so we have something valid here

	if( w < 0.001f )
	{
		behind = true;
	}
	else
	{
		float invw = 1.0f / w;
		screen[0] *= invw;
		screen[1] *= invw;
		behind = false;
	}

	return behind;
}

/*
===============
R_ScreenToWorld

Convert a given point from screen into world space
===============
*/
void R_ScreenToWorld( const vec3_t screen, vec3_t point )
{
	matrix4x4	screenToWorld;
	float	w;

	if( !point || !screen )
		return;

	Matrix4x4_Invert_Full( screenToWorld, RI.worldviewProjectionMatrix );

	point[0] = screen[0] * screenToWorld[0][0] + screen[1] * screenToWorld[0][1] + screen[2] * screenToWorld[0][2] + screenToWorld[0][3];
	point[1] = screen[0] * screenToWorld[1][0] + screen[1] * screenToWorld[1][1] + screen[2] * screenToWorld[1][2] + screenToWorld[1][3];
	point[2] = screen[0] * screenToWorld[2][0] + screen[1] * screenToWorld[2][1] + screen[2] * screenToWorld[2][2] + screenToWorld[2][3];
	w = screen[0] * screenToWorld[3][0] + screen[1] * screenToWorld[3][1] + screen[2] * screenToWorld[3][2] + screenToWorld[3][3];
	if( w != 0.0f ) VectorScale( point, ( 1.0f / w ), point );
}

/*
===============
R_PushScene
===============
*/
void R_PushScene( void )
{
	if( ++tr.draw_stack_pos >= MAX_DRAW_STACK )
		gEngfuncs.Host_Error( "draw stack overflow\n" );

	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_PopScene
===============
*/
void R_PopScene( void )
{
	if( --tr.draw_stack_pos < 0 )
		gEngfuncs.Host_Error( "draw stack underflow\n" );
	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_ClearScene
===============
*/
void R_ClearScene( void )
{
	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;

	// clear the scene befor start new frame
	if( gEngfuncs.drawFuncs->R_ClearScene != NULL )
		gEngfuncs.drawFuncs->R_ClearScene();

}

/*
===============
R_AddEntity
===============
*/
qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	if( !r_drawentities->value )
		return false; // not allow to drawing

	if( !clent || !clent->model )
		return false; // if set to invisible, skip

	if( FBitSet( clent->curstate.effects, EF_NODRAW ))
		return false; // done

	if( !R_ModelOpaque( clent->curstate.rendermode ) && CL_FxBlend( clent ) <= 0 )
		return true; // invisible

	switch( type )
	{
	case ET_FRAGMENTED:
		r_stats.c_client_ents++;
		break;
	case ET_TEMPENTITY:
		r_stats.c_active_tents_count++;
		break;
	default: break;
	}

	if( R_OpaqueEntity( clent ))
	{
		// opaque
		if( tr.draw_list->num_solid_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->solid_entities[tr.draw_list->num_solid_entities] = clent;
		tr.draw_list->num_solid_entities++;
	}
	else
	{
		// translucent
		if( tr.draw_list->num_trans_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->trans_entities[tr.draw_list->num_trans_entities] = clent;
		tr.draw_list->num_trans_entities++;
	}

	return true;
}

/*
=============
R_Clear
=============
*/
static void R_Clear( int bitMask )
{
	int	bits;

	// FPS-FIX #13: pglClearColor é chamado todo frame mas a cor raramente muda.
	// Cacheamos o estado e só chamamos quando necessário.
	{
		static qboolean s_last_overview = false;
		qboolean is_overview = ENGINE_GET_PARM( PARM_DEV_OVERVIEW ) ? true : false;
		if( is_overview != s_last_overview )
		{
			if( is_overview )
				pglClearColor( 0.0f, 1.0f, 0.0f, 1.0f ); // green background (Valve rules)
			else
				pglClearColor( 0.5f, 0.5f, 0.5f, 1.0f );
			s_last_overview = is_overview;
		}
	}

	bits = GL_DEPTH_BUFFER_BIT;

	if( glState.stencilEnabled )
		bits |= GL_STENCIL_BUFFER_BIT;

	bits &= bitMask;

	pglClear( bits );

	// change ordering for overview
	if( RI.drawOrtho )
	{
		gldepthmin = 1.0f;
		gldepthmax = 0.0f;
	}
	else
	{
		gldepthmin = 0.0f;
		gldepthmax = 1.0f;
	}

	pglDepthFunc( GL_LEQUAL );
	pglDepthRange( gldepthmin, gldepthmax );
}

//=============================================================================
/*
===============
R_GetFarClip
===============
*/
static float R_GetFarClip( void )
{
	if( WORLDMODEL && RI.drawWorld )
		return tr.movevars->zmax * 1.73f;
	return 2048.0f;
}

/*
===============
R_GetManualAspectRatio

Console command / CVAR:
	aspect_ratio 1.0	// normal
	aspect_ratio 0.5	// stretched
	aspect_ratio 0.1	// extreme stretched
===============
*/
static float R_GetManualAspectRatio( void )
{
	float value;

	if( !r_manual_aspect_ratio )
	{
		r_manual_aspect_ratio = gEngfuncs.Cvar_Get( "aspect_ratio", "1.0", FCVAR_GLCONFIG,
			"manual 3D horizontal aspect stretch: 1=normal, 0.1=extreme stretch, >1=squeeze" );
	}

	if( !r_manual_aspect_ratio )
		return 1.0f;

	value = r_manual_aspect_ratio->value;

	if( value < 0.1f )
		value = 0.1f;
	else if( value > 10.0f )
		value = 10.0f;

	return value;
}

/*
===============
R_StretchHorizontalFov

Scales only RI.fov_x, leaving fov_y intact. This gives the classic
"stretched" look without changing real resolution or viewport.
===============
*/
static float R_StretchHorizontalFov( float fov_x, float aspect_ratio )
{
	if( aspect_ratio == 1.0f )
		return fov_x;

	if( fov_x <= 1.0f || fov_x >= 179.0f )
		return fov_x;

	return atan( tan( DEG2RAD( fov_x ) * 0.5f ) * aspect_ratio ) * 2.0f / ( M_PI_F / 180.0f );
}

/*
===============
R_ApplyManualAspectRatio

Applied after water FOV wobble and before frustum/projection setup.
===============
*/
static void R_ApplyManualAspectRatio( void )
{
	float aspect_ratio;

	if( RI.drawOrtho || !RP_NORMALPASS() )
		return;

	aspect_ratio = R_GetManualAspectRatio();

	if( aspect_ratio != 1.0f )
		RI.fov_x = R_StretchHorizontalFov( RI.fov_x, aspect_ratio );
}

/*
===============
R_SetupFrustum
===============
*/
void R_SetupFrustum( void )
{
	const ref_overview_t	*ov = gEngfuncs.GetOverviewParms();

	if( RP_NORMALPASS() && ( ENGINE_GET_PARM( PARM_WATER_LEVEL ) >= 3 ) && ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
	{
		RI.fov_x = atan( tan( DEG2RAD( RI.fov_x ) / 2 ) * ( 0.97f + sin( gp_cl->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
		RI.fov_y = atan( tan( DEG2RAD( RI.fov_y ) / 2 ) * ( 1.03f - sin( gp_cl->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
	}

	// Manual 3D aspect/stretch CVAR. Does not require vid_restart.
	R_ApplyManualAspectRatio();

	// build the transformation matrix for the given view angles
	AngleVectors( RI.viewangles, RI.vforward, RI.vright, RI.vup );

	if( !r_lockfrustum.value )
	{
		VectorCopy( RI.vieworg, RI.cullorigin );
		VectorCopy( RI.vforward, RI.cull_vforward );
		VectorCopy( RI.vright, RI.cull_vright );
		VectorCopy( RI.vup, RI.cull_vup );
	}

	if( RI.drawOrtho )
		GL_FrustumInitOrtho( &RI.frustum, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
	else
	{
		// FPS-FIX #8: calcular farClip aqui e guardar em RI.farClip para
		// R_SetupProjectionMatrix reusar — evita 2ª chamada redundante.
		RI.farClip = R_GetFarClip();
		GL_FrustumInitProj( &RI.frustum, 0.0f, RI.farClip, RI.fov_x, RI.fov_y );
	}
}

/*
=============
R_SetupProjectionMatrix
=============
*/
static void R_SetupProjectionMatrix( matrix4x4 m )
{
	GLfloat	xMin, xMax, yMin, yMax, zNear, zFar;

	if( RI.drawOrtho )
	{
		const ref_overview_t *ov = gEngfuncs.GetOverviewParms();
		Matrix4x4_CreateOrtho( m, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
		return;
	}

	// FPS-FIX #8: RI.farClip ja foi preenchido em R_SetupFrustum (chamado antes).
	// So recalcula como fallback se vier zerado (ex: cubemap direto sem SetupFrustum).
	if( RI.farClip <= 0.0f )
		RI.farClip = R_GetFarClip();

	zNear = 4.0f;
	zFar = Q_max( 256.0f, RI.farClip );

	yMax = zNear * tan( RI.fov_y * M_PI_F / 360.0f );
	yMin = -yMax;

	xMax = zNear * tan( RI.fov_x * M_PI_F / 360.0f );
	xMin = -xMax;

	if( tr.rotation & 1 )
	{
		Matrix4x4_CreateProjection( m, yMax, yMin, xMax, xMin, zNear, zFar );
	}
	else
	{
		Matrix4x4_CreateProjection( m, xMax, xMin, yMax, yMin, zNear, zFar );
	}
}

/*
=============
R_SetupModelviewMatrix
=============
*/
static void R_SetupModelviewMatrix( matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	if( tr.rotation & 1 )
	{
		Matrix4x4_ConcatRotate( m, anglemod( -RI.viewangles[2] + 90 ), 1, 0, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[0], 0, 1, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[1], 0, 0, 1 );
	}
	else
	{
		Matrix4x4_ConcatRotate( m, -RI.viewangles[2], 1, 0, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[0], 0, 1, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[1], 0, 0, 1 );
	}
	Matrix4x4_ConcatTranslate( m, -RI.vieworg[0], -RI.vieworg[1], -RI.vieworg[2] );
}

/*
=============
R_LoadIdentity
=============
*/
void R_LoadIdentity( void )
{
	if( tr.modelviewIdentity ) return;

	Matrix4x4_LoadIdentity( RI.objectMatrix );
	Matrix4x4_Copy( RI.modelviewMatrix, RI.worldviewMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.modelviewMatrix );
	tr.modelviewIdentity = true;
}

/*
=============
R_RotateForEntity
=============
*/
void R_RotateForEntity( cl_entity_t *e )
{
	float	scale = 1.0f;

	if( e == CL_GetEntityByIndex( 0 ))
	{
		R_LoadIdentity();
		return;
	}

	if( e->model->type != mod_brush && e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	Matrix4x4_CreateFromEntity( RI.objectMatrix, e->angles, e->origin, scale );
	Matrix4x4_ConcatTransforms( RI.modelviewMatrix, RI.worldviewMatrix, RI.objectMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.modelviewMatrix );
	tr.modelviewIdentity = false;
}

/*
=============
R_TranslateForEntity
=============
*/
void R_TranslateForEntity( cl_entity_t *e )
{
	float	scale = 1.0f;

	if( e == CL_GetEntityByIndex( 0 ))
	{
		R_LoadIdentity();
		return;
	}

	if( e->model->type != mod_brush && e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	Matrix4x4_CreateFromEntity( RI.objectMatrix, vec3_origin, e->origin, scale );
	Matrix4x4_ConcatTransforms( RI.modelviewMatrix, RI.worldviewMatrix, RI.objectMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.modelviewMatrix );
	tr.modelviewIdentity = false;
}

/*
===============
R_FindViewLeaf
===============
*/
void R_FindViewLeaf( void )
{
	RI.oldviewleaf = RI.viewleaf;
	RI.viewleaf = gEngfuncs.Mod_PointInLeaf( RI.pvsorigin, WORLDMODEL->nodes );
}

/*
===============
R_SetupFrame
===============
*/
static void R_SetupFrame( void )
{
	// setup viewplane dist
	RI.viewplanedist = DotProduct( RI.vieworg, RI.vforward );

	// FPS-FIX #1: pglIsEnabled(GL_FOG) é um round-trip síncrono CPU<->GPU.
	// Cacheamos o estado em glState.isFogEnabled via R_AllowFog/R_DrawFog,
	// então não precisamos perguntar ao driver todo frame.
	// glState.isFogEnabled = pglIsEnabled( GL_FOG ); // REMOVIDO - fps-killer

	if( !gl_nosort.value )
	{
		// FPS-FIX #12: qsort com 0 ou 1 elemento é overhead puro.
		// Só ordena quando há pelo menos 2 entidades translúcidas.
		if( tr.draw_list->num_trans_entities > 1 )
			qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t* ), R_TransEntityCompare );
	}

	// current viewleaf
	if( RI.drawWorld )
	{
		RI.isSkyVisible = false; // unknown at this moment
		R_FindViewLeaf();
	}
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL( qboolean set_gl_state )
{
	R_SetupModelviewMatrix( RI.worldviewMatrix );
	R_SetupProjectionMatrix( RI.projectionMatrix );

	Matrix4x4_Concat( RI.worldviewProjectionMatrix, RI.projectionMatrix, RI.worldviewMatrix );

	if( !set_gl_state ) return;

	if( RP_NORMALPASS( ))
	{
		int x, x2, y, y2;

		// set up viewport (main, playersetup)
		// FPS-FIX #7: RI.viewport[N] * gpGlobals->width / gpGlobals->width == RI.viewport[N]
		// A divisão era código morto. Simplificado.
		x  = (int)floor( (float)RI.viewport[0] );
		x2 = (int)ceil(  (float)(RI.viewport[0] + RI.viewport[2]) );
		y  = (int)floor( (float)(gpGlobals->height - RI.viewport[1]) );
		y2 = (int)ceil(  (float)(gpGlobals->height - (RI.viewport[1] + RI.viewport[3])) );

		if( tr.rotation & 1 )
			pglViewport( y2, x, y - y2, x2 - x );
		else pglViewport( x, y2, x2 - x, y - y2 );
	}
	else
	{
		// envpass, mirrorpass
		pglViewport( RI.viewport[0], RI.viewport[1], RI.viewport[2], RI.viewport[3] );
	}

	pglMatrixMode( GL_PROJECTION );
	GL_LoadMatrix( RI.projectionMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.worldviewMatrix );

	if( FBitSet( RI.params, RP_CLIPPLANE ))
	{
		GLdouble	clip[4];
		mplane_t	*p = &RI.clipPlane;

		clip[0] = p->normal[0];
		clip[1] = p->normal[1];
		clip[2] = p->normal[2];
		clip[3] = -p->dist;

		pglClipPlane( GL_CLIP_PLANE0, clip );
		pglEnable( GL_CLIP_PLANE0 );
	}

	GL_Cull( GL_FRONT );

	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
}

/*
=============
R_EndGL
=============
*/
static void R_EndGL( void )
{
	if( RI.params & RP_CLIPPLANE )
		pglDisable( GL_CLIP_PLANE0 );
}

/*
=============
R_RecursiveFindWaterTexture

using to find source waterleaf with
watertexture to grab fog values from it
=============
*/
static gl_texture_t *R_RecursiveFindWaterTexture( const mnode_t *node, const mnode_t *ignore, qboolean down )
{
	gl_texture_t *tex = NULL;
	mnode_t *children[2];

	// assure the initial node is not null
	// we could check it here, but we would rather check it
	// outside the call to get rid of one additional recursion level
	Assert( node != NULL );

	// ignore solid nodes
	if( node->contents == CONTENTS_SOLID )
		return NULL;

	if( node->contents < 0 )
	{
		mleaf_t		*pleaf;
		msurface_t	**mark;
		int		i, c;

		// ignore non-liquid leaves
		if( node->contents != CONTENTS_WATER && node->contents != CONTENTS_LAVA && node->contents != CONTENTS_SLIME )
			 return NULL;

		// find texture
		pleaf = (mleaf_t *)node;
		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		for( i = 0; i < c; i++, mark++ )
		{
			if( (*mark)->flags & SURF_DRAWTURB && (*mark)->texinfo && (*mark)->texinfo->texture )
				return R_GetTexture( (*mark)->texinfo->texture->gl_texturenum );
		}

		// texture not found
		return NULL;
	}

	// this is a regular node
	// traverse children
	node_children( children, node, WORLDMODEL );

	if( children[0] && ( children[0] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( children[0], node, true );
		if( tex ) return tex;
	}

	if( children[1] && ( children[1] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( children[1], node, true );
		if( tex )	return tex;
	}

	// for down recursion, return immediately
	if( down ) return NULL;

	// texture not found, step up if any
	if( node->parent )
		return R_RecursiveFindWaterTexture( node->parent, node, false );

	// top-level node, bail out
	return NULL;
}

/*
=============
R_CheckFog

check for underwater fog
Using backward recursion to find waterline leaf
from underwater leaf (idea: XaeroX)
=============
*/
static void R_CheckFog( void )
{
	cl_entity_t	*ent;
	gl_texture_t	*tex;
	int		i, cnt, count;

	// quake global fog
	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
	{
		if( !tr.movevars->fog_settings )
		{
			if( glState.isFogEnabled ) // FPS-FIX #2: cache, não query síncrona
				pglDisable( GL_FOG );
			RI.fogEnabled = false;
			return;
		}

		// quake-style global fog
		RI.fogColor[0] = ((tr.movevars->fog_settings & 0xFF000000) >> 24) / 255.0f;
		RI.fogColor[1] = ((tr.movevars->fog_settings & 0xFF0000) >> 16) / 255.0f;
		RI.fogColor[2] = ((tr.movevars->fog_settings & 0xFF00) >> 8) / 255.0f;
		RI.fogDensity = ((tr.movevars->fog_settings & 0xFF) / 255.0f) * 0.01f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
		return;
	}

	RI.fogEnabled = false;

	if( RI.onlyClientDraw || ENGINE_GET_PARM( PARM_WATER_LEVEL ) < 3 || !RI.drawWorld || !RI.viewleaf )
	{
		if( RI.cached_waterlevel == 3 )
		{
			// in some cases waterlevel jumps from 3 to 1. Catch it
			RI.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );
			RI.cached_contents = CONTENTS_EMPTY;
			if( !RI.fogCustom )
			{
				glState.isFogEnabled = false;
				pglDisable( GL_FOG );
			}
		}
		return;
	}

	ent = gEngfuncs.CL_GetWaterEntity( RI.vieworg );
	if( ent && ent->model && ent->model->type == mod_brush && ent->curstate.skin < 0 )
		cnt = ent->curstate.skin;
	else cnt = RI.viewleaf->contents;

	RI.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );

	if( !IsLiquidContents( RI.cached_contents ) && IsLiquidContents( cnt ))
	{
		tex = NULL;

		// check for water texture
		if( ent && ent->model && ent->model->type == mod_brush )
		{
			msurface_t	*surf;

			count = ent->model->nummodelsurfaces;

			for( i = 0, surf = &ent->model->surfaces[ent->model->firstmodelsurface]; i < count; i++, surf++ )
			{
				if( surf->flags & SURF_DRAWTURB && surf->texinfo && surf->texinfo->texture )
				{
					tex = R_GetTexture( surf->texinfo->texture->gl_texturenum );
					RI.cached_contents = ent->curstate.skin;
					break;
				}
			}
		}
		else
		{
			tex = R_RecursiveFindWaterTexture( RI.viewleaf->parent, NULL, false );
			if( tex ) RI.cached_contents = RI.viewleaf->contents;
		}

		if( !tex ) return;	// no valid fogs

		// copy fog params
		RI.fogColor[0] = tex->fogParams[0] / 255.0f;
		RI.fogColor[1] = tex->fogParams[1] / 255.0f;
		RI.fogColor[2] = tex->fogParams[2] / 255.0f;
		RI.fogDensity = tex->fogParams[3] * 0.000025f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}
	else
	{
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}
}

/*
=============
R_CheckGLFog

special condition for Spirit 1.9
that used direct calls of glFog-functions
=============
*/
static void R_CheckGLFog( void )
{
#ifdef HACKS_RELATED_HLMODS
	if(( !RI.fogEnabled && !RI.fogCustom ) && glState.isFogEnabled && VectorIsNull( RI.fogColor )) // FPS-FIX #3: cache
	{
		// fill the fog color from GL-state machine
		pglGetFloatv( GL_FOG_COLOR, RI.fogColor );
		RI.fogSkybox = true;
	}
#endif
}

/*
=============
R_DrawFog

=============
*/
void R_DrawFog( void )
{
	if( !RI.fogEnabled || !gl_fog.value )
		return;

	pglEnable( GL_FOG );
	glState.isFogEnabled = true;

	// BUG-FIX: a versão anterior trocou a lógica. O original usava PARM_QUAKE_COMPATIBLE
	// para escolher EXP2 (Quake) vs EXP (Half-Life). RI.fogCustom é outra coisa.
	// FPS-FIX #11: cachear o fog mode — só chama pglFogi quando muda.
	{
		static int s_last_fog_mode = -1;
		int fog_mode = ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) ? GL_EXP2 : GL_EXP;
		if( fog_mode != s_last_fog_mode )
		{
			pglFogi( GL_FOG_MODE, fog_mode );
			s_last_fog_mode = fog_mode;
		}
	}

	pglFogf( GL_FOG_DENSITY, RI.fogDensity );
	pglFogfv( GL_FOG_COLOR, RI.fogColor );
	pglHint( GL_FOG_HINT, GL_FASTEST );
}

/*
=============
R_DrawEntitiesOnList
=============
*/
static void R_DrawEntitiesOnList( void )
{
	int	i;

	tr.blend = 1.0f;
	// FPS-FIX #5: GL_CheckForErrors() chama glGetError() que é uma query SÍNCRONA
	// que stalla o pipeline GPU. Foram removidas do hot-path e agora só rodam
	// em builds de debug. No release, use uma única checagem no fim do frame.
#ifdef _DEBUG
	GL_CheckForErrors();
#endif

// first draw solid entities
for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
{
    RI.currententity = tr.draw_list->solid_entities[i];
    RI.currentmodel = RI.currententity->model;

    Assert( RI.currententity != NULL );
    Assert( RI.currentmodel != NULL );

    switch( RI.currentmodel->type )
    {
    case mod_brush:
        R_DrawBrushModel( RI.currententity );
        break;
    case mod_alias:
        R_DrawAliasModel( RI.currententity );
        break;
    case mod_studio:
        R_DrawStudioModel( RI.currententity );
        break;
    default:
        break;
    }
}

	// quake-specific feature
	R_DrawAlphaTextureChains();

	// draw sprites seperately, because of alpha blending
	for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_sprite:
			R_DrawSpriteModel( RI.currententity );
			break;
		}
	}

	if( !RI.onlyClientDraw )
	{
		gEngfuncs.CL_DrawEFX( tr.frametime, false );
	}

	if( RI.drawWorld )
		gEngfuncs.pfnDrawNormalTriangles();

	// then draw translucent entities
	for( i = 0; i < tr.draw_list->num_trans_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->trans_entities[i];
		RI.currentmodel = RI.currententity->model;

		// handle studiomodels with custom rendermodes on texture
		if( RI.currententity->curstate.rendermode != kRenderNormal )
			tr.blend = CL_FxBlend( RI.currententity ) / 255.0f;
		else tr.blend = 1.0f; // draw as solid but sorted by distance

		if( tr.blend <= 0.0f ) continue;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_alias:
			R_DrawAliasModel( RI.currententity );
			break;
		case mod_studio:
			R_DrawStudioModel( RI.currententity );
			break;
		case mod_sprite:
			R_DrawSpriteModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	if( RI.drawWorld )
	{
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		gEngfuncs.pfnDrawTransparentTriangles ();
	}

	if( !RI.onlyClientDraw )
	{
		R_AllowFog( false );
		gEngfuncs.CL_DrawEFX( tr.frametime, true );
		R_AllowFog( true );
	}

	pglDisable( GL_BLEND );	// Trinity Render issues

	if( !RI.onlyClientDraw )
		R_DrawViewModel();
	gEngfuncs.CL_ExtraUpdate();

#ifdef _DEBUG
	GL_CheckForErrors(); // uma única checagem no fim, só em debug
#endif
}

/*
================
R_RenderScene

R_SetupRefParams must be called right before
================
*/
void R_RenderScene( void )
{
	if( !WORLDMODEL && RI.drawWorld )
		gEngfuncs.Host_Error( "%s: NULL worldmodel\n", __func__ );

	// frametime is valid only for normal pass
	if( RP_NORMALPASS( ))
		tr.frametime = gp_cl->time -   gp_cl->oldtime;
	else tr.frametime = 0.0;

	// begin a new frame
	tr.framecount++;

	R_PushDlights();

	R_SetupFrustum();
	R_SetupFrame();
	R_SetupGL( true );
	R_Clear( ~0 );

	R_MarkLeaves();
	R_DrawFog ();
	if( RI.drawWorld )
		R_AnimateRipples();

	R_CheckGLFog();
	R_DrawWorld();
	R_CheckFog();

	gEngfuncs.CL_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList();

	R_DrawWaterSurfaces();

	R_EndGL();
}

void R_GammaChanged( qboolean do_reset_gamma )
{
	if( do_reset_gamma )
	{
		// paranoia cubemap rendering
		if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
			gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}
	else
	{
		glConfig.softwareGammaUpdate = true;
		GL_RebuildLightmaps();
		glConfig.softwareGammaUpdate = false;
	}
}

static void R_CheckCvars( void )
{
	qboolean rebuild = false;

	if( FBitSet( gl_overbright.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_overbright.flags, FCVAR_CHANGED );
		rebuild = true;
	}

	if( FBitSet( r_vbo.flags, FCVAR_CHANGED ))
	{
		ClearBits( r_vbo.flags, FCVAR_CHANGED );

		R_EnableVBO( r_vbo.value ? true : false );
		if( R_HasEnabledVBO( ))
			R_GenerateVBO();

		if( gl_overbright.value )
			rebuild = true;
	}

	if( FBitSet( r_vbo_overbrightmode.flags, FCVAR_CHANGED ) && gl_overbright.value )
	{
		ClearBits( r_vbo_overbrightmode.flags, FCVAR_CHANGED );
		rebuild = true;
	}

	if( rebuild )
		R_GammaChanged( false );
}

/*
===============
R_BeginFrame
===============
*/
void R_BeginFrame( qboolean clearScene )
{
	// Legacy visibility cvars (for compatibility)
    if( !cl_walltrans )
     cl_walltrans = gEngfuncs.Cvar_Get( "cl_walltrans", "0", 0, "legacy transparency" );

	// ====================== VBO: alterna apenas quando o cvar MUDA ======================
	// FPS-FIX #9: chamar R_EnableVBO() todo frame tem custo de overhead desnecessario.
	// Guardamos o ultimo estado e so togglamos na mudanca real.
	extern void R_EnableVBO( qboolean enable );
	{
		static qboolean s_vbo_state_last = true; // assume VBO ligado no inicio
		qboolean vbo_desired = ( cl_walltrans && cl_walltrans->value > 0.0f ) ? false : true;
		if( vbo_desired != s_vbo_state_last )
		{
			R_EnableVBO( vbo_desired );
			s_vbo_state_last = vbo_desired;
		}
	}

	glConfig.softwareGammaUpdate = false;

	if(( gl_clear->value || ENGINE_GET_PARM( PARM_DEV_OVERVIEW )) &&
		clearScene && ENGINE_GET_PARM( PARM_CONNSTATE ) != ca_cinematic )
	{
		pglClear( GL_COLOR_BUFFER_BIT );
	}

	R_CheckCvars();

	R_Set2DMode( true );

	pglDrawBuffer( GL_BACK );

	if( FBitSet( gl_texture_nearest.flags|gl_lightmap_nearest.flags|gl_texture_anisotropy.flags|gl_texture_lodbias.flags, FCVAR_CHANGED ))
		R_SetTextureParameters();

	gEngfuncs.CL_ExtraUpdate ();

	// Canvas users should bracket their own draw calls with BeginFrame/EndFrame
	// while 2D projection is active. Do not keep Canvas state active globally;
	// it disables depth/cull/blend state and would contaminate later passes.
}

/*
===============
R_SetupRefParams

set initial params for renderer
===============
*/
void R_SetupRefParams( const ref_viewpass_t *rvp )
{
	RI.params = RP_NONE;
	RI.drawWorld = FBitSet( rvp->flags, RF_DRAW_WORLD );
	RI.onlyClientDraw = FBitSet( rvp->flags, RF_ONLY_CLIENTDRAW );
	RI.farClip = 0;

	if( !FBitSet( rvp->flags, RF_DRAW_CUBEMAP ))
		RI.drawOrtho = FBitSet( rvp->flags, RF_DRAW_OVERVIEW );
	else RI.drawOrtho = false;

	// setup viewport
	RI.viewport[0] = rvp->viewport[0];
	RI.viewport[1] = rvp->viewport[1];
	RI.viewport[2] = rvp->viewport[2];
	RI.viewport[3] = rvp->viewport[3];

	// calc FOV
	RI.fov_x = rvp->fov_x;
	RI.fov_y = rvp->fov_y;

	VectorCopy( rvp->vieworigin, RI.vieworg );
	VectorCopy( rvp->viewangles, RI.viewangles );
	VectorCopy( rvp->vieworigin, RI.pvsorigin );
}

/*
===============
R_RenderFrame
===============
*/
void R_RenderFrame( const ref_viewpass_t *rvp )
{
	if( r_norefresh->value )
		return;

	// setup the initial render params
	R_SetupRefParams( rvp );

	// FPS-FIX #10: pglFinish() bloqueia a CPU ate a GPU terminar TUDO.
	// gl_finish serve apenas para medir latencia de GPU em benchmarks/debug.
	// Em uso normal deve ficar 0. Se voce nao sabe o que e, deixe 0.
	if( gl_finish.value && RI.drawWorld )
		pglFinish();

	// completely override rendering
	if( gEngfuncs.drawFuncs->GL_RenderFrame != NULL )
	{
		tr.fCustomRendering = true;

		if( gEngfuncs.drawFuncs->GL_RenderFrame( rvp ))
		{
			R_GatherPlayerLight();
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	if( !RI.onlyClientDraw )
		R_RunViewmodelEvents();

	tr.realframecount++; // right called after viewmodel events
	R_RenderScene();

	return;
}

/*
===============
R_EndFrame
===============
*/
void R_EndFrame( void )
{
#if XASH_PSVITA
	VGL_ShimEndFrame();
#endif
#if !defined( XASH_GL_STATIC )
	GL2_ShimEndFrame();
#endif
	// flush any remaining 2D bits
	R_Set2DMode( false );

	gEngfuncs.GL_SwapBuffers();
}

/*
===============
R_DrawCubemapView
===============
*/
void R_DrawCubemapView( const vec3_t origin, const vec3_t angles, int size )
{
	ref_viewpass_t rvp;

	// basic params
	rvp.flags = rvp.viewentity = 0;
	SetBits( rvp.flags, RF_DRAW_WORLD );
	SetBits( rvp.flags, RF_DRAW_CUBEMAP );

	rvp.viewport[0] = rvp.viewport[1] = 0;
	rvp.viewport[2] = rvp.viewport[3] = size;
	rvp.fov_x = rvp.fov_y = 90.0f; // this is a final fov value

	// setup origin & angles
	VectorCopy( origin, rvp.vieworigin );
	VectorCopy( angles, rvp.viewangles );

	R_RenderFrame( &rvp );

	RI.viewleaf = NULL;		// force markleafs next frame
}

/*
===============
CL_FxBlend
===============
*/
int CL_FxBlend( cl_entity_t *e )
{
	int	blend = 0;
	float	offset, dist;
	vec3_t	tmp;

	offset = ((int)e->index ) * 363.0f; // Use ent index to de-sync these fx

	switch( e->curstate.renderfx )
	{
	case kRenderFxPulseSlowWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFastWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxPulseSlow:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFast:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxFadeSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 0 )
				e->curstate.renderamt -= 1;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 3 )
				e->curstate.renderamt -= 4;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 255 )
				e->curstate.renderamt += 1;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 252 )
				e->curstate.renderamt += 4;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeSlow:
		blend = 20 * sin( gp_cl->time * 4 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFast:
		blend = 20 * sin( gp_cl->time * 16 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFaster:
		blend = 20 * sin( gp_cl->time * 36 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerSlow:
		blend = 20 * (sin( gp_cl->time * 2 ) + sin( gp_cl->time * 17 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerFast:
		blend = 20 * (sin( gp_cl->time * 16 ) + sin( gp_cl->time * 23 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxHologram:
	case kRenderFxDistort:
		VectorCopy( e->origin, tmp );
		VectorSubtract( tmp, RI.vieworg, tmp );
		dist = DotProduct( tmp, RI.vforward );

		// turn off distance fade
		if( e->curstate.renderfx == kRenderFxDistort )
			dist = 1;

		if( dist <= 0 )
		{
			blend = 0;
		}
		else
		{
			e->curstate.renderamt = 180;
			if( dist <= 100 ) blend = e->curstate.renderamt;
			else blend = (int) ((1.0f - ( dist - 100 ) * ( 1.0f / 400.0f )) * e->curstate.renderamt );
			blend += gEngfuncs.COM_RandomLong( -32, 31 );
		}
		break;
	default:
		blend = e->curstate.renderamt;
		break;
	}

	blend = bound( 0, blend, 255 );

	return blend;
}
