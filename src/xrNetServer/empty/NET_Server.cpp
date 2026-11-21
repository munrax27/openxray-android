#include "NET_Server.h"
#include "NET_Common.h"
#include "NET_Log.h"
#include "NET_Messages.h"
#include "stdafx.h" // XXX: why PCH is not on the top of the file???
#include "xrCore/buffer_vector.h"
#include "xrGameSpy/xrGameSpy_MainDefs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(__ANDROID__)
#include "../android/AndroidLanProtocol.h"
#include <cstdio>
#include <utility>
#include <vector>
#endif

#include <functional>

static INetLog* pSvNetLog = nullptr;

#define NET_BANNED_STR "Player banned by server!"
#define NET_PROTECTED_SERVER_STR "Access denied by protected server for this player!"
#define NET_NOTFOR_SUBNET_STR "Your IP does not present in server's subnet"

pcstr nameTraffic = "traffic.net";

XRNETSERVER_API int psNET_ServerUpdate = 30; // FPS
XRNETSERVER_API int psNET_ServerPending = 3;

XRNETSERVER_API ClientID BroadcastCID(0xffffffff);

void ip_address::set(pcstr src_string)
{
    u32 buff[4];
    int cnt = sscanf(src_string, "%d.%d.%d.%d", &buff[0], &buff[1], &buff[2], &buff[3]);
    if (cnt == 4) {
        m_data.a1 = u8(buff[0] & 0xff);
        m_data.a2 = u8(buff[1] & 0xff);
        m_data.a3 = u8(buff[2] & 0xff);
        m_data.a4 = u8(buff[3] & 0xff);
    } else {
        Msg("! Bad ipAddress format [%s]", src_string);
        m_data.data = 0;
    }
}

xr_string ip_address::to_string() const
{
    string128 res;
    xr_sprintf(res, sizeof(res), "%d.%d.%d.%d", m_data.a1, m_data.a2, m_data.a3, m_data.a4);
    return res;
}

void IBannedClient::Load(CInifile& ini, const shared_str& sect)
{
    HAddr.set(sect.c_str());

    tm _tm_banned;
    const shared_str& time_to = ini.r_string(sect, "time_to");

    [[maybe_unused]] int res_t = sscanf(time_to.c_str(), "%02d.%02d.%d_%02d:%02d:%02d", &_tm_banned.tm_mday, &_tm_banned.tm_mon,
        &_tm_banned.tm_year, &_tm_banned.tm_hour, &_tm_banned.tm_min, &_tm_banned.tm_sec);
    VERIFY(res_t == 6);

    _tm_banned.tm_mon -= 1;
    _tm_banned.tm_year -= 1900;

    BanTime = mktime(&_tm_banned);

    Msg("- loaded banned client %s to %s", HAddr.to_string().c_str(), BannedTimeTo().c_str());
}

void IBannedClient::Save(CInifile& ini)
{
    ini.w_string(HAddr.to_string().c_str(), "time_to", BannedTimeTo().c_str());
}

xr_string IBannedClient::BannedTimeTo() const
{
    string256 res;

    return res;
}

IClient::IClient(CTimer* timer)
    : stats(timer)
    , server(nullptr)
{
    dwTime_LastUpdate = 0;
    flags.bLocal = FALSE;
    flags.bConnected = FALSE;
    flags.bReconnect = FALSE;
    flags.bVerified = TRUE;
}

IClient::~IClient() {}

struct ClientStatisticImpl {
    u32 mps_receive, mps_receive_base;
    u32 mps_send, mps_send_base;
    u32 dwBaseTime;
    CTimer* device_timer;
};

IClientStatistic::IClientStatistic()
    : m_pimpl(xr_new<ClientStatisticImpl>())
{
}

IClientStatistic::IClientStatistic(CTimer* timer)
{
    ZeroMemory(this, sizeof(*this));
    m_pimpl = xr_new<ClientStatisticImpl>();
    m_pimpl->device_timer = timer;
    m_pimpl->dwBaseTime = TimeGlobal(m_pimpl->device_timer);
}

IClientStatistic::IClientStatistic(const IClientStatistic& rhs)
    : m_pimpl(xr_new<ClientStatisticImpl>())
{
    *m_pimpl = *rhs.m_pimpl;
}

IClientStatistic::~IClientStatistic()
{
    xr_delete(m_pimpl);
}

u32 IClientStatistic::getPing() const { return 0; }
u32 IClientStatistic::getBPS() const { return 0; }
u32 IClientStatistic::getPeakBPS() const { return 0; }
u32 IClientStatistic::getDroppedCount() const { return 0; }
u32 IClientStatistic::getRetriedCount() const { return 0; }
u32 IClientStatistic::getMPS_Receive() const { return m_pimpl->mps_receive; }
u32 IClientStatistic::getMPS_Send() const { return m_pimpl->mps_send; }
u32 IClientStatistic::getReceivedPerSec() const { return dwBytesReceivedPerSec; }
u32 IClientStatistic::getSendedPerSec() const { return dwBytesSendedPerSec; }

void IClientStatistic::Clear()
{
    // XXX: Ugly, ugly hack (just following the lead of original code). FIX!
    ClientStatisticImpl* const saved_impl = m_pimpl;
    CTimer* const saved_timer = m_pimpl->device_timer;
    ZeroMemory(this, sizeof(*this));
    ZeroMemory(m_pimpl, sizeof(*m_pimpl));
    m_pimpl = saved_impl;
    m_pimpl->device_timer = saved_timer;
    m_pimpl->dwBaseTime = TimeGlobal(m_pimpl->device_timer);
}

//------------------------------------------------------------------------------

void IClient::_SendTo_LL(const void* data, u32 size, u32 flags, u32 timeout)
{
    R_ASSERT(server);
    server->IPureServer::SendTo_LL(ID, const_cast<void*>(data), size, flags, timeout);
}

//------------------------------------------------------------------------------
IClient* IPureServer::ID_to_client(ClientID ID, bool ScanAll)
{
    if (0 == ID.value())
        return nullptr;
    IClient* ret_client = GetClientByID(ID);
    if (ret_client || !ScanAll)
        return ret_client;

    return nullptr;
}

void IPureServer::_Recieve(const void* data, u32 data_size, u32 param)
{
    if (data_size >= NET_PacketSizeLimit) {
        Msg("! too large packet size[%d] received, DoS attack?", data_size);
        return;
    }

    NET_Packet packet;
    ClientID id;

    id.set(param);
    packet.construct(data, data_size);
    // DWORD currentThreadId = GetCurrentThreadId();
    // Msg("-S- Entering to csMessages from _Receive [%d]", currentThreadId);
    csMessage.Enter();
    // LogStackTrace(
    //		make_string("-S- Entered to csMessages [%d]", currentThreadId).c_str());
    //---------------------------------------
    if (psNET_Flags.test(NETFLAG_LOG_SV_PACKETS)) {
        if (!pSvNetLog)
            pSvNetLog = xr_new<INetLog>("logs" DELIMITER "net_sv_log.log", TimeGlobal(device_timer));

        if (pSvNetLog)
            pSvNetLog->LogPacket(TimeGlobal(device_timer), &packet, true);
    }
    //---------------------------------------
    u32 result = OnMessage(packet, id);
    // Msg("-S- Leaving from csMessages [%d]", currentThreadId);
    csMessage.Leave();

    if (result)
        SendBroadcast(id, packet, result);
}

//==============================================================================
#ifdef CONFIG_PROFILE_LOCKS
IPureServer::IPureServer(CTimer* timer, bool Dedicated)
    : m_bDedicated(Dedicated)
    , csPlayers(MUTEX_PROFILE_ID(IPureServer::csPlayers))
    , csMessage(MUTEX_PROFILE_ID(csMessage))
#else
IPureServer::IPureServer(CTimer* timer, bool Dedicated)
    : m_bDedicated(Dedicated)
#endif
{
    device_timer = timer;
    stats.clear();
    stats.dwSendTime = TimeGlobal(device_timer);
    SV_Client = nullptr;
    pSvNetLog = nullptr; // new INetLog("logs//net_sv_log.log", TimeGlobal(device_timer));
#ifdef DEBUG
    sender_functor_invoked = false;
#endif
}

IPureServer::~IPureServer()
{
    for (u32 it = 0; it < BannedAddresses.size(); it++)
        xr_delete(BannedAddresses[it]);

    BannedAddresses.clear();

    SV_Client = nullptr;

    xr_delete(pSvNetLog);

    psNET_direct_connect = false;
}

IPureServer::EConnect IPureServer::Connect(pcstr options, GameDescriptionData& game_descr)
{
    connect_options = options;
    psNET_direct_connect = (std::strstr(options, "/single") != nullptr) &&
        (std::strstr(options, "/coop") == nullptr);

#if defined(__ANDROID__)
    m_androidLanGameDescription = game_descr;
#else
    // The non-Android fallback keeps the original disabled networking behavior.
#endif

    // Parse options
    string4096 session_name;
    xr_strcpy(session_name, options);
    if (std::strchr(session_name, '/'))
        *std::strchr(session_name, '/') = 0;

    u32 dwMaxPlayers = 2;
    if (std::strstr(options, "maxplayers="))
    {
        const char* value = std::strstr(options, "maxplayers=") + 11;
        char buffer[64]{};
        const char* endValue = std::strchr(value, '/');
        const std::size_t length = std::min<std::size_t>(
            endValue ? static_cast<std::size_t>(endValue - value) : std::strlen(value), sizeof(buffer) - 1);
        std::memcpy(buffer, value, length);
        dwMaxPlayers = static_cast<u32>(std::strtoul(buffer, nullptr, 10));
    }
    if (dwMaxPlayers < 1 || dwMaxPlayers > 32)
        dwMaxPlayers = 2;

#if defined(__ANDROID__)
    m_androidLanMaxPlayers = dwMaxPlayers;
    m_androidLanGameFlags = 0;
    if (std::strstr(options, "/single"))
        m_androidLanGameFlags |= AndroidLan::GameFlagStory;
    if (std::strstr(options, "/alife"))
        m_androidLanGameFlags |= AndroidLan::GameFlagAlife;
    m_androidLanPassword[0] = 0;
    if (std::strstr(options, "psw="))
    {
        const char* value = std::strstr(options, "psw=") + 4;
        const char* endValue = std::strchr(value, '/');
        const std::size_t length = std::min<std::size_t>(
            endValue ? static_cast<std::size_t>(endValue - value) : std::strlen(value),
            sizeof(m_androidLanPassword) - 1);
        std::memcpy(m_androidLanPassword, value, length);
    }
#endif
#ifdef DEBUG
    Msg("MaxPlayers = %d", dwMaxPlayers);
#endif

    u32 dwServerPort = START_PORT_LAN_SV;
    bool portWasSet = false;
    if (std::strstr(options, "portsv="))
    {
        const char* value = std::strstr(options, "portsv=") + 7;
        char buffer[64]{};
        const char* endValue = std::strchr(value, '/');
        const std::size_t length = std::min<std::size_t>(
            endValue ? static_cast<std::size_t>(endValue - value) : std::strlen(value), sizeof(buffer) - 1);
        std::memcpy(buffer, value, length);
        const unsigned long parsedPort = std::strtoul(buffer, nullptr, 10);
        if (parsedPort == 0 || parsedPort > 65535)
            return ErrConnect;
        dwServerPort = static_cast<u32>(parsedPort);
        portWasSet = true;
    }

#if defined(__ANDROID__)
    if (!psNET_direct_connect)
    {
        bool listening = false;
        const u32 lastPort = portWasSet ? dwServerPort : END_PORT_LAN;
        for (u32 port = dwServerPort; port <= lastPort; ++port)
        {
            std::string error;
            if (m_androidLanListener.ListenIpv4(static_cast<std::uint16_t>(port), 4, &error))
            {
                psNET_Port = static_cast<int>(port);
                listening = true;
                Msg("* Android LAN server listening on TCP port %u", port);
                break;
            }

            Msg("! Android LAN: could not bind TCP port %u: %s", port, error.c_str());
            if (portWasSet)
                break;
        }

        if (!listening)
            return ErrConnect;

        m_androidLanRunning.store(true);
        m_androidLanAcceptThread = std::thread(&IPureServer::AndroidLanAcceptLoop, this);
    }
    else
    {
        psNET_Port = static_cast<int>(dwServerPort);
    }
#else
    if (!psNET_direct_connect)
    {
        // The legacy non-Android empty backend intentionally has no transport.
        HRESULT HostSuccess = S_FALSE;
        psNET_Port = static_cast<int>(dwServerPort);
        while (HostSuccess != S_OK)
        {
            if (portWasSet)
                return ErrConnect;
            ++psNET_Port;
            if (psNET_Port > END_PORT_LAN)
                return ErrConnect;
        }
    }
#endif

    if (!psNET_direct_connect)
    {
        BannedList_Load();
        IpList_Load();
    }

    return ErrNoError;
}

void IPureServer::Disconnect()
{
#if defined(__ANDROID__)
    m_androidLanRunning.store(false);
    m_androidLanListener.Close();

    {
        std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
        for (auto& entry : m_androidLanPeers)
        {
            if (entry.second.socket)
                entry.second.socket->Shutdown();
        }
    }

    if (m_androidLanAcceptThread.joinable() &&
        m_androidLanAcceptThread.get_id() != std::this_thread::get_id())
        m_androidLanAcceptThread.join();

    for (std::thread& thread : m_androidLanClientThreads)
    {
        if (thread.joinable() && thread.get_id() != std::this_thread::get_id())
            thread.join();
    }
    m_androidLanClientThreads.clear();

    {
        std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
        for (auto& entry : m_androidLanPeers)
        {
            if (entry.second.socket)
                entry.second.socket->Close();
        }
        m_androidLanPeers.clear();
    }
#endif

    if (!psNET_direct_connect)
    {
        BannedList_Save();
        IpList_Unload();
    }
}

#if defined(__ANDROID__)
void IPureServer::AndroidLanAcceptLoop()
{
    while (m_androidLanRunning.load())
    {
        AndroidLan::TcpSocket accepted;
        std::string peerAddress;
        std::uint16_t peerPort = 0;
        std::string error;

        if (!m_androidLanListener.Accept(accepted, &peerAddress, &peerPort, &error))
        {
            if (m_androidLanRunning.load() && !error.empty())
                Msg("! Android LAN: accept failed: %s", error.c_str());
            continue;
        }

        if (!accepted.SetReceiveTimeoutMs(5000, &error))
        {
            Msg("! Android LAN: could not set handshake timeout: %s", error.c_str());
            continue;
        }

        std::vector<std::uint8_t> helloBytes;
        if (!accepted.ReceiveFrame(helloBytes, sizeof(AndroidLan::ClientHello), &error) ||
            helloBytes.size() != sizeof(AndroidLan::ClientHello))
        {
            if (!error.empty())
                Msg("! Android LAN: invalid client hello: %s", error.c_str());
            continue;
        }

        AndroidLan::ClientHello hello{};
        std::memcpy(&hello, helloBytes.data(), sizeof(hello));
        hello.playerName[sizeof(hello.playerName) - 1] = 0;
        hello.playerPassword[sizeof(hello.playerPassword) - 1] = 0;
        hello.serverPassword[sizeof(hello.serverPassword) - 1] = 0;

        if (hello.magic != AndroidLan::ProtocolMagic || hello.version != AndroidLan::ProtocolVersion)
        {
            AndroidLan::ServerHello rejection{};
            rejection.magic = AndroidLan::ProtocolMagic;
            rejection.version = AndroidLan::ProtocolVersion;
            rejection.status = 1;
            accepted.SendFrame(&rejection, sizeof(rejection), nullptr);
            Msg("! Android LAN: rejected client with unsupported protocol");
            continue;
        }

        if (!m_androidLanRunning.load())
            break;

        if (m_androidLanPassword[0] && std::strcmp(hello.serverPassword, m_androidLanPassword) != 0)
        {
            AndroidLan::ServerHello rejection{};
            rejection.magic = AndroidLan::ProtocolMagic;
            rejection.version = AndroidLan::ProtocolVersion;
            rejection.status = 2;
            accepted.SendFrame(&rejection, sizeof(rejection), nullptr);
            Msg("! Android LAN: rejected client with incorrect server password");
            continue;
        }

        if (GetClientsCount() >= m_androidLanMaxPlayers)
        {
            AndroidLan::ServerHello rejection{};
            rejection.magic = AndroidLan::ProtocolMagic;
            rejection.version = AndroidLan::ProtocolVersion;
            rejection.status = 3;
            accepted.SendFrame(&rejection, sizeof(rejection), nullptr);
            Msg("! Android LAN: rejected client because the session is full");
            continue;
        }

        const u32 clientIdValue = m_androidLanNextClientId.fetch_add(1);
        if (clientIdValue < 2 || clientIdValue == 0xffffffff)
        {
            Msg("! Android LAN: client ID space exhausted");
            continue;
        }

        ClientID clientId;
        clientId.set(clientIdValue);
        std::shared_ptr<AndroidLan::TcpSocket> socket =
            std::make_shared<AndroidLan::TcpSocket>(std::move(accepted));

        AndroidLanPeer peer;
        peer.socket = socket;
        peer.address.set(peerAddress.c_str());
        peer.port = peerPort;

        {
            std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
            m_androidLanPeers[clientIdValue] = peer;
        }

        AndroidLan::ServerHello reply{};
        reply.magic = AndroidLan::ProtocolMagic;
        reply.version = AndroidLan::ProtocolVersion;
        reply.status = 0;
        reply.clientId = clientIdValue;
        reply.gameFlags = m_androidLanGameFlags;
        reply.game = m_androidLanGameDescription;

        if (!socket->SendFrame(&reply, sizeof(reply), &error))
        {
            Msg("! Android LAN: server hello send failed: %s", error.c_str());
            std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
            m_androidLanPeers.erase(clientIdValue);
            continue;
        }

        if (!socket->SetReceiveTimeoutMs(0, &error))
        {
            Msg("! Android LAN: could not clear handshake timeout: %s", error.c_str());
            socket->Shutdown();
            std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
            m_androidLanPeers.erase(clientIdValue);
            continue;
        }

        SClientConnectData clientData;
        clientData.clientID = clientId;
        std::snprintf(clientData.name, sizeof(clientData.name), "%s",
            hello.playerName[0] ? hello.playerName : "Player");
        std::snprintf(clientData.pass, sizeof(clientData.pass), "%s", hello.playerPassword);
        clientData.process_id = peerAddress == "127.0.0.1" ? GetCurrentProcessId() : 0;

        IClient* client = new_client(&clientData);
        if (!client)
        {
            socket->Shutdown();
            std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
            m_androidLanPeers.erase(clientIdValue);
            continue;
        }

        Msg("* Android LAN: client %u connected from %s:%u", clientIdValue, peerAddress.c_str(), peerPort);
        m_androidLanClientThreads.emplace_back(
            &IPureServer::AndroidLanClientLoop, this, clientId, std::move(socket));
    }
}

void IPureServer::AndroidLanClientLoop(ClientID id, std::shared_ptr<AndroidLan::TcpSocket> socket)
{
    while (m_androidLanRunning.load() && socket && socket->IsOpen())
    {
        std::vector<std::uint8_t> frame;
        std::string error;
        if (!socket->ReceiveFrame(frame, AndroidLan::TcpSocket::DefaultMaxFrameSize, &error))
        {
            if (m_androidLanRunning.load() && !error.empty())
                Msg("! Android LAN: client %u disconnected: %s", id.value(), error.c_str());
            break;
        }

        if (!frame.empty())
            RecievePacket(frame.data(), static_cast<u32>(frame.size()), id.value());
    }

    if (m_androidLanRunning.load())
    {
        {
            std::lock_guard<std::mutex> guard(m_androidLanDisconnectMutex);
            m_androidLanPendingDisconnects.push_back(id);
        }

        std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
        m_androidLanPeers.erase(id.value());
    }
}

void IPureServer::AndroidLanProcessDisconnects()
{
    std::vector<ClientID> pending;
    {
        std::lock_guard<std::mutex> guard(m_androidLanDisconnectMutex);
        pending.swap(m_androidLanPendingDisconnects);
    }

    for (const ClientID& id : pending)
    {
        csMessage.Enter();
        IClient* client = GetClientByID(id);
        if (client)
        {
            client->flags.bConnected = FALSE;
            client->flags.bReconnect = FALSE;
            OnCL_Disconnected(client);
            client_Destroy(client);
        }
        csMessage.Leave();
    }
}

#endif


HRESULT IPureServer::net_Handler(u32 dwMessageType, PVOID pMessage)
{
    return S_OK;
}

void IPureServer::Flush_Clients_Buffers()
{
#if NET_LOG_PACKETS
    Msg("# flush server send-buf");
#endif

    struct LocalSenderFunctor {
        static void FlushBuffer(IClient* client) { client->FlushSendBuffer(0); }
    };

    net_players.ForEachClientDo(LocalSenderFunctor::FlushBuffer);
}

void IPureServer::SendTo_Buf(ClientID id, void* data, u32 size, u32 dwFlags, u32 dwTimeout)
{
    IClient* tmp_client = net_players.GetFoundClient(ClientIdSearchPredicate(id));
    VERIFY(tmp_client);
    tmp_client->SendPacket(data, size, dwFlags, dwTimeout);
}

void IPureServer::SendTo_LL(ClientID ID /*DPNID ID*/, void* data, u32 size, u32 dwFlags, u32 dwTimeout)
{
    if (psNET_Flags.test(NETFLAG_LOG_SV_PACKETS))
    {
        if (!pSvNetLog)
            pSvNetLog = xr_new<INetLog>("logs" DELIMITER "net_sv_log.log", TimeGlobal(device_timer));
        if (pSvNetLog)
            pSvNetLog->LogData(TimeGlobal(device_timer), data, size);
    }

#if defined(__ANDROID__)
    std::shared_ptr<AndroidLan::TcpSocket> socket;
    {
        std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
        const auto it = m_androidLanPeers.find(ID.value());
        if (it != m_androidLanPeers.end())
            socket = it->second.socket;
    }

    if (!socket)
    {
        Msg("! Android LAN: no TCP route for client 0x%08x", ID.value());
        return;
    }

    std::string error;
    if (!socket->SendFrame(data, size, &error))
        Msg("! Android LAN: send to client 0x%08x failed: %s", ID.value(), error.c_str());
#else
    // No transport is available in the non-Android empty backend.
#endif
}

void IPureServer::SendTo(ClientID ID /*DPNID ID*/, NET_Packet& P, u32 dwFlags, u32 dwTimeout)
{
    SendTo_LL(ID, P.B.data, P.B.count, dwFlags, dwTimeout);
}

void IPureServer::SendBroadcast_LL(ClientID exclude, void* data, u32 size, u32 dwFlags)
{
    struct ClientExcluderPredicate {
        ClientID id_to_exclude;
        ClientExcluderPredicate(ClientID exclude)
            : id_to_exclude(exclude)
        {
        }

        bool operator()(IClient* client)
        {
            if (client->ID == id_to_exclude)
                return false;
            if (!client->flags.bConnected)
                return false;
            return true;
        }
    };

    struct ClientSenderFunctor {
        IPureServer* m_owner;
        void* m_data;
        u32 m_size;
        u32 m_dwFlags;

        ClientSenderFunctor(IPureServer* owner, void* data, u32 size, u32 dwFlags)
            : m_owner(owner)
            , m_data(data)
            , m_size(size)
            , m_dwFlags(dwFlags)
        {
        }

        void operator()(IClient* client) { m_owner->SendTo_LL(client->ID, m_data, m_size, m_dwFlags); }
    };

    ClientSenderFunctor temp_functor(this, data, size, dwFlags);
    net_players.ForFoundClientsDo(ClientExcluderPredicate(exclude), temp_functor);
}

void IPureServer::SendBroadcast(ClientID exclude, NET_Packet& P, u32 dwFlags)
{
    // Perform broadcasting
    SendBroadcast_LL(exclude, P.B.data, P.B.count, dwFlags);
}

u32 IPureServer::OnMessage(NET_Packet& P, ClientID sender) // Non-Zero means broadcasting with "flags" as returned
{
    /*
    u16 m_type;
    P.r_begin(m_type);
    switch (m_type)
    {
    case M_CHAT:
    {
        char buffer[256];
        P.r_string(buffer);
        printf("RECEIVE: %s\n", buffer);
    }
        break;
    }
    */

    return 0;
}

void IPureServer::OnCL_Connected(IClient* CL) { Msg("* Player 0x%08x connected.\n", CL->ID.value()); }
void IPureServer::OnCL_Disconnected(IClient* CL) { Msg("* Player 0x%08x disconnected.\n", CL->ID.value()); }

bool IPureServer::HasBandwidth(IClient* C)
{
    u32 dwTime = TimeGlobal(device_timer);
    u32 dwInterval = 0;

    if (psNET_direct_connect) {
        UpdateClientStatistic(C);
        C->dwTime_LastUpdate = dwTime;
        dwInterval = 1000;
        return true;
    }

    if (psNET_ServerUpdate != 0)
        dwInterval = 1000 / psNET_ServerUpdate;
    if (psNET_Flags.test(NETFLAG_MINIMIZEUPDATES))
        dwInterval = 1000; // approx 2 times per second

    HRESULT hr;
    if (psNET_ServerUpdate != 0 && (dwTime - C->dwTime_LastUpdate) > dwInterval) {
        // check queue for "empty" state
        u32 dwPending;
        if (FAILED(hr))
            return false;

        if (dwPending > u32(psNET_ServerPending)) {
            C->stats.dwTimesBlocked++;
            return false;
        }

        UpdateClientStatistic(C);
        // ok
        C->dwTime_LastUpdate = dwTime;
        return true;
    }
    return false;
}

void IPureServer::UpdateClientStatistic(IClient* C)
{
    // Query network statistic for this client
}

void IPureServer::ClearStatistic()
{
    stats.clear();
    struct StatsClearFunctor {
        static void Clear(IClient* client) { client->stats.Clear(); }
    };
    net_players.ForEachClientDo(StatsClearFunctor::Clear);
}

/*
bool IPureServer::DisconnectClient(IClient* C)
{
    if (!C) return false;

    string64 Reason = "st_kicked_by_server";
    HRESULT res = NET->DestroyClient(C->ID.value(), Reason, xr_strlen(Reason) + 1, 0);
    CHK_DX(res);
    return true;
}
*/

bool IPureServer::DisconnectClient(IClient* C, pcstr Reason)
{
    if (!C)
        return false;

#if defined(__ANDROID__)
    std::shared_ptr<AndroidLan::TcpSocket> socket;
    {
        std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
        const auto it = m_androidLanPeers.find(C->ID.value());
        if (it != m_androidLanPeers.end())
            socket = it->second.socket;
    }

    if (socket)
    {
        socket->Shutdown();
        return true;
    }
#endif

    return false;
}

bool IPureServer::DisconnectAddress(const ip_address& Address, pcstr reason)
{
    u32 players_count = net_players.ClientsCount();
    buffer_vector<IClient*> PlayersToDisconnect(xr_alloca(players_count * sizeof(IClient*)), players_count);

    struct ToDisconnectFillerFunctor {
        IPureServer* m_owner;
        buffer_vector<IClient*>* dest;
        ip_address const* address_to_disconnect;

        ToDisconnectFillerFunctor(
            IPureServer* owner, buffer_vector<IClient*>* dest_disconnect, ip_address const* address)
            : m_owner(owner)
            , dest(dest_disconnect)
            , address_to_disconnect(address)
        {
        }

        void operator()(IClient* client)
        {
            ip_address tmp_address;
            m_owner->GetClientAddress(client->ID, tmp_address);
            if (*address_to_disconnect == tmp_address)
                dest->push_back(client);
        }
    };

    ToDisconnectFillerFunctor tmp_functor(this, &PlayersToDisconnect, &Address);
    net_players.ForEachClientDo(tmp_functor);

    buffer_vector<IClient*>::iterator it = PlayersToDisconnect.begin();
    buffer_vector<IClient*>::iterator it_e = PlayersToDisconnect.end();

    for (; it != it_e; ++it) {
        DisconnectClient(*it, reason);
    }
    return true;
}

bool IPureServer::GetClientAddress(ClientID ID, ip_address& Address, u32* pPort)
{
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> guard(m_androidLanPeersMutex);
    const auto it = m_androidLanPeers.find(ID.value());
    if (it == m_androidLanPeers.end())
        return false;

    Address = it->second.address;
    if (pPort)
        *pPort = it->second.port;
    return true;
#else
    return true;
#endif
}

IBannedClient* IPureServer::GetBannedClient(const ip_address& Address)
{
    for (u32 it = 0; it < BannedAddresses.size(); it++) {
        IBannedClient* pBClient = BannedAddresses[it];
        if (pBClient->HAddr == Address)
            return pBClient;
    }
    return nullptr;
}

void IPureServer::BanClient(IClient* C, u32 BanTime)
{
    ip_address ClAddress;
    GetClientAddress(C->ID, ClAddress);
    BanAddress(ClAddress, BanTime);
}

void IPureServer::BanAddress(const ip_address& Address, u32 BanTimeSec)
{
    if (GetBannedClient(Address)) {
        Msg("Already banned\n");
        return;
    }

    IBannedClient* pNewClient = xr_new<IBannedClient>();
    pNewClient->HAddr = Address;
    time(&pNewClient->BanTime);
    pNewClient->BanTime += BanTimeSec;
    if (pNewClient) {
        BannedAddresses.push_back(pNewClient);
        BannedList_Save();
    }
}

void IPureServer::UnBanAddress(const ip_address& Address)
{
    if (!GetBannedClient(Address)) {
        Msg("! Can't find address %s in ban list.", Address.to_string().c_str());
        return;
    }

    for (u32 it = 0; it < BannedAddresses.size(); it++) {
        IBannedClient* pBClient = BannedAddresses[it];
        if (pBClient->HAddr == Address) {
            xr_delete(BannedAddresses[it]);
            BannedAddresses.erase(BannedAddresses.begin() + it);
            Msg("Unbanning %s", Address.to_string().c_str());
            BannedList_Save();
            break;
        }
    }
}

void IPureServer::Print_Banned_Addreses()
{
    Msg("- ----banned ip list begin-------");
    for (u32 i = 0; i < BannedAddresses.size(); i++) {
        IBannedClient* pBClient = BannedAddresses[i];
        Msg("- %s to %s", pBClient->HAddr.to_string().c_str(), pBClient->BannedTimeTo().c_str());
    }
    Msg("- ----banned ip list end-------");
}

void IPureServer::BannedList_Save()
{
    string_path temp;
    FS.update_path(temp, "$app_data_root$", GetBannedListName());

    CInifile ini(temp, false, false, true);

    for (u32 it = 0; it < BannedAddresses.size(); it++) {
        IBannedClient* cl = BannedAddresses[it];
        cl->Save(ini);
    }
}

void IPureServer::BannedList_Load()
{
    string_path temp;
    FS.update_path(temp, "$app_data_root$", GetBannedListName());

    CInifile ini(temp);

    auto it = ini.sections().begin();
    auto it_e = ini.sections().end();

    for (; it != it_e; ++it) {
        const shared_str& sect_name = (*it)->Name;
        IBannedClient* Cl = xr_new<IBannedClient>();
        Cl->Load(ini, sect_name);
        BannedAddresses.push_back(Cl);
    }
}

void IPureServer::IpList_Load()
{
    Msg("* Initializing IP filter.");
    m_ip_filter.load();
}

void IPureServer::IpList_Unload()
{
    Msg("* Deinitializing IP filter.");
    m_ip_filter.unload();
}

bool IPureServer::IsPlayerIPDenied(u32 ip_address) { return !m_ip_filter.is_ip_present(ip_address); }
bool banned_client_comparer(IBannedClient* C1, IBannedClient* C2) { return C1->BanTime > C2->BanTime; }

void IPureServer::UpdateBannedList()
{
    if (!BannedAddresses.size())
        return;
    sort(BannedAddresses.begin(), BannedAddresses.end(), banned_client_comparer);
    time_t T;
    time(&T);

    IBannedClient* Cl = BannedAddresses.back();
    if (Cl->BanTime < T) {
        ip_address Address = Cl->HAddr;
        UnBanAddress(Address);
    }
}

constexpr pcstr IPureServer::GetBannedListName() { return "banned_list_ip.ltx"; }
