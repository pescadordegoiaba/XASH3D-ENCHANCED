/*
cl_steam.c - steam(tm) broker implementation
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <inttypes.h>
#include "common.h"
#include "client.h"

// What is a broker?
// From Wikipedia, the free encyclopedia:
// "The broker pattern is an architecture pattern that involves the use of an
// intermediary software entity, called a "broker", to facilitate communication
// between two or more software components. The broker acts as a "middleman"
// between the components, allowing them to communicate without being aware of
// each other's existence.
//
// Due to proprietary nature of Steamworks SDK, it cannot be run on same amount
// of platforms supported by Xash3D FWGS, neither we can link directly due to
// GNU GPLv3 license. However, here comes the broker, by running it (in trusted
// network, preferrably) on a machine that has Steam client installed, the
// engine can communicate with it, acquiring needed information to log-in into
// Steam protected multiplayer servers.
static CVAR_DEFINE_AUTO( cl_steam_broker_addr, "127.0.0.1:27420", FCVAR_ARCHIVE, "address of steam broker instance" );

static struct
{
	netadr_t adr;
	int challenge;
	netadr_t serveradr;
	qboolean announced;
	qboolean addr_initialized;
} broker;

static qboolean SteamBroker_Enabled( void )
{
	return steam_login.value != 0.0f || Q_stricmp( cl_ticket_generator.string, "steam" ) == 0;
}

static qboolean SteamBroker_UpdateBrokerAddress( void )
{
	netadr_t parsed = { 0 };

	if( !NET_StringToAdr( cl_steam_broker_addr.string, &parsed ))
		return false;

	broker.adr = parsed;
	return true;
}

qboolean SteamBroker_InitiateGameConnection( netadr_t serveradr, int challenge )
{
	if( FBitSet( cl_steam_broker_addr.flags, FCVAR_CHANGED ))
	{
		NET_NetadrSetType( &broker.adr, NA_UNDEFINED );
		broker.addr_initialized = false;
		ClearBits( cl_steam_broker_addr.flags, FCVAR_CHANGED );
	}

	if( !SteamBroker_UpdateBrokerAddress() )
	{
		Con_Printf( S_ERROR "steam_login: endereço do SteamBroker inválido: %s\n", cl_steam_broker_addr.string );
		return false;
	}
	broker.addr_initialized = true;

	// only ipv4 supported
	if( NET_NetadrType( &serveradr ) != NA_IP )
		return false;

	if( !SteamBroker_Enabled() )
		return false;

	if( !SteamBroker_UpdateBrokerAddress() )
	{
		Con_Printf( "steam_login 1: endereco invalido em cl_steam_broker_addr: %s\n", cl_steam_broker_addr.string );
		return false;
	}

	Con_Printf( "steam_login 1: pedindo ticket ao SteamBroker em %s para %s...\n", cl_steam_broker_addr.string, NET_AdrToString( serveradr ) );

	broker.challenge = challenge;
	broker.serveradr = serveradr;

	// sb_connect <ip:port> <server's steam id> <secure> <challenge>
	// the message calls
	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_connect %s %"PRIu64" %s %d", NET_AdrToString( serveradr ), cls.server_steamid, cls.vac2_secure ? "true" : "false", broker.challenge );

	NET_SendPacket( NS_CLIENT, len, buf, broker.adr );

	return true;
}

void SteamBroker_TerminateGameConnection( void )
{
	if( NET_NetadrType( &cls.serveradr ) != NA_IP )
		return;

	if( !SteamBroker_Enabled() )
		return;

	// sb_disconnect <ip:port> <challenge>
	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_disconnect %s %d", NET_AdrToString( cls.serveradr ), broker.challenge );

	NET_SendPacket( NS_CLIENT, len, buf, broker.adr );
}

void SteamBroker_AnnounceGameStart( const char *gamedir )
{
	if( !SteamBroker_Enabled() )
		return;

	NET_Config( true, true ); // initialize sockets to be able to send packets to broker

	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_gamedir %s", gamedir );

	NET_SendPacket( NS_CLIENT, len, buf, broker.adr );
}

void SteamBroker_AnnounceGameShutdown( netadr_t broker_addr )
{
	NET_SendPacket( NS_CLIENT, sizeof( "sb_terminate" ) - 1, "sb_terminate", broker_addr );
}

void SteamBroker_Frame( void )
{
	if( !SteamBroker_Enabled() )
		return;

	qboolean restart = FBitSet( cl_steam_broker_addr.flags|cl_ticket_generator.flags|steam_login.flags, FCVAR_CHANGED );

	if( !broker.addr_initialized )
	{
		if( !SteamBroker_UpdateBrokerAddress( ))
		{ 
			Con_Printf( "%s: failed to resolve broker address \"%s\"\n", __func__, cl_steam_broker_addr.string );
			return;
		}
		else
		{
			broker.addr_initialized = true;
		}
	}

	if( restart )
	{
		/* steam_broker_patch: force reparse address */
		NET_NetadrSetType( &broker.adr, NA_UNDEFINED );
		broker.addr_initialized = false;
		broker.announced = false;
		ClearBits( cl_ticket_generator.flags, FCVAR_CHANGED );
		ClearBits( steam_login.flags, FCVAR_CHANGED );
		ClearBits( cl_steam_broker_addr.flags, FCVAR_CHANGED );
	}

	if( !broker.announced )
	{
		netadr_t previous_addr = broker.adr;
		if( !SteamBroker_UpdateBrokerAddress( ))
		{ 
			Con_Printf( "%s: failed to resolve broker address \"%s\"\n", __func__, cl_steam_broker_addr.string );
		}
		else
		{
			if( restart )
			{
				// terminate old steam broker if instance address has changed
				SteamBroker_AnnounceGameShutdown( previous_addr ); 
			}
			SteamBroker_AnnounceGameStart( GI->gamefolder );
		}
		broker.announced = true;
	}
}

void SteamBroker_HandlePacket( netadr_t from, sizebuf_t *msg )
{
	// message format
	// sb_connect\n<4 byte challenge><8 byte steamid><unsigned 4 byte len><len bytes ticket>
	int challenge;
	uint32_t len;
	uint8_t ticket[2048]; // 2048 bytes according to SDK docs

	if( !NET_CompareAdr( from, broker.adr ))
		return;

	challenge = MSG_ReadLong( msg );

	if( broker.challenge != challenge ) // TODO: print error
		return;

	MSG_ReadBytes( msg, cls.steamid, sizeof( cls.steamid ));

	len = MSG_ReadDword( msg );
	if( len > sizeof( ticket ))
	{
		Con_Printf( S_ERROR "steam_login: ticket do SteamBroker excedeu o limite interno: %u bytes.\n", (unsigned)len );
		cls.broker_wait = false;
		return;
	}

	MSG_ReadBytes( msg, ticket, len );

	Con_Printf( "%s: SteamID: %"PRIu64", ticket: [%d, %d, %d, %d...]\n", __func__, *(uint64_t *)cls.steamid, ticket[0], ticket[1], ticket[2], ticket[3] );

	CL_SendGoldSrcConnectPacket( broker.serveradr, challenge, ticket, len );

	cls.broker_wait = false;
}

void SteamBroker_Init( void )
{
	broker.announced = false;
	broker.addr_initialized = false;
	Cvar_RegisterVariable( &cl_steam_broker_addr );
	NET_NetadrSetType( &broker.adr, NA_UNDEFINED );
}

void SteamBroker_Shutdown( void )
{
	if( !SteamBroker_Enabled() )
		return;

	SteamBroker_AnnounceGameShutdown( broker.adr );
}
