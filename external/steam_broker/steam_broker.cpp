/*
 * steam_broker.cpp - SteamBroker externo para Xash3D.
 *
 * Este programa fica fora da engine GPL e usa o Steamworks SDK oficial.
 *
 * Protocolo esperado pelo engine/client/cl_steam.c:
 *
 * Xash -> broker, UDP cru:
 *   sb_connect <ip:port> <serverSteamID64> <true|false> <challenge>
 *   sb_disconnect <ip:port> <challenge>
 *   sb_gamedir <gamedir>
 *   sb_terminate
 *
 * Broker -> Xash, pacote connectionless/OOB:
 *   0xFF 0xFF 0xFF 0xFF "sb_connect\n"
 *   int32_le challenge
 *   uint64_le steamid64
 *   uint32_le ticket_len
 *   ticket bytes
 *
 * Segurança/legal:
 *   - não falsifica SteamID;
 *   - não implementa bypass de DRM/VAC/anti-cheat;
 *   - só chama SteamAPI_Init + ISteamUser::GetAuthSessionTicket.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "steam/steam_api.h"

static volatile sig_atomic_t g_running = 1;

struct TicketEntry
{
    HAuthTicket handle = k_HAuthTicketInvalid;
    uint32_t challenge = 0;
    std::string server;
};

static std::map<std::string, TicketEntry> g_tickets;

static void on_signal(int)
{
    g_running = 0;
}

static void put_u32_le(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v & 0xff));
    out.push_back((uint8_t)((v >> 8) & 0xff));
    out.push_back((uint8_t)((v >> 16) & 0xff));
    out.push_back((uint8_t)((v >> 24) & 0xff));
}

static void put_u64_le(std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out.push_back((uint8_t)((v >> (i * 8)) & 0xff));
}

static std::string key_for(const std::string& server, uint32_t challenge)
{
    return server + "#" + std::to_string(challenge);
}

static bool parse_listen(const char* s, std::string& ip, uint16_t& port)
{
    const char* colon = strrchr(s, ':');
    if (!colon || colon == s)
        return false;
    ip.assign(s, colon - s);
    long p = strtol(colon + 1, nullptr, 10);
    if (p <= 0 || p > 65535)
        return false;
    port = (uint16_t)p;
    return true;
}

static bool split_addr(const std::string& server, std::string& ip, uint16_t& port)
{
    size_t colon = server.rfind(':');
    if (colon == std::string::npos || colon == 0)
        return false;
    ip = server.substr(0, colon);
    long p = strtol(server.c_str() + colon + 1, nullptr, 10);
    if (p <= 0 || p > 65535)
        return false;
    port = (uint16_t)p;
    return true;
}

static void cancel_all_tickets()
{
    for (auto& kv : g_tickets)
    {
        if (kv.second.handle != k_HAuthTicketInvalid)
            SteamUser()->CancelAuthTicket(kv.second.handle);
    }
    g_tickets.clear();
}

static void cancel_ticket_for(const std::string& server, uint32_t challenge)
{
    auto it = g_tickets.find(key_for(server, challenge));
    if (it != g_tickets.end())
    {
        if (it->second.handle != k_HAuthTicketInvalid)
            SteamUser()->CancelAuthTicket(it->second.handle);
        printf("[broker] ticket cancelado para %s challenge=%u\n", server.c_str(), challenge);
        g_tickets.erase(it);
    }
}

static bool send_ticket_response(int sock, const sockaddr_in& to, const std::string& server, uint64_t serverSteamId, bool secure, uint32_t challenge)
{
    if (!SteamUser() || !SteamUser()->BLoggedOn())
    {
        fprintf(stderr, "[broker] SteamUser indisponível ou usuário não logado.\n");
        return false;
    }

    uint8_t ticket[4096];
    uint32 ticket_len = 0;

    /*
     * API moderna:
     * HAuthTicket GetAuthSessionTicket(void *pTicket, int cbMaxTicket, uint32 *pcbTicket,
     *                                 const SteamNetworkingIdentity *pIdentityRemote);
     *
     * Para servidores GoldSrc existentes, o Xash já envia serverSteamId quando conhece.
     * Mesmo assim, alguns servidores antigos não expõem identidade confiável antes do handshake.
     * Por isso, usamos nullptr por padrão para manter compatibilidade.
     *
     * Se quiser experimentar identidade remota, compile com:
     *   make REMOTE_IDENTITY=1
     */
#ifdef STEAM_BROKER_REMOTE_IDENTITY
    SteamNetworkingIdentity remote;
    memset(&remote, 0, sizeof(remote));
    if (serverSteamId != 0)
    {
        remote.SetSteamID(CSteamID((uint64)serverSteamId));
    }
    else
    {
        std::string ip;
        uint16_t port = 0;
        if (split_addr(server, ip, port))
        {
            SteamNetworkingIPAddr addr;
            addr.Clear();
            addr.SetIPv4(inet_addr(ip.c_str()), port);
            remote.SetIPAddr(addr);
        }
    }
    HAuthTicket h = SteamUser()->GetAuthSessionTicket(ticket, sizeof(ticket), &ticket_len, &remote);
#else
    HAuthTicket h = SteamUser()->GetAuthSessionTicket(ticket, sizeof(ticket), &ticket_len, nullptr);
#endif

    if (h == k_HAuthTicketInvalid || ticket_len == 0 || ticket_len > sizeof(ticket))
    {
        fprintf(stderr, "[broker] GetAuthSessionTicket falhou. handle=%u len=%u\n", (unsigned)h, (unsigned)ticket_len);
        return false;
    }

    TicketEntry e;
    e.handle = h;
    e.challenge = challenge;
    e.server = server;
    g_tickets[key_for(server, challenge)] = e;

    uint64_t steamid64 = SteamUser()->GetSteamID().ConvertToUint64();

    std::vector<uint8_t> out;
    out.reserve(4 + 11 + 4 + 8 + 4 + ticket_len);
    put_u32_le(out, 0xFFFFFFFFu);
    const char marker[] = "sb_connect\n";
    out.insert(out.end(), marker, marker + sizeof(marker) - 1);
    put_u32_le(out, challenge);
    put_u64_le(out, steamid64);
    put_u32_le(out, ticket_len);
    out.insert(out.end(), ticket, ticket + ticket_len);

    ssize_t sent = sendto(sock, out.data(), out.size(), 0, (const sockaddr*)&to, sizeof(to));
    if (sent < 0)
    {
        perror("[broker] sendto");
        SteamUser()->CancelAuthTicket(h);
        g_tickets.erase(key_for(server, challenge));
        return false;
    }

    char toip[64];
    inet_ntop(AF_INET, &to.sin_addr, toip, sizeof(toip));
    printf("[broker] ticket enviado: xash=%s:%u server=%s serverSteamID=%llu secure=%s challenge=%u steamID64=%llu ticket=%u bytes\n",
           toip, ntohs(to.sin_port), server.c_str(), (unsigned long long)serverSteamId,
           secure ? "true" : "false", challenge, (unsigned long long)steamid64, (unsigned)ticket_len);
    return true;
}

static void handle_packet(int sock, const char* buf, ssize_t len, const sockaddr_in& from)
{
    std::string msg(buf, buf + len);

    if (msg.compare(0, 10, "sb_connect") == 0)
    {
        char server[128] = {0};
        char secure_s[16] = {0};
        unsigned long long serverSteamId = 0;
        unsigned int challenge = 0;

        int n = sscanf(msg.c_str(), "sb_connect %127s %llu %15s %u", server, &serverSteamId, secure_s, &challenge);
        if (n != 4)
        {
            fprintf(stderr, "[broker] pacote sb_connect inválido: %s\n", msg.c_str());
            return;
        }

        bool secure = (strcmp(secure_s, "true") == 0 || strcmp(secure_s, "1") == 0);
        send_ticket_response(sock, from, server, (uint64_t)serverSteamId, secure, challenge);
        return;
    }

    if (msg.compare(0, 13, "sb_disconnect") == 0)
    {
        char server[128] = {0};
        unsigned int challenge = 0;
        if (sscanf(msg.c_str(), "sb_disconnect %127s %u", server, &challenge) == 2)
            cancel_ticket_for(server, challenge);
        return;
    }

    if (msg.compare(0, 10, "sb_gamedir") == 0)
    {
        printf("[broker] gamedir recebido do Xash: %s\n", msg.c_str() + 10);
        return;
    }

    if (msg.compare(0, 12, "sb_terminate") == 0)
    {
        printf("[broker] terminate recebido; encerrando.\n");
        g_running = 0;
        return;
    }

    fprintf(stderr, "[broker] pacote desconhecido: %.*s\n", (int)len, buf);
}

static void usage(const char* argv0)
{
    printf("Uso:\n");
    printf("  %s --listen 127.0.0.1:27420 [--appid 10]\n", argv0);
    printf("\n");
    printf("Notas:\n");
    printf("  --appid cria/atualiza steam_appid.txt no diretório atual antes do SteamAPI_Init.\n");
    printf("  Para Counter-Strike 1.6, o AppID comum é 10. Use apenas com jogo legítimo na sua conta.\n");
}

int main(int argc, char** argv)
{
    std::string listen_ip = "127.0.0.1";
    uint16_t listen_port = 27420;
    const char* appid = nullptr;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc)
        {
            if (!parse_listen(argv[++i], listen_ip, listen_port))
            {
                fprintf(stderr, "Endereço inválido para --listen.\n");
                return 2;
            }
        }
        else if (strcmp(argv[i], "--appid") == 0 && i + 1 < argc)
        {
            appid = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if (appid && *appid)
    {
        FILE* f = fopen("steam_appid.txt", "wb");
        if (!f)
        {
            perror("steam_appid.txt");
            return 2;
        }
        fprintf(f, "%s\n", appid);
        fclose(f);
        printf("[broker] steam_appid.txt escrito com AppID %s\n", appid);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (!SteamAPI_Init())
    {
        fprintf(stderr, "[broker] SteamAPI_Init falhou.\n");
        fprintf(stderr, "         Abra o Steam, faça login, confira steam_appid.txt e libsteam_api.so.\n");
        return 1;
    }

    if (!SteamUser() || !SteamUser()->BLoggedOn())
    {
        fprintf(stderr, "[broker] SteamAPI iniciou, mas usuário não está logado.\n");
        SteamAPI_Shutdown();
        return 1;
    }

    printf("[broker] Steam logado. SteamID64=%llu\n",
           (unsigned long long)SteamUser()->GetSteamID().ConvertToUint64());

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        SteamAPI_Shutdown();
        return 1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    if (inet_pton(AF_INET, listen_ip.c_str(), &addr.sin_addr) != 1)
    {
        fprintf(stderr, "IP inválido: %s\n", listen_ip.c_str());
        close(sock);
        SteamAPI_Shutdown();
        return 2;
    }

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(sock);
        SteamAPI_Shutdown();
        return 1;
    }

    printf("[broker] ouvindo UDP em %s:%u\n", listen_ip.c_str(), (unsigned)listen_port);
    printf("[broker] agora abra o Xash e use: cl_steam_broker_addr %s:%u; steam_login 1; connect IP:PORT\n",
           listen_ip.c_str(), (unsigned)listen_port);

    while (g_running)
    {
        SteamAPI_RunCallbacks();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 16000;

        int r = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        if (r > 0 && FD_ISSET(sock, &rfds))
        {
            char buf[2048];
            sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromlen);
            if (n > 0)
                handle_packet(sock, buf, n, from);
        }
    }

    printf("[broker] encerrando, cancelando tickets...\n");
    cancel_all_tickets();
    close(sock);
    SteamAPI_Shutdown();
    return 0;
}
