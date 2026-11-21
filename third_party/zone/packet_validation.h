#pragma once

#include "packets.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ZoneProtocol
{
constexpr std::uint16_t PacketMagic = 0x5A4F;
constexpr std::uint8_t PacketVersion = 1;

// Validate the exact Zone wire envelope before any caller reads its payload.
// The TCP transport may carry one of these packets inside its own length frame.
inline bool IsValidPacket(const void* data, std::size_t size) noexcept
{
    if (!data || size < sizeof(ZO_Header))
        return false;

    ZO_Header header{};
    std::memcpy(&header, data, sizeof(header));
    return header.magic == PacketMagic &&
        header.protocol == PacketVersion &&
        header.payload_len == size - sizeof(ZO_Header);
}
} // namespace ZoneProtocol
