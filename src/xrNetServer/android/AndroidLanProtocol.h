#pragma once

#include "../NET_Common.h"
#include "../../../third_party/zone/packet_validation.h"

#include <cstdint>

namespace AndroidLan
{
constexpr std::uint32_t ProtocolMagic = 0x584C414E; // "XLAN"
constexpr std::uint16_t ProtocolVersion = 2;
constexpr std::uint32_t GameFlagStory = 1u << 0;
constexpr std::uint32_t GameFlagAlife = 1u << 1;

// Reusable game-state packet schema imported from the MIT-licensed Zone project.
// Its wire layout remains independent from the Android TCP handshake above.
namespace ZoneCompat
{
using Header = ZoneProtocol::ZO_Header;
using Opcode = ZoneProtocol::Opcode;
using ClientTransform = ZoneProtocol::ClientTransform;
using SnapshotEntry = ZoneProtocol::SnapshotEntry;
using ServerSnapshot = ZoneProtocol::ServerSnapshot;
using EntityEnterAoI = ZoneProtocol::EntityEnterAoI;
using EntityLeaveAoI = ZoneProtocol::EntityLeaveAoI;
using DamageNotify = ZoneProtocol::DamageNotify;

static_assert(sizeof(Header) == 12, "Zone packet header layout changed");
static_assert(sizeof(ClientTransform) == 29, "Zone transform layout changed");
static_assert(sizeof(SnapshotEntry) == 22, "Zone snapshot entry layout changed");
static_assert(sizeof(EntityEnterAoI) == 132, "Zone entity-enter layout changed");

// Adapter naming keeps game code independent from the upstream namespace.
inline bool IsValidPacket(const void* data, std::size_t size) noexcept
{
    return ZoneProtocol::IsValidPacket(data, size);
}
} // namespace ZoneCompat

#pragma pack(push, 1)
struct ClientHello
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t reserved;
    char playerName[64];
    char playerPassword[64];
    char serverPassword[64];
};

struct ServerHello
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t status;
    std::uint32_t clientId;
    std::uint32_t gameFlags;
    GameDescriptionData game;
};
#pragma pack(pop)
} // namespace AndroidLan
