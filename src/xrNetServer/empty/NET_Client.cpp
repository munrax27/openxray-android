#include "NET_Client.h"
#include "NET_Common.h"
#include "NET_Log.h"
#include "NET_Messages.h"
#if defined(__ANDROID__)
#include "../android/AndroidLanProtocol.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#endif
#include "NET_Server.h"
#include "stdafx.h"
#include "xrCore/Threading/Lock.hpp"

#include "xrGameSpy/xrGameSpy_MainDefs.h"

static INetLog* pClNetLog = nullptr;

#ifdef CONFIG_PROFILE_LOCKS
INetQueue::INetQueue()
    : pcs(xr_new<Lock>(MUTEX_PROFILE_ID(INetQueue)))
#else
INetQueue::INetQueue()
    : pcs(xr_new<Lock>())
#endif
{
    unused.reserve(128);
    for (int i = 0; i < 16; i++)
        unused.push_back(xr_new<NET_Packet>());
}

INetQueue::~INetQueue()
{
    pcs->Enter();
    u32 it;
    for (it = 0; it < unused.size(); it++)
        xr_delete(unused[it]);
    for (it = 0; it < ready.size(); it++)
        xr_delete(ready[it]);
    pcs->Leave();
    xr_delete(pcs);
}

//static u32 LastTimeCreate = 0;

NET_Packet* INetQueue::Create()
{
    NET_Packet* P = nullptr;

    if (unused.empty()) {
        ready.push_back(xr_new<NET_Packet>());
        P = ready.back();
        //---------------------------------------------
        // LastTimeCreate = SDL_GetTicks();
        //---------------------------------------------
    } else {
        ready.push_back(unused.back());
        unused.pop_back();
        P = ready.back();
    }
    // pcs->Leave();
    return P;
}

NET_Packet* INetQueue::Create(const NET_Packet& _other)
{
    NET_Packet* P = nullptr;
    pcs->Enter();
    if (unused.empty()) {
        ready.push_back(xr_new<NET_Packet>());
        P = ready.back();
        //---------------------------------------------
        //        LastTimeCreate = SDL_GetTicks();
        //---------------------------------------------
    } else {
        ready.push_back(unused.back());
        unused.pop_back();
        P = ready.back();
    }
    CopyMemory(P, &_other, sizeof(NET_Packet));
    pcs->Leave();
    return P;
}

NET_Packet* INetQueue::Retreive()
{
    NET_Packet* P = nullptr;

    if (!ready.empty())
        P = ready.front();
    //---------------------------------------------
    else {
        /*
    u32 tmp_time = SDL_GetTicks() - 60000;
    u32 size = unused.size();
    if ((LastTimeCreate < tmp_time) && (size > 32))
    {
        xr_delete(unused.back());
        unused.pop_back();
    }
    */
    }

    return P;
}

void INetQueue::Release()
{
    VERIFY(!ready.empty());
    //---------------------------------------------
    // u32 tmp_time = SDL_GetTicks() - 60000;
    //u32 size = unused.size();
    ready.front()->B.count = 0;
    /*
   * if ((LastTimeCreate < tmp_time) && (size > 32))
  {
      xr_delete(ready.front());
  }
  else
      unused.push_back(ready.front());
      */

    ready.pop_front();
}

void INetQueue::LockQ()
{
    pcs->Enter();
}

void INetQueue::UnlockQ()
{
    pcs->Leave();
}

const u32 syncQueueSize = 512;
//const int syncSamples = 256;

class XRNETSERVER_API syncQueue {
    u32 table[syncQueueSize];
    u32 write;
    u32 count;

public:
    syncQueue() { clear(); }

    void push(u32 value)
    {
        table[write++] = value;
        if (write == syncQueueSize)
            write = 0;

        if (count <= syncQueueSize)
            count++;
    }

    u32* begin() { return table; }
    u32* end() { return table + count; }
    u32 size() const { return count; }

    void clear()
    {
        write = 0;
        count = 0;
    }
} net_DeltaArray;

//-------
XRNETSERVER_API Flags32 psNET_Flags = { 0 };
XRNETSERVER_API int psNET_ClientUpdate = 30; // FPS
XRNETSERVER_API int psNET_ClientPending = 2;
XRNETSERVER_API char psNET_Name[32] = "Player";
XRNETSERVER_API bool psNET_direct_connect = false;

/****************************************************************************
 *
 * DirectPlay8 Service Provider GUIDs
 *
 ****************************************************************************/

static HRESULT WINAPI Handler(PVOID pvUserContext,
    DWORD dwMessageType,
    PVOID pMessage)
{
    IPureClient* C = (IPureClient*)pvUserContext;
    return C->net_Handler(dwMessageType, pMessage);
}

//------------------------------------------------------------------------------

void IPureClient::_SendTo_LL(const void* data,
    u32 size,
    u32 flags,
    u32 timeout)
{
    IPureClient::SendTo_LL(const_cast<void*>(data), size, flags, timeout);
}

//------------------------------------------------------------------------------

void IPureClient::_Recieve(const void* data, u32 data_size, u32 /*param*/)
{
    MSYS_PING* cfg = (MSYS_PING*)data;
    net_Statistic.dwBytesReceived += data_size;

    if ((data_size >= 2 * sizeof(u32)) && (cfg->sign1 == 0x12071980) && (cfg->sign2 == 0x26111975)) {
        // Internal system message
        if (data_size == sizeof(MSYS_PING)) {
            // It is reverted(server) ping
            u32 time = TimerAsync(device_timer);
            u32 ping = time - (cfg->dwTime_ClientSend);
            u32 delta = cfg->dwTime_Server + ping / 2 - time;

            net_DeltaArray.push(delta);
            Sync_Average();
            return;
        }

        if (data_size == sizeof(MSYS_CONFIG)) {
            net_Connected = EnmConnectionCompleted;
            return;
        }
        Msg("! Unknown system message");
    }
    if (net_Connected == EnmConnectionCompleted) {
        // one of the messages - decompress it

        if (psNET_Flags.test(NETFLAG_LOG_CL_PACKETS)) {
            if (!pClNetLog)
                pClNetLog = xr_new<INetLog>("logs//net_cl_log.log", timeServer());

            if (pClNetLog)
                pClNetLog->LogData(timeServer(), const_cast<void*>(data), data_size,
                    true);
        }

        OnMessage(const_cast<void*>(data), data_size);
    }
}

//==============================================================================
#ifdef CONFIG_PROFILE_LOCKS
IPureClient::IPureClient(CTimer* timer)
    : net_Statistic(timer)
    , net_csEnumeration(
          xr_new<Lock>(MUTEX_PROFILE_ID(IPureClient::net_csEnumeration)))
#else
IPureClient::IPureClient(CTimer* timer)
    : net_Statistic(timer)
    , net_csEnumeration(xr_new<Lock>())
#endif
{
    device_timer = timer;
    net_TimeDelta_User = 0;
    net_Time_LastUpdate = 0;
    net_TimeDelta = 0;
    net_TimeDelta_Calculated = 0;

    pClNetLog = nullptr; // new INetLog("logs//net_cl_log.log", timeServer());
}

IPureClient::~IPureClient()
{
#if defined(__ANDROID__)
    Disconnect();
#endif
    xr_delete(pClNetLog);
    pClNetLog = nullptr;
    psNET_direct_connect = false;
    xr_delete(net_csEnumeration);
}

bool IPureClient::Connect(pcstr options)
{
#if defined(__ANDROID__)
    R_ASSERT(options);
    Disconnect();

    char serverName[256]{};
    const char* slash = std::strchr(options, '/');
    const std::size_t hostLength = slash ? static_cast<std::size_t>(slash - options) : std::strlen(options);
    if (hostLength == 0 || hostLength >= sizeof(serverName))
    {
        Msg("! Android LAN: invalid server address in options");
        return false;
    }
    std::memcpy(serverName, options, hostLength);

    char playerName[64]{};
    std::snprintf(playerName, sizeof(playerName), "%s", psNET_Name);

    char password[64]{};
    const char* passValue = std::strstr(options, "pass=");
    if (passValue)
    {
        passValue += 5;
        const char* endValue = std::strchr(passValue, '/');
        const std::size_t length = std::min<std::size_t>(endValue ? endValue - passValue : std::strlen(passValue), sizeof(password) - 1);
        std::memcpy(password, passValue, length);
    }

    u32 port = START_PORT_LAN_SV;
    const char* portValue = std::strstr(options, "port=");
    if (portValue)
    {
        portValue += 5;
        const unsigned long parsedPort = std::strtoul(portValue, nullptr, 10);
        if (parsedPort == 0 || parsedPort > 65535)
        {
            Msg("! Android LAN: invalid server port");
            return false;
        }
        port = static_cast<u32>(parsedPort);
    }

    if (xr_stricmp(serverName, "localhost") == 0)
        std::snprintf(serverName, sizeof(serverName), "%s", "127.0.0.1");

    std::string error;
    if (!m_androidLanSocket.ConnectIpv4(serverName, static_cast<std::uint16_t>(port), &error))
    {
        Msg("! Android LAN: connection to %s:%u failed: %s", serverName, port, error.c_str());
        net_Connected = EnmConnectionFails;
        net_Disconnected = true;
        OnInvalidHost();
        return false;
    }

    AndroidLan::ClientHello hello{};
    hello.magic = AndroidLan::ProtocolMagic;
    hello.version = AndroidLan::ProtocolVersion;
    std::snprintf(hello.playerName, sizeof(hello.playerName), "%s", playerName);
    std::snprintf(hello.password, sizeof(hello.password), "%s", password);

    if (!m_androidLanSocket.SendFrame(&hello, sizeof(hello), &error))
    {
        Msg("! Android LAN: hello send failed: %s", error.c_str());
        m_androidLanSocket.Close();
        net_Connected = EnmConnectionFails;
        net_Disconnected = true;
        OnInvalidHost();
        return false;
    }

    std::vector<std::uint8_t> replyBytes;
    if (!m_androidLanSocket.ReceiveFrame(replyBytes, sizeof(AndroidLan::ServerHello), &error) ||
        replyBytes.size() != sizeof(AndroidLan::ServerHello))
    {
        Msg("! Android LAN: invalid server handshake: %s", error.c_str());
        m_androidLanSocket.Close();
        net_Connected = EnmConnectionFails;
        net_Disconnected = true;
        OnInvalidHost();
        return false;
    }

    AndroidLan::ServerHello reply{};
    std::memcpy(&reply, replyBytes.data(), sizeof(reply));
    if (reply.magic != AndroidLan::ProtocolMagic || reply.version != AndroidLan::ProtocolVersion || reply.status != 0 || reply.clientId == 0)
    {
        Msg("! Android LAN: server rejected or sent an unsupported handshake");
        m_androidLanSocket.Close();
        net_Connected = EnmConnectionFails;
        net_Disconnected = true;
        OnInvalidHost();
        return false;
    }

    m_game_description = reply.game;
    ClientID localClientId;
    localClientId.set(reply.clientId);
    SetClientID(localClientId);
    m_androidLanServerAddress = serverName;
    m_androidLanServerPort = port;

    net_csEnumeration->Enter();
    net_Hosts.clear();
    HOST_NODE node;
    node.dpSessionName = serverName;
    net_Hosts.push_back(std::move(node));
    net_csEnumeration->Leave();

    net_Connected = EnmConnectionWait;
    net_Syncronised = false;
    net_Disconnected = false;
    m_androidLanRunning.store(true);
    m_androidLanReceiveThread = std::thread(&IPureClient::AndroidLanReceiveLoop, this);

    Msg("* Android LAN: connected to %s:%u as client %u", serverName, port, reply.clientId);
    return true;
#else

    R_ASSERT(options);
    net_Disconnected = false;

    if (!psNET_direct_connect) {
        string256 server_name = "";
        // xr_strcpy(server_name,options);
        if (strchr(options, '/'))
            strncpy_s(server_name, options, strchr(options, '/') - options);
        if (strchr(server_name, '/'))
            *strchr(server_name, '/') = 0;

        string64 password_str = "";
        if (strstr(options, "psw=")) {
            const char* PSW = strstr(options, "psw=") + 4;
            if (strchr(PSW, '/'))
                strncpy_s(password_str, PSW, strchr(PSW, '/') - PSW);
            else
                xr_strcpy(password_str, PSW);
        }

        string64 user_name_str = "";
        if (strstr(options, "name=")) {
            const char* NM = strstr(options, "name=") + 5;
            if (strchr(NM, '/'))
                strncpy_s(user_name_str, NM, strchr(NM, '/') - NM);
            else
                xr_strcpy(user_name_str, NM);
        }

        string64 user_pass = "";
        if (strstr(options, "pass=")) {
            const char* UP = strstr(options, "pass=") + 5;
            if (strchr(UP, '/'))
                strncpy_s(user_pass, UP, strchr(UP, '/') - UP);
            else
                xr_strcpy(user_pass, UP);
        }

        int psSV_Port = START_PORT_LAN_SV;
        if (strstr(options, "port=")) {
            string64 portstr;
            xr_strcpy(portstr, strstr(options, "port=") + 5);
            if (strchr(portstr, '/'))
                *strchr(portstr, '/') = 0;
            psSV_Port = atol(portstr);
            clamp(psSV_Port, int(START_PORT), int(END_PORT));
        }

        bool bPortWasSet = false;
        int psCL_Port = START_PORT_LAN_CL;
        if (strstr(options, "portcl=")) {
            string64 portstr;
            xr_strcpy(portstr, strstr(options, "portcl=") + 7);
            if (strchr(portstr, '/'))
                *strchr(portstr, '/') = 0;
            psCL_Port = atol(portstr);
            clamp(psCL_Port, int(START_PORT), int(END_PORT));
            bPortWasSet = true;
        }
        // Msg("* Client connect on port %d\n", psNET_Port);

        //
        net_Connected = EnmConnectionWait;
        net_Syncronised = false;
        net_Disconnected = false;

        //---------------------------

        bool bSimulator = false;
        if (strstr(Core.Params, "-netsim"))
            bSimulator = true;

        // Setup client info
        /*xr_strcpy( tmp, server_name );
    xr_strcat( tmp, "/name=" );
    xr_strcat( tmp, user_name_str );
    xr_strcat( tmp, "/" );*/

        if (xr_stricmp(server_name, "localhost") == 0) {

            u32 c_port = u32(psCL_Port);
            HRESULT res = S_FALSE;
            while (res != S_OK) {
                if (res != S_OK) {
                    //			xr_string res =
                    // xrDebug::ErrorToString(HostSuccess);

                    if (bPortWasSet) {
                        Msg("! IPureClient : port %d is BUSY!", c_port);
                        return false;
                    }
                    Msg("! IPureClient : port %d is BUSY!", c_port);

                    c_port++;
                    if (c_port > END_PORT_LAN) {
                        return false;
                    }
                } else {
                    Msg("- IPureClient : created on port %d!", c_port);
                }
            }

            //		R_CHK(res);
            if (res != S_OK)
                return false;

            // Create ONE node
            HOST_NODE NODE;

            // Retreive session name
            char desc[4096];
            ZeroMemory(desc, sizeof(desc));
            net_Hosts.push_back(NODE);
        } else {
            string64 EnumData;
            EnumData[0] = 0;
            xr_strcat(EnumData, "ToConnect");
            // We now have the host address so lets enum
            u32 c_port = psCL_Port;
            HRESULT res = S_FALSE;
            while (res != S_OK && c_port <= END_PORT) {

                if (res != S_OK) {
                    // xr_string res = xrDebug::ErrorToString(HostSuccess);

                    c_port++;
                } else
                    Msg("- IPureClient : created on port %d!", c_port);
            }

            // ****** Connection
            if (net_Hosts.empty()) {
                OnInvalidHost();
                return false;
            }

            //WCHAR SessionPasswordUNICODE[4096];
            //if (xr_strlen(password_str)) {
            //}

            net_csEnumeration->Enter();
            // real connect
            for (u32 I = 0; I < net_Hosts.size(); I++)
                Msg("* HOST #%d: %s\n", I + 1, net_Hosts[I].dpSessionName.c_str());

            if (res != S_OK)
                return false;
        }

        // Caps
        /*
    GUID			sp_guid;
    DPN_SP_CAPS		sp_caps;

    net_Address_device->GetSP(&sp_guid);
    ZeroMemory		(&sp_caps,sizeof(sp_caps));
    sp_caps.dwSize	= sizeof(sp_caps);
    R_CHK			(NET->GetSPCaps(&sp_guid,&sp_caps,0));
    sp_caps.dwSystemBufferSize	= 0;
    R_CHK			(NET->SetSPCaps(&sp_guid,&sp_caps,0));
    R_CHK			(NET->GetSPCaps(&sp_guid,&sp_caps,0));
    */
    } // psNET_direct_connect
    // Sync
    net_TimeDelta = 0;
    return true;
}
#endif
}

void IPureClient::Disconnect()
{
#if defined(__ANDROID__)
    net_Disconnected = true;
    m_androidLanRunning.store(false);
    m_androidLanSocket.Shutdown();
    if (m_androidLanReceiveThread.joinable() && m_androidLanReceiveThread.get_id() != std::this_thread::get_id())
        m_androidLanReceiveThread.join();
    m_androidLanSocket.Close();

    net_csEnumeration->Enter();
    net_Hosts.clear();
    net_csEnumeration->Leave();

    net_Connected = EnmConnectionWait;
    net_Syncronised = false;
#else

    // Clean up Host _list_
    net_csEnumeration->Enter();

    // XXX: Were we supposed to do something here? Because this code currently does absolutely nothing
    #if 0
    for (u32 i = 0; i < net_Hosts.size(); i++) {
        HOST_NODE& N = net_Hosts[i];
    }
    #endif

    net_Hosts.clear();
    net_csEnumeration->Leave();

    net_Connected = EnmConnectionWait;
    net_Syncronised = false;
}
#endif
}

#if defined(__ANDROID__)
void IPureClient::AndroidLanReceiveLoop()
{
    while (m_androidLanRunning.load())
    {
        std::vector<std::uint8_t> frame;
        std::string error;
        if (!m_androidLanSocket.ReceiveFrame(frame, AndroidLan::TcpSocket::DefaultMaxFrameSize, &error))
        {
            if (m_androidLanRunning.load())
                Msg("! Android LAN: receive stopped: %s", error.c_str());
            break;
        }

        if (!frame.empty())
            RecievePacket(frame.data(), static_cast<u32>(frame.size()));
    }

    if (m_androidLanRunning.exchange(false))
    {
        net_Disconnected = true;
        net_Connected = EnmConnectionFails;
    }
}
#endif

HRESULT IPureClient::net_Handler(u32 dwMessageType, PVOID pMessage)
{
    // HRESULT     hr = S_OK;

    return S_OK;
}

void IPureClient::OnMessage(void* data, u32 size)
{
    // One of the messages - decompress it
    net_Queue.LockQ();
    NET_Packet* P = net_Queue.Create();

    P->construct(data, size);
    P->timeReceive = timeServer_Async(); // TimerAsync(device_timer);

    u16 m_type;
    P->r_begin(m_type);
    net_Queue.UnlockQ();
}

void IPureClient::timeServer_Correct(u32 sv_time, u32 cl_time)
{
    u32 ping = net_Statistic.getPing();
    u32 delta = sv_time + ping / 2 - cl_time;
    net_DeltaArray.push(delta);
    Sync_Average();
}

void IPureClient::SendTo_LL(void* data, u32 size, u32 dwFlags, u32 dwTimeout)
{
    if (net_Disconnected)
        return;

    if (psNET_Flags.test(NETFLAG_LOG_CL_PACKETS))
    {
        if (!pClNetLog)
            pClNetLog = xr_new<INetLog>("logs" DELIMITER "net_cl_log.log", timeServer());
        if (pClNetLog)
            pClNetLog->LogData(timeServer(), data, size);
    }

    net_Statistic.dwBytesSended += size;

#if defined(__ANDROID__)
    std::string error;
    if (!m_androidLanSocket.SendFrame(data, size, &error))
    {
        Msg("! Android LAN: send failed: %s", error.c_str());
        net_Disconnected = true;
        net_Connected = EnmConnectionFails;
    }
#else
    // verify
#endif
}

void IPureClient::Send(NET_Packet& packet, u32 dwFlags, u32 dwTimeout)
{
    SendPacket(packet.B.data, packet.B.count, dwFlags, dwTimeout);
}

void IPureClient::Flush_Send_Buffer()
{
    FlushSendBuffer(0);
}

bool IPureClient::net_HasBandwidth()
{
    u32 dwTime = TimeGlobal(device_timer);
    u32 dwInterval = 0;
    if (net_Disconnected)
        return false;

    if (psNET_ClientUpdate != 0)
        dwInterval = 1000 / psNET_ClientUpdate;
    if (psNET_Flags.test(NETFLAG_MINIMIZEUPDATES))
        dwInterval = 1000; // approx 3 times per second

    if (psNET_direct_connect) {
        if (0 != psNET_ClientUpdate && (dwTime - net_Time_LastUpdate) > dwInterval) {
            net_Time_LastUpdate = dwTime;
            return true;
        }
        return false;
    }
    if (0 != psNET_ClientUpdate && (dwTime - net_Time_LastUpdate) > dwInterval) {
        // check queue for "empty" state
        u32 dwPending = 0;

        if (dwPending > u32(psNET_ClientPending)) {
            net_Statistic.dwTimesBlocked++;
            return false;
        }

        UpdateStatistic();

        // ok
        net_Time_LastUpdate = dwTime;
        return true;
    }
    return false;
}

void IPureClient::UpdateStatistic()
{
    // Query network statistic for this client
}

void IPureClient::Sync_Thread()
{
    //***** Ping server
}

void IPureClient::Sync_Average() {}

void IPureClient::net_Syncronize()
{
#if defined(__ANDROID__)
    // The LAN transport currently relies on the game-state time values sent
    // with normal updates; no DirectPlay clock negotiation is required here.
    net_TimeDelta = 0;
    net_Syncronised = true;
#else
    net_Syncronised = false;
    net_DeltaArray.clear();
    Threading::SpawnThread("network-time-sync", [this]
    {
        Sync_Thread();
    });
#endif
}

bool IPureClient::net_isDisconnected() const
{
    return net_Disconnected;
}

void IPureClient::ClearStatistic()
{
    net_Statistic.Clear();
}
bool IPureClient::net_IsSyncronised()
{
    return net_Syncronised;
}

IPureClient::HOST_NODE::HOST_NODE()
{
}

IPureClient::HOST_NODE::HOST_NODE(const HOST_NODE& rhs)
{
    dpSessionName = rhs.dpSessionName;
}

IPureClient::HOST_NODE::HOST_NODE(HOST_NODE&& rhs) noexcept
{
    dpSessionName.swap(rhs.dpSessionName);
}

IPureClient::HOST_NODE::~HOST_NODE() noexcept
{
}

bool IPureClient::GetServerAddress(ip_address& pAddress, u32* pPort)
{
#if defined(__ANDROID__)
    if (m_androidLanServerAddress.empty())
        return false;
    pAddress.set(m_androidLanServerAddress.c_str());
    if (pPort)
        *pPort = m_androidLanServerPort;
    return true;
#else
    return true;
#endif
};
