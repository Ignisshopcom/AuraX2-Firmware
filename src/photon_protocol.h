#pragma once

#include <stddef.h>
#include <stdint.h>

// Wifi_commands.xlsx: binary payload only, without the module's +IPD header.
namespace PhotonProtocol {
static constexpr uint16_t BasePort = 5000;
static constexpr uint8_t Start = 0xA4;
static constexpr uint8_t Stop = 0xA3;

struct Packet {
    uint8_t bytes[8] = {};
    size_t size = 0;
};

inline uint32_t elapsedTicks(int64_t nowUs, int64_t startUs) {
    if (nowUs <= startUs) return 0;
    uint64_t ticks = static_cast<uint64_t>(nowUs - startUs) / 10;
    return ticks > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ticks);
}

inline Packet programCommand(uint8_t packetId, bool start, uint16_t prefix, uint32_t ticks = 0) {
    Packet packet;
    if (start && prefix == 0) return packet;
    packet.bytes[0] = packetId;
    packet.bytes[1] = start ? Start : Stop;
    packet.size = start ? 8 : 2;
    if (start) {
        // Captured Photon traffic: elapsed program time in 10 us ticks, LE.
        for (unsigned i = 0; i < 4; ++i) packet.bytes[2 + i] = static_cast<uint8_t>(ticks >> (8 * i));
        packet.bytes[6] = static_cast<uint8_t>(prefix);
        packet.bytes[7] = static_cast<uint8_t>(prefix >> 8);
    }
    return packet;
}
}  // namespace PhotonProtocol
