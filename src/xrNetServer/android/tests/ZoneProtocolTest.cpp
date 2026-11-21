#include "third_party/zone/packet_validation.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

int main()
{
    static_assert(sizeof(ZoneProtocol::ZO_Header) == 12);
    static_assert(sizeof(ZoneProtocol::ClientTransform) == 29);
    static_assert(sizeof(ZoneProtocol::SnapshotEntry) == 22);
    static_assert(sizeof(ZoneProtocol::EntityEnterAoI) == 132);

    const std::uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    ZoneProtocol::ZO_Header header{};
    header.magic = ZoneProtocol::PacketMagic;
    header.protocol = ZoneProtocol::PacketVersion;
    header.flags = 0x02;
    header.sequence = 1;
    header.opcode = static_cast<std::uint16_t>(ZoneProtocol::Opcode::CLIENT_TRANSFORM);
    header.payload_len = sizeof(payload);

    std::vector<std::uint8_t> frame(sizeof(header) + sizeof(payload));
    std::memcpy(frame.data(), &header, sizeof(header));
    std::memcpy(frame.data() + sizeof(header), payload, sizeof(payload));

    assert(ZoneProtocol::IsValidPacket(frame.data(), frame.size()));
    assert(!ZoneProtocol::IsValidPacket(nullptr, frame.size()));
    assert(!ZoneProtocol::IsValidPacket(frame.data(), sizeof(header) - 1));

    auto badMagic = frame;
    badMagic[0] ^= 0xFF;
    assert(!ZoneProtocol::IsValidPacket(badMagic.data(), badMagic.size()));

    auto badLength = frame;
    badLength[10] = 0;
    badLength[11] = 0;
    assert(!ZoneProtocol::IsValidPacket(badLength.data(), badLength.size()));

    auto truncated = frame;
    truncated.pop_back();
    assert(!ZoneProtocol::IsValidPacket(truncated.data(), truncated.size()));

    return 0;
}
