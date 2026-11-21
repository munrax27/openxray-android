#pragma once

#include "../NET_Common.h"
#include "../../../third_party/zone/packets.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

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

constexpr std::uint16_t PacketMagic = 0x5A4F;
constexpr std::uint8_t PacketVersion = 1;

static_assert(sizeof(Header) == 12, "Zone packet header layout changed");
static_assert(sizeof(ClientTransform) == 29, "Zone transform layout changed");
static_assert(sizeof(SnapshotEntry) == 22, "Zone snapshot entry layout changed");
static_assert(sizeof(EntityEnterAoI) == 132, "Zone entity-enter layout changed");

// Validates a complete Zone-format datagram/frame before any payload is read.
// This helper is a protocol adapter only; it does not switch the active X-Ray
// client/server traffic away from the existing Android LAN transport.
inline bool IsValidPacket(const void* data, std::size_t size) noexcept
{
    if (!data || size < sizeof(Header))
        return false;

    Header header{};
    std::memcpy(&header, data, sizeof(header));
    return header.magic == PacketMagic &&
        header.protocol == PacketVersion &&
        header.payload_len == size - sizeof(Header);
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
