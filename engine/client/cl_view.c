/* XASH_I73770_ENGINE_PATCH_BEGIN */
#if defined(XASH_I73770_ENGINE_PATCH) && (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O3,omit-frame-pointer")
#pragma GCC target ("sse4.1,sse4.2,avx")
#endif
/* XASH_I73770_ENGINE_PATCH_END */

/*
cl_view.c - player rendering positioning
Copyright (C) 2009 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "const.h"
#include "entity_types.h"
#include "vgui_draw.h"
#include "sound.h"
#include "input.h" // touch
#include "platform/platform.h" // GL_UpdateSwapInterval
#include "server.h"
#include "esp_local.h"
#include <SDL.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/*
===============
V_CalcViewRect

calc frame rectangle (Quake1 style)
===============
*/
static void V_CalcViewRect( void )
{
	qboolean	full = false;
	int	sb_lines;
	float	size;

	if( Host_IsQuakeCompatible( ))
	{
		// intermission is always full screen
		if( cl.intermission ) size = 120.0f;
		else size = scr_viewsize.value;

		if( size >= 120.0f )
			sb_lines = 0;		// no status bar at all
		else if( size >= 110.0f )
			sb_lines = 24;		// no inventory
		else sb_lines = 48;

		if( scr_viewsize.value >= 100.0f )
		{
			full = true;
			size = 100.0f;
		}
		else size = scr_viewsize.value;

		if( cl.intermission )
		{
			size = 100.0f;
			sb_lines = 0;
			full = true;
		}
		size /= 100.0f;
	}
	else
	{
		full = true;
		sb_lines = 0;
		size = 1.0f;
	}

	clgame.viewport[2] = refState.width * size;
	clgame.viewport[3] = refState.height * size;

	if( clgame.viewport[3] > refState.height - sb_lines )
		clgame.viewport[3] = refState.height - sb_lines;
	if( clgame.viewport[3] > refState.height )
		clgame.viewport[3] = refState.height;

	clgame.viewport[0] = ( refState.width - clgame.viewport[2] ) / 2;
	if( full ) clgame.viewport[1] = 0;
	else clgame.viewport[1] = ( refState.height - sb_lines - clgame.viewport[3] ) / 2;

}

/*
===============
V_SetupViewModel
===============
*/
static void V_SetupViewModel( void )
{
	cl_entity_t	*view = &clgame.viewent;
	player_info_t	*info = &cl.players[cl.playernum];

	if( !cl.local.weaponstarttime )
		cl.local.weaponstarttime = cl.time;

	// setup the viewent variables
	view->curstate.colormap = (info->topcolor & 0xFFFF)|((info->bottomcolor << 8) & 0xFFFF);
	view->curstate.number = cl.playernum + 1;
	view->index = cl.playernum + 1;
	view->model = CL_ModelHandle( cl.local.viewmodel );
	view->curstate.modelindex = cl.local.viewmodel;
	view->curstate.sequence = cl.local.weaponsequence;
	view->curstate.rendermode = kRenderNormal;

	// alias models has another animation methods
	if( view->model && view->model->type == mod_studio )
	{
		view->curstate.animtime = cl.local.weaponstarttime;
		view->curstate.frame = 0.0f;
	}
}

/*
===============
V_SetRefParams
===============
*/
static void V_SetRefParams( ref_params_t *fd )
{
	memset( fd, 0, sizeof( ref_params_t ));

	// probably this is not needs
	VectorCopy( refState.vieworg, fd->vieworg );
	VectorCopy( refState.viewangles, fd->viewangles );

	fd->frametime = host.frametime;
	fd->time = cl.time;

	fd->intermission = cl.intermission;
	fd->paused = (cl.paused != 0);
	fd->spectator = (cls.spectator != 0);
	fd->onground = (cl.local.onground != -1);
	fd->waterlevel = cl.local.waterlevel;

	VectorCopy( cl.simvel, fd->simvel );
	VectorCopy( cl.simorg, fd->simorg );

	VectorCopy( cl.viewheight, fd->viewheight );
	fd->idealpitch = cl.local.idealpitch;

	VectorCopy( cl.viewangles, fd->cl_viewangles );
	fd->health = cl.local.health;
	VectorCopy( cl.crosshairangle, fd->crosshairangle );
	if( Host_IsQuakeCompatible( ))
		fd->viewsize = scr_viewsize.value;
	else fd->viewsize = 120.0f;

	VectorCopy( cl.punchangle, fd->punchangle );
	fd->maxclients = cl.maxclients;
	fd->viewentity = cl.viewentity;
	fd->playernum = cl.playernum;
	fd->max_entities = clgame.maxEntities;
	fd->demoplayback = cls.demoplayback;
	fd->hardware = 1; // OpenGL

	if( cl.first_frame || cl.skip_interp )
	{
		cl.first_frame = false;		// now can be unlocked
		fd->smoothing = true;		// NOTE: currently this used to prevent ugly un-duck effect while level is changed
	}
	else fd->smoothing = cl.local.pushmsec;		// enable smoothing in multiplayer by server request (AMX uses)

	// get pointers to movement vars and user cmd
	fd->movevars = &clgame.movevars;
	fd->cmd = &cl.cmd;

	// setup viewport
	fd->viewport[0] = clgame.viewport[0];
	fd->viewport[1] = clgame.viewport[1];
	fd->viewport[2] = clgame.viewport[2];
	fd->viewport[3] = clgame.viewport[3];

	fd->onlyClientDraw = 0;	// reset clientdraw
	fd->nextView = 0;		// reset nextview
}

/*
===============
V_MergeOverviewRefdef

merge refdef with overview settings
===============
*/
static void V_RefApplyOverview( ref_viewpass_t *rvp )
{
	ref_overview_t	*ov = &clgame.overView;
	float		aspect;
	float		size_x, size_y;
	vec2_t		mins, maxs;

	if( !CL_IsDevOverviewMode( ))
		return;

	// NOTE: Xash3D may use 16:9 or 16:10 aspects
	aspect = (float)refState.width / (float)refState.height;

	size_x = fabs( 8192.0f / ov->flZoom );
	size_y = fabs( 8192.0f / (ov->flZoom * aspect ));

	// compute rectangle
	ov->xLeft = -(size_x / 2);
	ov->xRight = (size_x / 2);
	ov->yTop = -(size_y / 2);
	ov->yBottom = (size_y / 2);

	if( CL_IsDevOverviewMode() == 1 )
	{
		Con_NPrintf( 0, " Overview: Zoom %.2f, Map Origin (%.2f, %.2f, %.2f), Z Min %.2f, Z Max %.2f, Rotated %i\n",
		ov->flZoom, ov->origin[0], ov->origin[1], ov->origin[2], ov->zNear, ov->zFar, ov->rotated );
	}

	VectorCopy( ov->origin, rvp->vieworigin );
	rvp->vieworigin[2] = ov->zFar + ov->zNear;
	Vector2Copy( rvp->vieworigin, mins );
	Vector2Copy( rvp->vieworigin, maxs );

	mins[!ov->rotated] += ov->xLeft;
	maxs[!ov->rotated] += ov->xRight;
	mins[ov->rotated] += ov->yTop;
	maxs[ov->rotated] += ov->yBottom;

	rvp->viewangles[0] = 90.0f;
	rvp->viewangles[1] = 90.0f;
	rvp->viewangles[2] = (ov->rotated) ? (ov->flZoom < 0.0f) ? 180.0f : 0.0f : (ov->flZoom < 0.0f) ? -90.0f : 90.0f;

	SetBits( rvp->flags, RF_DRAW_OVERVIEW );

	ref.dllFuncs.GL_OrthoBounds( mins, maxs );
}

/*
====================
V_CalcFov
====================
*/
static float V_CalcFov( float *fov_x, float width, float height )
{
	float	x, half_fov_y;

	if( *fov_x < 1.0f || *fov_x > 179.0f )
		*fov_x = 90.0f; // default value

	x = width / tan( DEG2RAD( *fov_x ) * 0.5f );
	half_fov_y = atan( height / x );

	return RAD2DEG( half_fov_y ) * 2;
}

/*
====================
V_AdjustFov
====================
*/
static void V_AdjustFov( float *fov_x, float *fov_y, float width, float height, qboolean lock_x )
{
	float x, y;

	if( width * 3 == 4 * height || width * 4 == height * 5 )
	{
		// 4:3 or 5:4 ratio
		return;
	}

	if( lock_x )
	{
		*fov_y = 2 * atan((width * 3) / (height * 4) * tan( *fov_y * M_PI_F / 360.0f * 0.5f )) * 360 / M_PI_F;
		return;
	}

	y = V_CalcFov( fov_x, 640, 480 );
	x = *fov_x;

	*fov_x = V_CalcFov( &y, height, width );
	if( *fov_x < x ) *fov_x = x;
	else *fov_y = y;
}

/*
=============
V_GetRefParams
=============
*/
static void V_GetRefParams( ref_params_t *fd, ref_viewpass_t *rvp )
{
	// part1: deniable updates
	VectorCopy( fd->simvel, cl.simvel );
	VectorCopy( fd->simorg, cl.simorg );
	VectorCopy( fd->punchangle, cl.punchangle );
	VectorCopy( fd->viewheight, cl.viewheight );

	// part2: really used updates
	VectorCopy( fd->crosshairangle, cl.crosshairangle );
	VectorCopy( fd->cl_viewangles, cl.viewangles );

	// setup ref_viewpass
	rvp->viewport[0] = fd->viewport[0];
	rvp->viewport[1] = fd->viewport[1];
	rvp->viewport[2] = fd->viewport[2];
	rvp->viewport[3] = fd->viewport[3];

	VectorCopy( fd->vieworg, rvp->vieworigin );
	VectorCopy( fd->viewangles, rvp->viewangles );

	rvp->viewentity = fd->viewentity;

	// calc FOV
	rvp->fov_x = bound( 10.0f, cl.local.scr_fov, 150.0f ); // this is a final fov value

	// first we need to compute FOV and other things that needs for frustum properly work
	rvp->fov_y = V_CalcFov( &rvp->fov_x, clgame.viewport[2], clgame.viewport[3] );

	// adjust FOV for widescreen
	if( refState.wideScreen && r_adjust_fov.value )
		V_AdjustFov( &rvp->fov_x, &rvp->fov_y, clgame.viewport[2], clgame.viewport[3], false );

	rvp->flags = 0;

	if( fd->onlyClientDraw )
		SetBits( rvp->flags, RF_ONLY_CLIENTDRAW );
	SetBits( rvp->flags, RF_DRAW_WORLD );
}

/*
==================
V_PreRender

==================
*/
qboolean V_PreRender( void )
{
	// too early
	if( !ref.initialized )
		return false;

	if( host.status == HOST_SLEEP )
		return false;

	// if the screen is disabled (loading plaque is up)
	if( cls.disable_screen )
	{
		if(( host.realtime - cls.disable_screen ) > cl_timeout.value )
		{
			Con_Reportf( "%s: loading plaque timed out\n", __func__ );
			cls.disable_screen = 0.0f;
		}
		return false;
	}

	V_CheckGamma();

	ref.dllFuncs.R_BeginFrame( !cl.paused && ( cls.state == ca_active ));

	GL_UpdateSwapInterval( );

	return true;
}

//============================================================================

/*
==================
V_RenderView

==================
*/
void V_RenderView( void )
{
	// HACKHACK: make ref params static
	// not really critical but allows client.dll to take address of refdef and don't trigger ASan
	static ref_params_t	rp;
	ref_viewpass_t	rvp;
	int		viewnum = 0;

	if( !cl.video_prepped || ( !ui_renderworld.value && UI_IsVisible() && !cl.background ))
		return; // still loading

	V_CalcViewRect ();	// compute viewport rectangle
	V_SetRefParams( &rp );
	V_SetupViewModel ();
	ref.dllFuncs.R_Set2DMode( false );
	SCR_DirtyScreen();
	ref.dllFuncs.GL_BackendStartFrame ();

	do
	{
		clgame.dllFuncs.pfnCalcRefdef( &rp );
		V_GetRefParams( &rp, &rvp );
		V_RefApplyOverview( &rvp );

		if( viewnum == 0 && FBitSet( rvp.flags, RF_ONLY_CLIENTDRAW ))
		{
			ref.dllFuncs.R_ClearScreen();
		}

		GL_RenderFrame( &rvp );
		S_UpdateFrame( &rvp );
		viewnum++;

	} while( rp.nextView );

	// draw debug triangles on a server
	SV_DrawDebugTriangles ();
	ref.dllFuncs.GL_BackendEndFrame ();
}

#define POINT_SIZE		16.0f
#define NODE_INTERVAL_X(x)	(x * 16.0f)
#define NODE_INTERVAL_Y(x)	(x * 16.0f)

static void R_DrawLeafNode( float x, float y, float scale )
{
	float downScale = scale * 0.25f;// * POINT_SIZE;

	ref.dllFuncs.R_DrawStretchPic( x - downScale * 0.5f, y - downScale * 0.5f, downScale, downScale, 0, 0, 1, 1, R_GetBuiltinTexture( REF_PARTICLE_TEXTURE ) );
}

static void R_DrawNodeConnection( float x, float y, float x2, float y2 )
{
	ref.dllFuncs.Begin( TRI_LINES );
		ref.dllFuncs.Vertex3f( x, y, 0 );
		ref.dllFuncs.Vertex3f( x2, y2, 0 );
	ref.dllFuncs.End( );
}

static void R_ShowTree_r( mnode_t *node, float x, float y, float scale, int shownodes, mleaf_t *viewleaf )
{
	float	downScale = scale * 0.8f;

	downScale = Q_max( downScale, 1.0f );

	if( !node ) return;

	world.recursion_level++;

	if( node->contents < 0 )
	{
		mleaf_t	*leaf = (mleaf_t *)node;

		if( world.recursion_level > world.max_recursion )
			world.max_recursion = world.recursion_level;

		if( shownodes == 1 )
		{
			if( cl.worldmodel->leafs == leaf )
				ref.dllFuncs.Color4f( 1.0f, 1.0f, 1.0f, 1.0f );
			else if( viewleaf && viewleaf == leaf )
				ref.dllFuncs.Color4f( 1.0f, 0.0f, 0.0f, 1.0f );
			else ref.dllFuncs.Color4f( 0.0f, 1.0f, 0.0f, 1.0f );
			R_DrawLeafNode( x, y, scale );
		}
		world.recursion_level--;
		return;
	}

	if( shownodes == 1 )
	{
		ref.dllFuncs.Color4f( 0.0f, 0.0f, 1.0f, 1.0f );
		R_DrawLeafNode( x, y, scale );
	}
	else if( shownodes == 2 )
	{
		R_DrawNodeConnection( x, y, x - scale, y + scale );
		R_DrawNodeConnection( x, y, x + scale, y + scale );
	}

	R_ShowTree_r( node_child( node, 1, cl.worldmodel ), x - scale, y + scale, downScale, shownodes, viewleaf );
	R_ShowTree_r( node_child( node, 0, cl.worldmodel ), x + scale, y + scale, downScale, shownodes, viewleaf );

	world.recursion_level--;
}

static void R_ShowTree( void )
{
	float	x = (float)((refState.width - (int)POINT_SIZE) >> 1);
	float	y = NODE_INTERVAL_Y(1.0f);
	mleaf_t *viewleaf;

	if( !cl.worldmodel || !r_showtree.value )
		return;

	world.recursion_level = 0;
	viewleaf = Mod_PointInLeaf( refState.vieworg, cl.worldmodel->nodes, cl.worldmodel );

	ref.dllFuncs.TriRenderMode( kRenderTransTexture );

	//pglLineWidth( 2.0f );
	ref.dllFuncs.Color4f( 1, 0.7f, 0, 1.0f );
	//pglDisable( GL_TEXTURE_2D );
	R_ShowTree_r( cl.worldmodel->nodes, x, y, world.max_recursion * 3.5f, 2, viewleaf );
	//pglEnable( GL_TEXTURE_2D );
	//pglLineWidth( 1.0f );

	R_ShowTree_r( cl.worldmodel->nodes, x, y, world.max_recursion * 3.5f, 1, viewleaf );

	Con_NPrintf( 0, "max recursion %d\n", world.max_recursion );
}

static int ESP_TeamFromModel( const char *model )
{
	if( !model || !model[0] )
		return 0;
	if( Q_stristr( model, "terror" ) || Q_stristr( model, "leet" )
		|| Q_stristr( model, "arctic" ) || Q_stristr( model, "guerilla" )
		|| Q_stristr( model, "militia" ))
		return 1;
	if( Q_stristr( model, "urban" ) || Q_stristr( model, "gsg9" )
		|| Q_stristr( model, "sas" ) || Q_stristr( model, "gign" )
		|| Q_stristr( model, "vip" ))
		return 2;
	return 0;
}

static void ESP_CopyName( char *dst, size_t n, const char *src )
{
	size_t i;

	if( !src )
		src = "";
	for( i = 0; i + 1 < n && src[i]; i++ )
		dst[i] = ( src[i] >= 32 && src[i] < 127 ) ? src[i] : ' ';
	dst[i] = 0;
}

/* Publish avatar positions for the external overlay.
   Works in any session: local listen server, remote multiplayer,
   demo playback. Entities that are not resolvable are simply skipped. */
static void ESP_PublishLocal( void )
{
	static esp_frame_t *frame;
	static int fd = -1;
	esp_frame_t local;
	struct timespec ts;
	uint32_t odd;
	int i, n, count;

	if( fd < 0 )
	{
		fd = open( ESP_PATH, O_RDWR | O_CREAT, 0600 );
		if( fd < 0 )
			return;
		if( ftruncate( fd, sizeof( *frame )) < 0 )
			return;
		frame = mmap( NULL, sizeof( *frame ), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
		if( frame == MAP_FAILED )
		{
			frame = NULL;
			return;
		}
		memset( frame, 0, sizeof( *frame ));
		frame->magic = ESP_MAGIC;
	}
	if( !frame )
		return;

	memset( &local, 0, sizeof( local ));
	local.magic = ESP_MAGIC;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	local.time_ms = (uint32_t)( ts.tv_sec * 1000u + ts.tv_nsec / 1000000u );

	if( host.hWnd )
	{
		SDL_GetWindowPosition( (SDL_Window *)host.hWnd, &local.win_x, &local.win_y );
		SDL_GetWindowSize( (SDL_Window *)host.hWnd, &local.win_w, &local.win_h );
	}
	if( local.win_w < 2 || local.win_h < 2 )
	{
		local.win_w = refState.width;
		local.win_h = refState.height;
	}

	if( clgame.entities )
	{
		float fov_x, fov_y;
		int vp_w, vp_h;

		VectorCopy( refState.vieworg, local.vieworg );
		VectorCopy( refState.viewangles, local.viewangles );

		vp_w = clgame.viewport[2] > 0 ? clgame.viewport[2] : refState.width;
		vp_h = clgame.viewport[3] > 0 ? clgame.viewport[3] : refState.height;
		fov_x = bound( 10.0f, cl.local.scr_fov, 150.0f );
		fov_y = V_CalcFov( &fov_x, vp_w, vp_h );
		if( refState.wideScreen && r_adjust_fov.value )
			V_AdjustFov( &fov_x, &fov_y, vp_w, vp_h, false );
		local.fov_x = fov_x;
		local.fov_y = fov_y;

		n = cl.maxclients;
		if( n < 1 )
			n = clgame.maxEntities - 1;
		if( n > ESP_MAX )
			n = ESP_MAX;
		if( n > clgame.maxEntities - 1 )
			n = clgame.maxEntities - 1;

		count = 0;
		for( i = 1; i <= n && count < ESP_MAX; i++ )
		{
			cl_entity_t *ent = &clgame.entities[i];
			esp_ent_t *out;
			const char *mdl = "";
			edict_t *ed;
			int team;

			if( i == cl.playernum + 1 )
				continue;
			if( !ent->model || ent->model->type != mod_studio )
				continue;
			if( FBitSet( ent->curstate.effects, EF_NODRAW ))
				continue;
			if( !ent->player && !Q_stristr( ent->model->name, "/player/" ))
				continue;

			out = &local.ent[count];
			VectorCopy( ent->origin, out->origin );
			if( ent->model->radius > 1.0f )
			{
				VectorCopy( ent->model->mins, out->mins );
				VectorCopy( ent->model->maxs, out->maxs );
			}
			else
			{
				VectorSet( out->mins, -16, -16, -36 );
				VectorSet( out->maxs, 16, 16, 36 );
			}

			ed = SV_EdictNum( i );
			if( ed && !ed->free )
			{
				out->health = (int)ed->v.health;
				out->dead = ed->v.deadflag ? 1 : 0;
				team = ed->v.team;
			}
			else
			{
				out->health = ent->curstate.health;
				out->dead = out->health <= 0;
				team = ent->curstate.team;
			}

			if( i - 1 < MAX_CLIENTS )
			{
				ESP_CopyName( out->name, sizeof( out->name ), cl.players[i - 1].name );
				mdl = cl.players[i - 1].model;
			}
			if( !out->name[0] )
				ESP_CopyName( out->name, sizeof( out->name ), "avatar" );
			ESP_CopyName( out->model, sizeof( out->model ), mdl[0] ? mdl : ent->model->name );

			if( team != 1 && team != 2 )
				team = ESP_TeamFromModel( out->model );
			if( team != 1 && team != 2 )
				team = ESP_TeamFromModel( ent->model->name );
			out->team = team;
			count++;
		}
		local.count = count;
	}

	if( refState.camera_ready )
	{
		memcpy( local.camera_mvp, refState.camera_mvp, sizeof( local.camera_mvp ));
		local.camera_vp[0] = refState.camera_vp[0];
		local.camera_vp[1] = refState.camera_vp[1];
		local.camera_vp[2] = refState.camera_vp[2];
		local.camera_vp[3] = refState.camera_vp[3];
		local.camera_ready = 1;
	}

	odd = ( frame->seq + 1u ) | 1u;
	frame->seq = odd;
	__sync_synchronize();
	local.seq = odd;
	local.magic = ESP_MAGIC;
	memcpy( (char *)frame + sizeof( uint32_t ) * 2, (char *)&local + sizeof( uint32_t ) * 2,
		sizeof( local ) - sizeof( uint32_t ) * 2 );
	__sync_synchronize();
	frame->seq = odd + 1u;
}

#define ESP_SOUND_MARKS 48
#define ESP_SOUND_LINGER 1.25

typedef struct
{
	vec3_t origin;
	double time;
	int entnum;
	int chan;
	int vol;
	char name[48];
} esp_sound_mark_t;

static esp_sound_mark_t g_espSounds[ESP_SOUND_MARKS];

static void ESP_SoundClear( void )
{
	memset( g_espSounds, 0, sizeof( g_espSounds ));
}

/* Origin the mixer is using. Does not write the channel.
   Works in local and remote sessions; entities that are not
   synchronized fall back to the channel origin. */
static qboolean ESP_SoundOrigin( const channel_t *ch, vec3_t out )
{
	cl_entity_t *ent;

	if( ch->entnum > 0 && ( ch->entnum - 1 ) == cl.playernum )
		return false;

	VectorCopy( ch->origin, out );
	if( ch->entnum <= 0 || ch->staticsound )
		return !VectorIsNull( out );

	ent = CL_GetEntityByIndex( ch->entnum );
	if( ent && ent->model && ent->curstate.messagenum == cl.parsecount )
	{
		if( ent->model->type == mod_brush )
		{
			VectorAverage( ent->model->mins, ent->model->maxs, out );
			VectorAdd( ent->origin, out, out );
		}
		else
			VectorCopy( ent->origin, out );
		return true;
	}

	return !VectorIsNull( out );
}

static void ESP_SoundNote( const channel_t *ch, const vec3_t origin, int vol )
{
	esp_sound_mark_t *slot, *oldest;
	const char *name, *base;
	int i;

	name = ( ch->name[0] && ch->is_sentence ) ? ch->name : ( ch->sfx ? ch->sfx->name : "" );
	base = Q_strrchr( name, '/' );
	base = base ? base + 1 : name;

	oldest = &g_espSounds[0];
	for( i = 0; i < ESP_SOUND_MARKS; i++ )
	{
		slot = &g_espSounds[i];
		if( slot->time > 0.0 && slot->entnum == ch->entnum && slot->chan == ch->entchannel
			&& !Q_strncmp( slot->name, base, sizeof( slot->name )))
		{
			VectorCopy( origin, slot->origin );
			slot->time = host.realtime;
			slot->vol = vol;
			return;
		}
		if( slot->time < oldest->time )
			oldest = slot;
	}

	memset( oldest, 0, sizeof( *oldest ));
	VectorCopy( origin, oldest->origin );
	oldest->time = host.realtime;
	oldest->entnum = ch->entnum;
	oldest->chan = ch->entchannel;
	oldest->vol = vol;
	Q_strncpy( oldest->name, base, sizeof( oldest->name ));
}

/* Debug markers for sounds the mixer is playing.
   Sem restrição de sessão: funciona em listen server, servidor remoto
   e demo playback. Ajuste esp_sound 1 (entidades) ou 2 (também mundo). */
static void ESP_DrawSounds( void )
{
	static convar_t *cv;
	rgba_t hud = { 255, 220, 80, 255 };
	rgba_t dim = { 160, 160, 160, 255 };
	int i, shown;

	if( !cv )
		cv = Cvar_FindVar( "esp_sound" );
	if( !cv || cv->value <= 0.0f )
	{
		ESP_SoundClear();
		return;
	}

	if( cls.state != ca_active || !dma.initialized )
	{
		ESP_SoundClear();
		return;
	}

	for( i = NUM_AMBIENTS; i < total_channels; i++ )
	{
		channel_t *ch = &channels[i];
		vec3_t origin;
		int vol;

		if( !ch->sfx || ch->localsound )
			continue;
		/* 1 = entity sounds (steps, shots, voice). 2 = also world/static. */
		if( cv->value < 2.0f && ( ch->staticsound || ch->entnum <= 0 ))
			continue;
		if( !ESP_SoundOrigin( ch, origin ))
			continue;

		vol = Q_max( ch->leftvol, ch->rightvol );
		ESP_SoundNote( ch, origin, vol );
	}

	shown = 0;
	for( i = 0; i < ESP_SOUND_MARKS; i++ )
	{
		esp_sound_mark_t *mark = &g_espSounds[i];
		vec3_t screen;
		char msg[96];

		if( mark->time <= 0.0 || ( host.realtime - mark->time ) > ESP_SOUND_LINGER )
			continue;
		if( ref.dllFuncs.WorldToScreen( mark->origin, screen ))
			continue;

		screen[0] =  0.5f * screen[0] * refState.width;
		screen[1] = -0.5f * screen[1] * refState.height;
		screen[0] += 0.5f * refState.width;
		screen[1] += 0.5f * refState.height;

		Q_snprintf( msg, sizeof( msg ), "%s\nent %d vol %d", mark->name, mark->entnum, mark->vol );
		if( mark->vol > 0 )
			Con_DrawString( (int)screen[0], (int)screen[1], msg, hud );
		else
			Con_DrawString( (int)screen[0], (int)screen[1], msg, dim );
		shown++;
	}

	{
		char msg[32];

		Q_snprintf( msg, sizeof( msg ), "esp_sound %d", shown );
		Con_DrawString( 8, 48, msg, hud );
	}
}

/*
   NOTA: As funções OfflineBots_Session() e OfflineBots_Update() foram
   removidas. Elas existiam para alimentar o cvar "offline_bots" que era
   lido pelo client.dll (aimbot.cpp) para bloquear o aimbot fora de
   partidas offline com bots. Com a trava removida, nada mais precisa
   dessas funções.
*/

/*
==================
V_PostRender

==================
*/
void V_PostRender( void )
{
	qboolean		draw_2d = false;

	ref.dllFuncs.R_AllowFog( false );
	ref.dllFuncs.R_Set2DMode( true );

	if( cls.state == ca_active && cls.signon == SIGNONS && cls.scrshot_action != scrshot_mapshot )
	{
		SCR_TileClear();
		CL_DrawHUD( CL_ACTIVE );
		VGui_Paint();
	}

	switch( cls.scrshot_action )
	{
	case scrshot_inactive:
	case scrshot_normal:
	case scrshot_snapshot:
		draw_2d = true;
		break;
	}

	if( draw_2d )
	{
		SCR_RSpeeds();
		SCR_NetSpeeds();
		SCR_DrawPos();
		SCR_DrawEnts();
		ESP_DrawSounds();
		SCR_DrawNetGraph();
		SCR_DrawUserCmd();
		SV_DrawOrthoTriangles();
		CL_DrawDemoRecording();
		CL_DrawHUD( CL_CHANGELEVEL );
		ref.dllFuncs.R_ShowTextures();
		R_ShowTree();
		Con_DrawConsole();
		UI_UpdateMenu( host.realtime );
		Con_DrawVersion();
		Con_DrawDebug(); // must be last
		Touch_Draw();
		OSK_Draw();

		S_ExtraUpdate();
	}

	SCR_MakeScreenShot();
	Evdev_DrawCursor();
	ESP_PublishLocal();
	ref.dllFuncs.R_AllowFog( true );
	Platform_SetTimer( 0.0f );
	ref.dllFuncs.R_EndFrame();

	V_CheckGammaEnd();
}