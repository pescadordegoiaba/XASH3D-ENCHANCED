/*
input.c - win32 input devices
Copyright (C) 2007 Uncle Mike

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
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "cursor_type.h"
#include "platform/platform.h"

// evdev — Linux puro
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

// SDL — usado apenas para relative mouse mode como fallback
#include <SDL2/SDL.h>

#define XASH_USE_EVDEV 1

static qboolean in_mouseactive;
static qboolean in_mouseinitialized;
static qboolean in_mouse_suspended;
static struct { int x, y; } in_lastvalidpos;
static qboolean in_mouse_savedpos;
static int in_mstate = 0;
static struct inputstate_s { float lastpitch, lastyaw; } inputstate;

#define EVDEV_MAX_DEVICES 8

// evdev state. Several event nodes can be real mice; their counts are summed.
static int evdev_fds[EVDEV_MAX_DEVICES];
static int evdev_ndev;
static qboolean evdev_grabbed;
static int evdev_acc_x, evdev_acc_y;
static int evdev_buttons;
static float evdev_cur_x, evdev_cur_y;
static qboolean evdev_cur_valid;
static double evdev_click_time[5];
static double evdev_rescan_at;
static qboolean evdev_reported_none;
static qboolean evdev_inited;
static convar_t *evdev_out_x;
static convar_t *evdev_out_y;

CVAR_DEFINE_AUTO( m_pitch, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse pitch value" );
CVAR_DEFINE_AUTO( m_yaw, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse yaw value" );
CVAR_DEFINE_AUTO( m_ignore, DEFAULT_M_IGNORE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "ignore mouse events" );
static CVAR_DEFINE_AUTO( look_filter, "0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "filter look events" );
static CVAR_DEFINE_AUTO( m_rawinput, "1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "raw input" );
static CVAR_DEFINE_AUTO( cl_forwardspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "forward speed" );
static CVAR_DEFINE_AUTO( cl_backspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "back speed" );
static CVAR_DEFINE_AUTO( cl_sidespeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "side speed" );
static CVAR_DEFINE_AUTO( m_grab_debug, "1", FCVAR_PRIVILEGED, "grab debug" );
static CVAR_DEFINE_AUTO( evdev_cursor_speed, "1", FCVAR_ARCHIVE, "cursor pixels per raw /dev/input count" );
static CVAR_DEFINE_AUTO( cl_imgui_mouse, "0", FCVAR_CLIENTDLL, "client imgui wants an absolute cursor" );
CVAR_DEFINE_AUTO( touch_enable, DEFAULT_TOUCH_ENABLE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "touch" );

//=============================================================================
// evdev
//=============================================================================

static qboolean EVDev_WantCursor( void );

static int EVDev_TestBit( const unsigned char *bits, int bit )
{
	return ( bits[bit / 8] >> ( bit % 8 )) & 1;
}

static int EVDev_ButtonIndex( int code )
{
	switch( code )
	{
	case BTN_LEFT: return 0;
	case BTN_RIGHT: return 1;
	case BTN_MIDDLE: return 2;
	case BTN_SIDE: return 3;
	case BTN_EXTRA: return 4;
	default: return -1;
	}
}

static void EVDev_ReleaseButtons( void )
{
	int i;

	for( i = 0; i < 5; i++ )
	{
		if( !FBitSet( evdev_buttons, BIT( i )))
			continue;
		ClearBits( evdev_buttons, BIT( i ));
		if( in_mouseinitialized )
			IN_MouseEvent( i, 0 );
	}
}

static void EVDev_CloseAll( void )
{
	int i;

	for( i = 0; i < evdev_ndev; i++ )
	{
		if( evdev_fds[i] >= 0 )
		{
			ioctl( evdev_fds[i], EVIOCGRAB, (void *)0 );
			close( evdev_fds[i] );
			evdev_fds[i] = -1;
		}
	}
	evdev_ndev = 0;
	evdev_grabbed = false;
}

static void EVDev_DropDevice( int index )
{
	if( index < 0 || index >= evdev_ndev )
		return;

	if( evdev_fds[index] >= 0 )
		close( evdev_fds[index] );

	evdev_ndev--;
	for( ; index < evdev_ndev; index++ )
		evdev_fds[index] = evdev_fds[index + 1];
	evdev_fds[evdev_ndev] = -1;

	if( !evdev_ndev )
	{
		evdev_grabbed = false;
		EVDev_ReleaseButtons();
	}
}

static void EVDev_Scan( void )
{
	DIR *dir;
	struct dirent *ent;
	int tried = 0, opened = 0;

	EVDev_CloseAll();

	dir = opendir( "/dev/input" );
	if( !dir )
	{
		Con_Printf( "[evdev] open /dev/input: %s\n", strerror( errno ));
		evdev_reported_none = true;
		return;
	}

	while(( ent = readdir( dir )) != NULL && evdev_ndev < EVDEV_MAX_DEVICES )
	{
		char path[300];
		char name[128];
		unsigned char relbits[( REL_MAX + 7 ) / 8 + 1];
		unsigned char keybits[( KEY_MAX + 7 ) / 8 + 1];
		int fd;

		if( strncmp( ent->d_name, "event", 5 ) != 0 )
			continue;

		Q_snprintf( path, sizeof( path ), "/dev/input/%s", ent->d_name );
		tried++;
		fd = open( path, O_RDONLY | O_NONBLOCK );
		if( fd < 0 )
			continue;

		memset( relbits, 0, sizeof( relbits ));
		memset( keybits, 0, sizeof( keybits ));

		if( ioctl( fd, EVIOCGBIT( EV_REL, sizeof( relbits )), relbits ) < 0
			|| ioctl( fd, EVIOCGBIT( EV_KEY, sizeof( keybits )), keybits ) < 0
			|| !EVDev_TestBit( relbits, REL_X )
			|| !EVDev_TestBit( relbits, REL_Y )
			|| !EVDev_TestBit( keybits, BTN_LEFT ))
		{
			close( fd );
			continue;
		}

		name[0] = 0;
		if( ioctl( fd, EVIOCGNAME( sizeof( name ) - 1 ), name ) < 0 )
			Q_strncpy( name, "mouse", sizeof( name ));
		name[sizeof( name ) - 1] = 0;

		evdev_fds[evdev_ndev++] = fd;
		opened++;
		Con_Printf( "[evdev] mouse: %s (%s)\n", name, path );
	}

	closedir( dir );

	if( !opened && !evdev_reported_none )
	{
		Con_Printf( "[evdev] nenhum mouse em /dev/input (%d devices, grupo 'input'?)\n", tried );
		evdev_reported_none = true;
	}
	else if( opened )
		evdev_reported_none = false;
}

static void EVDev_OnButton( int index, int down )
{
	int state;

	if( index < 0 || index > 4 || !in_mouseinitialized )
		return;

	if( down )
	{
		if( FBitSet( evdev_buttons, BIT( index )))
			return;
		SetBits( evdev_buttons, BIT( index ));

		state = 1;
		if( host.realtime - evdev_click_time[index] < 0.4 && evdev_click_time[index] > 0 )
			state = 2;
		evdev_click_time[index] = host.realtime;
	}
	else
	{
		if( !FBitSet( evdev_buttons, BIT( index )))
			return;
		ClearBits( evdev_buttons, BIT( index ));
		state = 0;
	}

	IN_MouseEvent( index, state );
}

static void EVDev_Read( qboolean apply )
{
	int i;

	for( i = 0; i < evdev_ndev; )
	{
		struct input_event ev;
		ssize_t n;
		qboolean dead = false;

		while(( n = read( evdev_fds[i], &ev, sizeof( ev ))) == (ssize_t)sizeof( ev ))
		{
			int btn;

			if( !apply )
				continue;

			if( ev.type == EV_REL )
			{
				if( ev.code == REL_X )
					evdev_acc_x += ev.value;
				else if( ev.code == REL_Y )
					evdev_acc_y += ev.value;
				else if( ev.code == REL_WHEEL && ev.value )
				{
					int step = ev.value > 0 ? 1 : -1;
					int left = ev.value > 0 ? ev.value : -ev.value;

					if( left > 4 )
						left = 4;
					while( left-- )
						IN_MWheelEvent( step );
				}
			}
			else if( ev.type == EV_KEY && ev.value != 2 )
			{
				btn = EVDev_ButtonIndex( ev.code );
				if( btn >= 0 )
					EVDev_OnButton( btn, ev.value ? 1 : 0 );
			}
		}

		if( n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR )
			dead = true;
		if( n == 0 )
			dead = true;

		if( dead )
		{
			Con_Printf( "[evdev] device fechou: %s\n", strerror( errno ));
			EVDev_DropDevice( i );
			continue;
		}
		i++;
	}
}

static void EVDev_SetGrab( qboolean grab )
{
	int i;

	if( grab == evdev_grabbed )
		return;

	if( grab && evdev_ndev <= 0 )
		return;

	for( i = 0; i < evdev_ndev; i++ )
	{
		if( ioctl( evdev_fds[i], EVIOCGRAB, (void *)(intptr_t)( grab ? 1 : 0 )) < 0 )
		{
			int j;

			if( m_grab_debug.value )
				Con_Printf( "[evdev] EVIOCGRAB %s falhou: %s\n", grab ? "on" : "off", strerror( errno ));
			if( grab )
			{
				for( j = 0; j < i; j++ )
					ioctl( evdev_fds[j], EVIOCGRAB, (void *)0 );
				return;
			}
		}
	}

	evdev_grabbed = grab;
	SDL_SetRelativeMouseMode( SDL_FALSE );
	SDL_ShowCursor( ( !grab && host.mouse_visible ) ? SDL_TRUE : SDL_FALSE );

	// Events queued before the grab are desktop motion. Drop them.
	EVDev_Read( false );
	evdev_acc_x = evdev_acc_y = 0;
	if( !grab )
		EVDev_ReleaseButtons();

	if( m_grab_debug.value )
		Con_Printf( "[evdev] grab %s (%d)\n", grab ? "on" : "off", evdev_ndev );
}

static void EVDev_UpdateGrab( void )
{
	qboolean want = ( host.status == HOST_FRAME ) && evdev_ndev > 0 && !m_ignore.value && in_mouseinitialized;

	EVDev_SetGrab( want );
}

static void EVDev_MoveCursor( float dx, float dy )
{
	float speed = evdev_cursor_speed.value;

	if( speed == 0.0f )
		speed = 1.0f;

	if( !evdev_cur_valid || refState.width <= 1 || refState.height <= 1 )
	{
		evdev_cur_x = refState.width > 1 ? refState.width * 0.5f : 0;
		evdev_cur_y = refState.height > 1 ? refState.height * 0.5f : 0;
		evdev_cur_valid = refState.width > 1;
	}

	evdev_cur_x += dx * speed;
	evdev_cur_y += dy * speed;

	if( refState.width > 1 )
	{
		if( evdev_cur_x < 0 ) evdev_cur_x = 0;
		if( evdev_cur_y < 0 ) evdev_cur_y = 0;
		if( evdev_cur_x > refState.width - 1 ) evdev_cur_x = refState.width - 1;
		if( evdev_cur_y > refState.height - 1 ) evdev_cur_y = refState.height - 1;
	}
}

static qboolean EVDev_WantCursor( void )
{
	if( cl_imgui_mouse.value != 0.0f && cls.key_dest == key_game )
		return true;
	if( host.mouse_visible )
		return true;
	if( cls.key_dest == key_menu || cls.key_dest == key_console )
		return true;
	if( cls.state != ca_active )
		return true;
	return false;
}

static void EVDev_Publish( void )
{
	float dx = 0, dy = 0;

	if( Evdev_OwnsPointer( ))
	{
		dx = (float)evdev_acc_x;
		dy = (float)evdev_acc_y;
		evdev_acc_x = evdev_acc_y = 0;

		if( m_ignore.value )
			dx = dy = 0;
		else if( EVDev_WantCursor( ))
		{
			EVDev_MoveCursor( dx, dy );
			dx = dy = 0;
		}
	}
	else if( !m_ignore.value && !host.mouse_visible && in_mouseactive && cls.key_dest == key_game )
	{
		Platform_MouseMove( &dx, &dy );
	}
	else
	{
		float sink_x, sink_y;
		Platform_MouseMove( &sink_x, &sink_y );
	}

	if( evdev_out_x )
		evdev_out_x->value = dx;
	if( evdev_out_y )
		evdev_out_y->value = dy;
}

qboolean Evdev_OwnsPointer( void )
{
	return evdev_grabbed && evdev_ndev > 0;
}

qboolean Evdev_CursorPos( int *x, int *y )
{
	if( !Evdev_OwnsPointer( ))
		return false;

	if( !evdev_cur_valid )
		EVDev_MoveCursor( 0, 0 );

	if( x ) *x = (int)evdev_cur_x;
	if( y ) *y = (int)evdev_cur_y;
	return true;
}

void Evdev_SetCursorPos( int x, int y )
{
	evdev_cur_x = (float)x;
	evdev_cur_y = (float)y;
	evdev_cur_valid = true;
	EVDev_MoveCursor( 0, 0 );
}

void Evdev_DrawCursor( void )
{
	static const unsigned short bits[16] = {
		0x0001, 0x0003, 0x0005, 0x0009,
		0x0011, 0x0021, 0x0041, 0x0081,
		0x0101, 0x0201, 0x007F, 0x0009,
		0x0011, 0x0011, 0x0022, 0x0044
	};
	int row, col, scale = 2;
	int ox, oy;

	if( !Evdev_OwnsPointer( ) || m_ignore.value || !EVDev_WantCursor( ) || !ref.dllFuncs.FillRGBA )
		return;
	if( refState.width <= 1 || refState.height <= 1 )
		return;

	ox = (int)evdev_cur_x;
	oy = (int)evdev_cur_y;

	for( row = 0; row < 16; row++ )
	{
		for( col = 0; col < 16; col++ )
		{
			if( !( bits[row] & ( 1 << col )))
				continue;
			ref.dllFuncs.FillRGBA( kRenderTransTexture,
				ox + col * scale + scale, oy + row * scale + scale,
				scale, scale, 0, 0, 0, 220 );
			ref.dllFuncs.FillRGBA( kRenderTransTexture,
				ox + col * scale, oy + row * scale,
				scale, scale, 255, 255, 255, 255 );
		}
	}
}

static void EVDev_Rescan_f( void )
{
	qboolean was = evdev_grabbed;

	if( was )
		EVDev_SetGrab( false );
	evdev_reported_none = false;
	EVDev_Scan();
	evdev_rescan_at = host.realtime + 2.0;
	if( was )
		EVDev_UpdateGrab();
}

void Evdev_Init( void )
{
	int i;

	for( i = 0; i < EVDEV_MAX_DEVICES; i++ )
		evdev_fds[i] = -1;

	evdev_out_x = Cvar_Get( "evdev_dx", "0", FCVAR_READ_ONLY, "raw mouse dx this frame, from /dev/input" );
	evdev_out_y = Cvar_Get( "evdev_dy", "0", FCVAR_READ_ONLY, "raw mouse dy this frame, from /dev/input" );
	Cmd_AddRestrictedCommand( "evdev_rescan", EVDev_Rescan_f, "reopen /dev/input mice" );

	EVDev_Scan();
	EVDev_Read( false );
	evdev_acc_x = evdev_acc_y = 0;
	evdev_rescan_at = host.realtime + 2.0;
	evdev_inited = true;
}

void Evdev_Shutdown( void )
{
	if( !evdev_inited )
		return;

	EVDev_ReleaseButtons();
	EVDev_SetGrab( false );
	EVDev_CloseAll();
	Cmd_RemoveCommand( "evdev_rescan" );
	evdev_inited = false;
}

void Evdev_SetGrab( qboolean grab )
{
	EVDev_SetGrab( grab );
}

void IN_EvdevMove( float *yaw, float *pitch )
{
	if( yaw ) *yaw = (float)evdev_acc_x;
	if( pitch ) *pitch = (float)evdev_acc_y;
	evdev_acc_x = evdev_acc_y = 0;
}

void IN_EvdevFrame( void )
{
	if( !evdev_inited )
		return;

	if( evdev_ndev <= 0 && host.realtime >= evdev_rescan_at )
	{
		EVDev_Scan();
		evdev_rescan_at = host.realtime + 2.0;
	}

	EVDev_UpdateGrab();
	EVDev_Read( evdev_grabbed );
}

//=============================================================================
// fim evdev
//=============================================================================

static void IN_StartupMouse( void )
{
	Cvar_RegisterVariable( &m_ignore );
	Cvar_RegisterVariable( &m_pitch );
	Cvar_RegisterVariable( &m_yaw );
	Cvar_RegisterVariable( &look_filter );
	Cvar_RegisterVariable( &m_rawinput );
	Cvar_RegisterVariable( &m_grab_debug );
	Cvar_RegisterVariable( &evdev_cursor_speed );
	Cvar_RegisterVariable( &cl_imgui_mouse );
	Cvar_RegisterVariable( &touch_enable );

	if( Sys_CheckParm( "-noenginemouse" ))
		return;

#if XASH_USE_EVDEV
	Evdev_Init();
#endif

	in_mouseinitialized = true;
}

void IN_MouseSavePos( void )
{
	if( !in_mouseactive ) return;
	Platform_GetMousePos( &in_lastvalidpos.x, &in_lastvalidpos.y );
	in_mouse_savedpos = true;
}

void IN_MouseRestorePos( void )
{
	if( !in_mouse_savedpos ) return;
	Platform_SetMousePos( in_lastvalidpos.x, in_lastvalidpos.y );
	in_mouse_savedpos = false;
}

static void IN_ApplyImGuiCursor( void )
{
	static int applied = 0;
	int want;

	if( cls.key_dest != key_game )
	{
		applied = 0;
		return;
	}

	want = cl_imgui_mouse.value != 0.0f;
	if( want != applied )
	{
		applied = want;
		Platform_SetCursorType( want ? dc_arrow : dc_none );
	}
}

void IN_ToggleClientMouse( int newstate, int oldstate )
{
	if( newstate == oldstate ) return;

	if( newstate == key_menu || newstate == key_console )
		Platform_SetCursorType( dc_arrow );
	else
		Platform_SetCursorType( dc_none );

	if( m_ignore.value ) return;

	if( oldstate == key_game ) IN_DeactivateMouse();
	else if( newstate == key_game ) IN_ActivateMouse();
}

/*
============
IN_SetRelativeMouseMode

SDL relative mode is only the fallback. While /dev/input is grabbed it
is forced off so the compositor does not add a second, filtered delta.
============
*/
void IN_SetRelativeMouseMode( qboolean set )
{
	static qboolean s_bRawInput;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( Evdev_OwnsPointer( ))
		set = false;

	if( set == s_bRawInput )
		return;

	s_bRawInput = set;
	SDL_SetRelativeMouseMode( set ? SDL_TRUE : SDL_FALSE );

	if( verbose ) Con_Printf( "%s: %s\n", __func__, set ? "true" : "false" );
}

void IN_SetMouseGrab( qboolean set )
{
	static qboolean s_bMouseGrab;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( set && !s_bMouseGrab )
	{
		Platform_SetMouseGrab( true );
		s_bMouseGrab = true;
		if( verbose ) Con_Printf( "%s: true\n", __func__ );
	}
	else if( !set && s_bMouseGrab )
	{
		Platform_SetMouseGrab( false );
		s_bMouseGrab = false;
		if( verbose ) Con_Printf( "%s: false\n", __func__ );
	}
}

static void IN_CheckMouseState( qboolean active )
{
	qboolean use_raw_input = true;

	if( m_ignore.value )
		active = false;

	if( active && use_raw_input && !host.mouse_visible && cls.state == ca_active )
		IN_SetRelativeMouseMode( true );
	else
		IN_SetRelativeMouseMode( false );

	if( active && !host.mouse_visible && cls.state == ca_active )
		IN_SetMouseGrab( true );
	else
		IN_SetMouseGrab( false );
}

void IN_ActivateMouse( void )
{
	if( !in_mouseinitialized ) return;
	IN_CheckMouseState( true );
	if( clgame.dllFuncs.IN_ActivateMouse )
		clgame.dllFuncs.IN_ActivateMouse();
	in_mouseactive = true;
}

void IN_DeactivateMouse( void )
{
	if( !in_mouseinitialized ) return;
	IN_CheckMouseState( false );
	if( clgame.dllFuncs.IN_DeactivateMouse )
		clgame.dllFuncs.IN_DeactivateMouse();
	in_mouseactive = false;
}

static void IN_MouseMove( void )
{
	int x, y;
	if( !in_mouseinitialized ) return;

	if( Touch_WantVisibleCursor( ))
	{
		Touch_KeyEvent( 0, 0 );
		return;
	}

	Platform_GetMousePos( &x, &y );
	VGui_MouseMove( x, y );
	UI_MouseMove( x, y );
}

void IN_MouseEvent( int key, int down )
{
	if( !in_mouseinitialized ) return;

	if( down ) SetBits( in_mstate, BIT( key ));
	else ClearBits( in_mstate, BIT( key ));

	if( Touch_WantVisibleCursor( ))
		Touch_KeyEvent( K_MOUSE1 + key, down );
	else if( cls.key_dest == key_game )
	{
		if( cl_imgui_mouse.value != 0.0f )
			Key_Event( K_MOUSE1 + key, down );
		else
		{
			VGui_MouseEvent( K_MOUSE1 + key, down );
			// Cursor mode (buy menu, spectator mouse) must not also fire +attack.
			if( in_mouseactive && !host.mouse_visible && clgame.dllFuncs.IN_MouseEvent )
				clgame.dllFuncs.IN_MouseEvent( in_mstate );
		}
	}
	else
		Key_Event( K_MOUSE1 + key, down );
}

void IN_MWheelEvent( int y )
{
	int b = y > 0 ? K_MWHEELUP : K_MWHEELDOWN;
	VGui_MWheelEvent( y );
	Key_Event( b, true );
	Key_Event( b, false );
}

void IN_Shutdown( void )
{
	IN_DeactivateMouse();
#if XASH_USE_EVDEV
	Evdev_Shutdown();
#endif
	Touch_Shutdown();
}

uint IN_CollectInputDevices( void )
{
	return 0;
}

void IN_LockInputDevices( qboolean lock )
{
	(void)lock;
}

void IN_Init( void )
{
	Cvar_RegisterVariable( &cl_forwardspeed );
	Cvar_RegisterVariable( &cl_backspeed );
	Cvar_RegisterVariable( &cl_sidespeed );

	if( !Host_IsDedicated() )
	{
		IN_StartupMouse();
		Joy_Init();
		Touch_Init();
	}
}

#define F (1U << 0)
#define B (1U << 1)
#define L (1U << 2)
#define R (1U << 3)
#define T (1U << 4)
#define S (1U << 5)

static void IN_JoyAppendMove( usercmd_t *cmd, float forwardmove, float sidemove )
{
	static uint moveflags = T | S;

	if( forwardmove ) cmd->forwardmove = forwardmove * cl_forwardspeed.value;
	if( sidemove )    cmd->sidemove    = sidemove    * cl_sidespeed.value;

	if( forwardmove ) moveflags &= ~T;
	else if( !( moveflags & T )) { Cmd_ExecuteString( "-back" ); Cmd_ExecuteString( "-forward" ); moveflags |= T; }

	if( sidemove ) moveflags &= ~S;
	else if( !( moveflags & S )) { Cmd_ExecuteString( "-moveleft" ); Cmd_ExecuteString( "-moveright" ); moveflags |= S; }

	if( forwardmove >  0.7f && !( moveflags & F )) { moveflags |= F; Cmd_ExecuteString( "+forward" ); }
	else if( forwardmove < 0.7f && ( moveflags & F )) { moveflags &= ~F; Cmd_ExecuteString( "-forward" ); }

	if( forwardmove < -0.7f && !( moveflags & B )) { moveflags |= B; Cmd_ExecuteString( "+back" ); }
	else if( forwardmove > -0.7f && ( moveflags & B )) { moveflags &= ~B; Cmd_ExecuteString( "-back" ); }

	if( sidemove >  0.9f && !( moveflags & R )) { moveflags |= R; Cmd_ExecuteString( "+moveright" ); }
	else if( sidemove < 0.9f && ( moveflags & R )) { moveflags &= ~R; Cmd_ExecuteString( "-moveright" ); }

	if( sidemove < -0.9f && !( moveflags & L )) { moveflags |= L; Cmd_ExecuteString( "+moveleft" ); }
	else if( sidemove > -0.9f && ( moveflags & L )) { moveflags &= ~L; Cmd_ExecuteString( "-moveleft" ); }
}

/* ================
IN_CollectInput

Mouse:
  - evdev com grab ativo (sub-pixel, sem borda) — preferido
  - fallback SDL relative pointer (Wayland/X11)
================ */
static void IN_CollectInput( float *forward, float *side, float *pitch, float *yaw, qboolean includeMouse )
{
	// Mouse deltas are published in evdev_dx/evdev_dy and applied by the client.
	(void)includeMouse;

	Joy_FinalizeMove( forward, side, yaw, pitch );
	Touch_GetMove( forward, side, yaw, pitch );

	if( look_filter.value )
	{
		*pitch = ( inputstate.lastpitch + *pitch ) / 2;
		*yaw   = ( inputstate.lastyaw   + *yaw   ) / 2;
		inputstate.lastpitch = *pitch;
		inputstate.lastyaw   = *yaw;
	}
}

void IN_EngineAppendMove( float frametime, usercmd_t *cmd, qboolean active )
{
	float forward, side, pitch, yaw;

	if( clgame.dllFuncs.pfnLookEvent ) return;
	if( cls.key_dest != key_game || cl.paused || cl.intermission ) return;

	forward = side = pitch = yaw = 0;

	if( active )
	{
		float sensitivity = 1;

		IN_CollectInput( &forward, &side, &pitch, &yaw,
			in_mouseinitialized && !m_ignore.value );

		IN_JoyAppendMove( cmd, forward, side );

		if( pitch || yaw )
		{
			cmd->viewangles[YAW]   += yaw * sensitivity;
			cmd->viewangles[PITCH] += pitch * sensitivity;
			cmd->viewangles[PITCH]  = bound( -90, cmd->viewangles[PITCH], 90 );
			VectorCopy( cmd->viewangles, cl.viewangles );
		}
	}
}

static void IN_Commands( void )
{
#if XASH_USE_EVDEV
	IN_EvdevFrame();
#endif

	if( clgame.dllFuncs.pfnLookEvent )
	{
		float forward = 0, side = 0, pitch = 0, yaw = 0;
		IN_CollectInput( &forward, &side, &pitch, &yaw,
			in_mouseinitialized && !m_ignore.value );

		if( cls.key_dest == key_game )
		{
			clgame.dllFuncs.pfnLookEvent( yaw, pitch );
			clgame.dllFuncs.pfnMoveEvent( forward, side );
		}
	}

	if( !in_mouseinitialized )
		return;

	IN_ApplyImGuiCursor();
	IN_CheckMouseState( in_mouseactive );
#if XASH_USE_EVDEV
	EVDev_Publish();
#endif
}

void Host_InputFrame( void )
{
	IN_Commands();
	IN_MouseMove();
}