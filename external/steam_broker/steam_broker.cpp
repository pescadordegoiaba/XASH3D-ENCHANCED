/*
 * steam_broker.cpp - SteamBroker externo para Xash3D/GoldSrc.
 *
 * Este broker usa a API legacy ISteamUser::InitiateGameConnection
 * quando disponível. Isso gera um auth blob de jogo/servidor, não apenas
 * um GetAuthSessionTicket moderno genérico.
 *
 * Não burla Steam/VAC/DRM. Exige Steam aberto, conta logada e licença do AppID.
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

struct ConnEntry
{
    std::string server;
    uint32_t challenge = 0;
    uint32_t ip_host = 0;
    uint16_t port_host = 0;
};

static std::map<std::string, ConnEntry> g_conns;

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

static bool parse_host_port(const char* s, std::string& ip, uint16_t& port)
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

static bool parse_server_ipv4(const std::string& server, uint32_t& ip_host, uint16_t& port_host)
{
    std::string ip;
    uint16_t port = 0;
    if (!parse_host_port(server.c_str(), ip, port))
        return false;

    in_addr addr{};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1)
        return false;

    ip_host = ntohl(addr.s_addr); // Steamworks quer host order.
    port_host = port;
    return true;
}

static void terminate_one(const ConnEntry& e)
{
    if (SteamUser())
    {
#ifdef STEAM_BROKER_DEPRECATED_SUFFIX
        SteamUser()->TerminateGameConnection_DEPRECATED(e.ip_host, e.port_host);
#else
        SteamUser()->TerminateGameConnection(e.ip_host, e.port_host);
#endif
    }
    printf("[broker] TerminateGameConnection: %s challenge=%u\n",
           e.server.c_str(), e.challenge);
}

static void terminate_key(const std::string& server, uint32_t challenge)
{
    auto it = g_conns.find(key_for(server, challenge));
    if (it != g_conns.end())
    {
        terminate_one(it->second);
        g_conns.erase(it);
    }
}

static void terminate_all()
{
    for (const auto& kv : g_conns)
        terminate_one(kv.second);
    g_conns.clear();
}

static bool send_auth_blob_response(
    int sock,
    const sockaddr_in& to,
    const std::string& server,
    uint64_t serverSteamID,
    bool secure,
    uint32_t challenge)
{
    if (!SteamUser() || !SteamUser()->BLoggedOn())
    {
        fprintf(stderr, "[broker] SteamUser indisponivel ou usuario nao logado.\n");
        return false;
    }

    uint32_t ip_host = 0;
    uint16_t port_host = 0;
    if (!parse_server_ipv4(server, ip_host, port_host))
    {
        fprintf(stderr, "[broker] servidor invalido ou nao-IPv4: %s\n", server.c_str());
        return false;
    }

    uint8_t auth_blob[2048];
    memset(auth_blob, 0, sizeof(auth_blob));

    CSteamID gameServerID((uint64)serverSteamID);

    int blob_len = 0;
#ifdef STEAM_BROKER_DEPRECATED_SUFFIX
    blob_len = SteamUser()->InitiateGameConnection_DEPRECATED(
        auth_blob,
        sizeof(auth_blob),
        gameServerID,
        ip_host,
        port_host,
        secure
    );
#else
    blob_len = SteamUser()->InitiateGameConnection(
        auth_blob,
        sizeof(auth_blob),
        gameServerID,
        ip_host,
        port_host,
        secure
    );
#endif

    if (blob_len <= 0 || blob_len > (int)sizeof(auth_blob))
    {
        fprintf(stderr,
                "[broker] InitiateGameConnection falhou. server=%s serverSteamID=%llu secure=%s ip_host=0x%08x port=%u len=%d\n",
                server.c_str(),
                (unsigned long long)serverSteamID,
                secure ? "true" : "false",
                ip_host,
                (unsigned)port_host,
                blob_len);
        return false;
    }

    ConnEntry e;
    e.server = server;
    e.challenge = challenge;
    e.ip_host = ip_host;
    e.port_host = port_host;
    g_conns[key_for(server, challenge)] = e;

    const uint64_t steamid64 = SteamUser()->GetSteamID().ConvertToUint64();

    std::vector<uint8_t> out;
    out.reserve(4 + 11 + 4 + 8 + 4 + blob_len);

    put_u32_le(out, 0xFFFFFFFFu);
    const char marker[] = "sb_connect\n";
    out.insert(out.end(), marker, marker + sizeof(marker) - 1);
    put_u32_le(out, challenge);
    put_u64_le(out, steamid64);
    put_u32_le(out, (uint32_t)blob_len);
    out.insert(out.end(), auth_blob, auth_blob + blob_len);

    ssize_t sent = sendto(sock, out.data(), out.size(), 0,
                          (const sockaddr*)&to, sizeof(to));
    if (sent < 0)
    {
        perror("[broker] sendto");
        terminate_key(server, challenge);
        return false;
    }

    char toip[64] = {0};
    inet_ntop(AF_INET, &to.sin_addr, toip, sizeof(toip));

    printf("[broker] legacy auth blob enviado: xash=%s:%u server=%s serverSteamID=%llu secure=%s challenge=%u steamID64=%llu blob=%d bytes\n",
           toip,
           ntohs(to.sin_port),
           server.c_str(),
           (unsigned long long)serverSteamID,
           secure ? "true" : "false",
           challenge,
           (unsigned long long)steamid64,
           blob_len);
    return true;
}

static void handle_packet(int sock, const char* buf, ssize_t len, const sockaddr_in& from)
{
    std::string msg(buf, buf + len);

    if (msg.compare(0, 10, "sb_connect") == 0)
    {
        char server[128] = {0};
        char secure_s[16] = {0};
        unsigned long long serverSteamID = 0;
        unsigned int challenge = 0;

        int n = sscanf(msg.c_str(), "sb_connect %127s %llu %15s %u",
                       server, &serverSteamID, secure_s, &challenge);
        if (n != 4)
        {
            fprintf(stderr, "[broker] sb_connect invalido: %s\n", msg.c_str());
            return;
        }

        const bool secure =
            strcmp(secure_s, "true") == 0 ||
            strcmp(secure_s, "1") == 0 ||
            strcmp(secure_s, "secure") == 0;

        send_auth_blob_response(sock, from, server, (uint64_t)serverSteamID, secure, challenge);
        return;
    }

    if (msg.compare(0, 13, "sb_disconnect") == 0)
    {
        char server[128] = {0};
        unsigned int challenge = 0;
        int n = sscanf(msg.c_str(), "sb_disconnect %127s %u", server, &challenge);
        if (n == 2)
            terminate_key(server, challenge);
        else
            fprintf(stderr, "[broker] sb_disconnect invalido: %s\n", msg.c_str());
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

    fprintf(stderr, "[broker] pacote desconhecido: %s\n", msg.c_str());
}

static int run_server(const char* listen_addr)
{
    std::string ip;
    uint16_t port = 0;
    if (!parse_host_port(listen_addr, ip, port))
    {
        fprintf(stderr, "[broker] --listen invalido: %s\n", listen_addr);
        return 2;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 2;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
    {
        fprintf(stderr, "[broker] IP invalido: %s\n", ip.c_str());
        close(sock);
        return 2;
    }

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(sock);
        return 2;
    }

    printf("[broker] ouvindo UDP em %s\n", listen_addr);
    printf("[broker] usando Steam legacy auth: InitiateGameConnection + TerminateGameConnection\n");

    while (g_running)
    {
        SteamAPI_RunCallbacks();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int r = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        if (r == 0)
            continue;

        sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        char buf[4096];
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (sockaddr*)&from, &fromlen);
        if (n > 0)
            handle_packet(sock, buf, n, from);
    }

    terminate_all();
    close(sock);
    return 0;
}

int main(int argc, char** argv)
{
    const char* listen = "127.0.0.1:27420";

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc)
        {
            listen = argv[++i];
        }
        else
        {
            fprintf(stderr, "uso: %s [--listen 127.0.0.1:27420]\n", argv[0]);
            return 2;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (!SteamAPI_Init())
    {
        fprintf(stderr, "[broker] SteamAPI_Init falhou.\n");
        fprintf(stderr, "         Confira Steam aberto/logado, steam_appid.txt=10, licença do CS 1.6 e libsteam_api.so 32-bit.\n");
        return 1;
    }

    if (!SteamUser() || !SteamUser()->BLoggedOn())
    {
        fprintf(stderr, "[broker] Steam iniciou, mas usuário não está logado.\n");
        SteamAPI_Shutdown();
        return 1;
    }

    printf("[broker] Steam logado. SteamID64=%llu\n",
           (unsigned long long)SteamUser()->GetSteamID().ConvertToUint64());

    int rc = run_server(listen);

    printf("[broker] encerrando.\n");
    SteamAPI_Shutdown();
    return rc;
}
