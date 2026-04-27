/*
 * steam_login_client.h - shim C ABI para autenticação Steam oficial.
 *
 * Não contém Steamworks SDK. Para ativar de verdade, compile
 * steam_login_client.cpp com XASH_ENABLE_STEAM_AUTH definido e inclua os
 * headers public/steam do Steamworks SDK.
 */
#ifndef XASH_STEAM_LOGIN_CLIENT_H
#define XASH_STEAM_LOGIN_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int SteamLogin_ClientEnabled( void );
int SteamLogin_ClientInit( void );
void SteamLogin_ClientShutdown( void );
void SteamLogin_ClientFrame( void );
int SteamLogin_ClientLoggedOn( void );
uint64_t SteamLogin_ClientSteamID64( void );
int SteamLogin_ClientGetAuthTicket( unsigned char *out, size_t max_len, uint32_t *out_len );
void SteamLogin_ClientCancelTicket( int ticket_handle );
const char *SteamLogin_ClientStatus( void );

/* Futuro suporte para servidor Xash modificado. Para servidor GoldSrc/Steam
 * existente, o próprio protocolo/sign-on do servidor deve liberar ou rejeitar. */
void SteamLogin_ClientMarkAuthPending( void );
void SteamLogin_ClientMarkAuthAccepted( void );
void SteamLogin_ClientMarkAuthFailed( const char *reason );
int SteamLogin_ClientCanActivate( void );

#ifdef __cplusplus
}
#endif

#endif /* XASH_STEAM_LOGIN_CLIENT_H */
