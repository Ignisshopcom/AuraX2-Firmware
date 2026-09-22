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

inline Packet programCommand(uint8_t group, bool start, uint16_t prefix) {
    Packet packet;
    if (group < 1 || group > 10 || (start && prefix == 0)) return packet;
    packet.bytes[0] = group;
    packet.bytes[1] = start ? Start : Stop;
    packet.size = start ? 8 : 2;
    if (start) {
        // No shared Photon clock: the developer permits a zero timestamp.
        // Prefix uses little-endian; byte order still needs device confirmation.
        packet.bytes[6] = static_cast<uint8_t>(prefix);
        packet.bytes[7] = static_cast<uint8_t>(prefix >> 8);
    }
    return packet;
}
}  // namespace PhotonProtocol
