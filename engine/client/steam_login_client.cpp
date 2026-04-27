/*
 * steam_login_client.cpp
 *
 * Shim opcional Steamworks para Xash. O modo padrão compila como stub e mantém
 * steam_login 0 funcionando sem depender do Steamworks SDK.
 *
 * Para ativar:
 *   - instale/baixe o Steamworks SDK oficial;
 *   - compile este arquivo com -DXASH_ENABLE_STEAM_AUTH;
 *   - adicione include para <SteamworksSDK>/public;
 *   - linke com steam_api/libsteam_api da mesma arquitetura do engine;
 *   - rode com Steam aberto e, em desenvolvimento, steam_appid.txt correto.
 *
 * Este código não falsifica SteamID nem gera ticket non-Steam. Ele apenas chama
 * a Steam API oficial quando steam_login estiver em 1.
 */
#include "steam_login_client.h"

#include <stdio.h>
#include <string.h>

extern "C" {
#include "client.h"
}

extern "C" convar_t steam_login;

static char g_steam_status[256] = "steam_login: modo Steam ainda não inicializado";
static int g_auth_pending = 0;
static int g_auth_accepted = 0;

#ifdef XASH_ENABLE_STEAM_AUTH
#include "steam/steam_api.h"
static int g_steam_inited = 0;
static HAuthTicket g_ticket = k_HAuthTicketInvalid;
#endif

static void SteamLogin_SetStatus( const char *msg )
{
	if( !msg ) msg = "";
	snprintf( g_steam_status, sizeof( g_steam_status ), "%s", msg );
}

extern "C" int SteamLogin_ClientEnabled( void )
{
	return steam_login.value != 0.0f;
}

extern "C" const char *SteamLogin_ClientStatus( void )
{
	return g_steam_status;
}

extern "C" int SteamLogin_ClientInit( void )
{
	if( !SteamLogin_ClientEnabled() )
	{
		SteamLogin_SetStatus( "steam_login 0: usando conexão padrão/non-Steam" );
		return 0;
	}

#ifndef XASH_ENABLE_STEAM_AUTH
	SteamLogin_SetStatus( "steam_login 1 solicitado, mas o engine foi compilado sem XASH_ENABLE_STEAM_AUTH" );
	return 0;
#else
	if( g_steam_inited )
		return 1;

	if( !SteamAPI_Init() )
	{
		SteamLogin_SetStatus( "SteamAPI_Init falhou: abra o Steam, confira steam_appid.txt em dev e a arquitetura da libsteam_api" );
		return 0;
	}

	if( !SteamUser() || !SteamUser()->BLoggedOn() )
	{
		SteamLogin_SetStatus( "SteamAPI iniciou, mas nenhum usuário Steam está logado" );
		return 0;
	}

	g_steam_inited = 1;
	SteamLogin_SetStatus( "SteamAPI inicializada e usuário logado" );
	return 1;
#endif
}

extern "C" void SteamLogin_ClientFrame( void )
{
#ifdef XASH_ENABLE_STEAM_AUTH
	if( g_steam_inited )
		SteamAPI_RunCallbacks();
#endif
}

extern "C" int SteamLogin_ClientLoggedOn( void )
{
#ifndef XASH_ENABLE_STEAM_AUTH
	return 0;
#else
	return g_steam_inited && SteamUser() && SteamUser()->BLoggedOn();
#endif
}

extern "C" uint64_t SteamLogin_ClientSteamID64( void )
{
#ifndef XASH_ENABLE_STEAM_AUTH
	return 0ULL;
#else
	if( !SteamLogin_ClientLoggedOn() )
		return 0ULL;
	return SteamUser()->GetSteamID().ConvertToUint64();
#endif
}

extern "C" int SteamLogin_ClientGetAuthTicket( unsigned char *out, size_t max_len, uint32_t *out_len )
{
	if( out_len ) *out_len = 0;
	if( !out || !out_len || max_len == 0 )
	{
		SteamLogin_SetStatus( "buffer inválido para Steam auth ticket" );
		return 0;
	}

#ifndef XASH_ENABLE_STEAM_AUTH
	SteamLogin_SetStatus( "auth ticket indisponível: compile com XASH_ENABLE_STEAM_AUTH e Steamworks SDK" );
	return 0;
#else
	if( !SteamLogin_ClientInit() )
		return 0;

	if( g_ticket != k_HAuthTicketInvalid )
	{
		SteamUser()->CancelAuthTicket( g_ticket );
		g_ticket = k_HAuthTicketInvalid;
	}

	uint32 cb_ticket = 0;

#ifdef XASH_STEAMWORKS_OLD_TICKET_API
	g_ticket = SteamUser()->GetAuthSessionTicket( out, (int)max_len, &cb_ticket );
#else
	SteamNetworkingIdentity remote_identity;
	remote_identity.Clear();
	g_ticket = SteamUser()->GetAuthSessionTicket( out, (int)max_len, &cb_ticket, &remote_identity );
#endif

	if( g_ticket == k_HAuthTicketInvalid || cb_ticket == 0 )
	{
		SteamLogin_SetStatus( "GetAuthSessionTicket retornou ticket inválido" );
		return 0;
	}

	*out_len = cb_ticket;
	g_auth_pending = 1;
	g_auth_accepted = 0;
	snprintf( g_steam_status, sizeof( g_steam_status ),
		"Steam auth ticket oficial gerado: %u bytes, SteamID64=%llu",
		(unsigned)cb_ticket, (unsigned long long)SteamLogin_ClientSteamID64() );
	return (int)g_ticket;
#endif
}

extern "C" void SteamLogin_ClientCancelTicket( int ticket_handle )
{
#ifndef XASH_ENABLE_STEAM_AUTH
	(void)ticket_handle;
#else
	if( g_steam_inited && ticket_handle != 0 )
		SteamUser()->CancelAuthTicket( (HAuthTicket)ticket_handle );
	if( ticket_handle == (int)g_ticket )
		g_ticket = k_HAuthTicketInvalid;
#endif
	g_auth_pending = 0;
	g_auth_accepted = 0;
}

extern "C" void SteamLogin_ClientShutdown( void )
{
#ifdef XASH_ENABLE_STEAM_AUTH
	if( g_steam_inited )
	{
		if( g_ticket != k_HAuthTicketInvalid )
		{
			SteamUser()->CancelAuthTicket( g_ticket );
			g_ticket = k_HAuthTicketInvalid;
		}
		SteamAPI_Shutdown();
	}
	g_steam_inited = 0;
#endif
	g_auth_pending = 0;
	g_auth_accepted = 0;
	SteamLogin_SetStatus( "SteamAPI finalizada" );
}

extern "C" void SteamLogin_ClientMarkAuthPending( void )
{
	g_auth_pending = 1;
	g_auth_accepted = 0;
}

extern "C" void SteamLogin_ClientMarkAuthAccepted( void )
{
	g_auth_pending = 0;
	g_auth_accepted = 1;
	SteamLogin_SetStatus( "Steam auth aceita pelo servidor" );
}

extern "C" void SteamLogin_ClientMarkAuthFailed( const char *reason )
{
	g_auth_pending = 0;
	g_auth_accepted = 0;
	if( reason && reason[0] )
		SteamLogin_SetStatus( reason );
	else
		SteamLogin_SetStatus( "Steam auth recusada pelo servidor" );
}

extern "C" int SteamLogin_ClientCanActivate( void )
{
	/* Não trave ca_active por padrão: servidores GoldSrc Steam existentes liberam
	 * a conexão pelo próprio signon. Esta função fica pronta para protocolo Xash
	 * customizado com callback explícito steam_auth_ok/steam_auth_fail. */
	if( !SteamLogin_ClientEnabled() ) return 1;
	if( !g_auth_pending ) return 1;
	return g_auth_accepted != 0;
}
