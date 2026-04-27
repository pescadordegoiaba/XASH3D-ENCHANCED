/* steam_login_server.h - scaffold opcional para servidor Xash próprio. */
#ifndef XASH_STEAM_LOGIN_SERVER_H
#define XASH_STEAM_LOGIN_SERVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int SteamLogin_ServerInit( unsigned int ip, unsigned short steam_port, unsigned short game_port, unsigned short query_port, int secure, const char *version );
void SteamLogin_ServerFrame( void );
void SteamLogin_ServerShutdown( void );
int SteamLogin_ServerBeginAuth( const unsigned char *ticket, size_t ticket_len, uint64_t steamid64 );
void SteamLogin_ServerEndAuth( uint64_t steamid64 );
const char *SteamLogin_ServerStatus( void );

#ifdef __cplusplus
}
#endif

#endif /* XASH_STEAM_LOGIN_SERVER_H */
