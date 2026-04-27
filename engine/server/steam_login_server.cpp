/*
 * steam_login_server.cpp
 * Scaffold para servidor Xash próprio. Para entrar em servidores existentes
 * como cliente, o principal é o steam_login_client.cpp + cl_steam.c.
 */
#include "steam_login_server.h"

#include <stdio.h>
#include <string.h>

extern "C" {
#include "common.h"
}

static char g_server_status[256] = "Steam GameServer API não inicializada";

static void Server_SetStatus( const char *msg )
{
	if( !msg ) msg = "";
	snprintf( g_server_status, sizeof( g_server_status ), "%s", msg );
}

extern "C" const char *SteamLogin_ServerStatus( void )
{
	return g_server_status;
}

#ifndef XASH_ENABLE_STEAM_AUTH
extern "C" int SteamLogin_ServerInit( unsigned int ip, unsigned short steam_port, unsigned short game_port, unsigned short query_port, int secure, const char *version )
{
	(void)ip; (void)steam_port; (void)game_port; (void)query_port; (void)secure; (void)version;
	Server_SetStatus( "servidor compilado sem XASH_ENABLE_STEAM_AUTH" );
	return 0;
}
extern "C" void SteamLogin_ServerFrame( void ) {}
extern "C" void SteamLogin_ServerShutdown( void ) {}
extern "C" int SteamLogin_ServerBeginAuth( const unsigned char *ticket, size_t ticket_len, uint64_t steamid64 )
{
	(void)ticket; (void)ticket_len; (void)steamid64;
	Server_SetStatus( "BeginAuth indisponível sem Steamworks SDK" );
	return 0;
}
extern "C" void SteamLogin_ServerEndAuth( uint64_t steamid64 ) { (void)steamid64; }
#else
#include "steam/steam_gameserver.h"

static int g_server_inited = 0;

extern "C" int SteamLogin_ServerInit( unsigned int ip, unsigned short steam_port, unsigned short game_port, unsigned short query_port, int secure, const char *version )
{
	EServerMode mode = secure ? eServerModeAuthenticationAndSecure : eServerModeAuthentication;
	if( g_server_inited ) return 1;
	if( !SteamGameServer_Init( ip, steam_port, game_port, query_port, mode, version ? version : "1.0.0.0" ) )
	{
		Server_SetStatus( "SteamGameServer_Init falhou" );
		return 0;
	}
	g_server_inited = 1;
	Server_SetStatus( "Steam GameServer API inicializada" );
	return 1;
}

extern "C" void SteamLogin_ServerFrame( void )
{
	if( g_server_inited )
		SteamGameServer_RunCallbacks();
}

extern "C" void SteamLogin_ServerShutdown( void )
{
	if( g_server_inited )
		SteamGameServer_Shutdown();
	g_server_inited = 0;
	Server_SetStatus( "Steam GameServer API finalizada" );
}

extern "C" int SteamLogin_ServerBeginAuth( const unsigned char *ticket, size_t ticket_len, uint64_t steamid64 )
{
	if( !g_server_inited || !ticket || ticket_len == 0 || steamid64 == 0ULL )
	{
		Server_SetStatus( "BeginAuth inválido: API/ticket/SteamID ausente" );
		return 0;
	}
	CSteamID steam_id( steamid64 );
	EBeginAuthSessionResult r = SteamGameServer()->BeginAuthSession( ticket, (int)ticket_len, steam_id );
	if( r != k_EBeginAuthSessionResultOK )
	{
		snprintf( g_server_status, sizeof( g_server_status ), "BeginAuthSession recusou imediatamente: %d", (int)r );
		return 0;
	}
	Server_SetStatus( "BeginAuthSession OK: aguardando ValidateAuthTicketResponse_t" );
	return 1;
}

extern "C" void SteamLogin_ServerEndAuth( uint64_t steamid64 )
{
	if( g_server_inited && steamid64 != 0ULL )
		SteamGameServer()->EndAuthSession( CSteamID( steamid64 ) );
}
#endif
