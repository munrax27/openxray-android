#pragma once

#include "../NET_Common.h"

#include <cstdint>

namespace AndroidLan
{
constexpr std::uint32_t ProtocolMagic = 0x584C414E; // "XLAN"
constexpr std::uint16_t ProtocolVersion = 1;

#pragma pack(push, 1)
struct ClientHello
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t reserved;
    char playerName[64];
    char password[64];
};

struct ServerHello
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t status;
    std::uint32_t clientId;
    GameDescriptionData game;
};
#pragma pack(pop)
} // namespace AndroidLan
