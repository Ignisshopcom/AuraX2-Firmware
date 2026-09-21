#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// AXA2: magic, 128-bit owner, sequence LE32, capture time LE32,
// five envelopes, band count, 64 mel envelopes. ACK echoes the first 28 bytes.
namespace AudioStream {
constexpr uint16_t Port = 4211;
constexpr size_t Bands = 64, Header = 34, Bytes = Header + Bands, AckBytes = 28;
inline uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
struct Receiver {
    uint8_t owner[16] = {};
    bool seen = false;
    uint32_t sequence = 0, clockOffset = 0;
    uint32_t windowStart = 0, windowPackets = 0, lastPacketMs = 0;
    uint16_t rateX10 = 0;
    void reset(const uint8_t* token) {
        memcpy(owner, token, 16);
        seen = false;
        windowPackets = rateX10 = 0;
    }
    bool accept(const uint8_t* p, size_t len, uint32_t now) {
        if (len != Bytes || memcmp(p, "AXA2", 4) || memcmp(p + 4, owner, 16) || p[33] != Bands) return false;
        const uint32_t next = read32(p + 20), delta = next - sequence;
        if (seen && (!delta || delta >= 0x80000000u)) return false;
        const uint32_t offset = now - read32(p + 24);
        // Compare relative clocks, never assume phone and ESP share a time origin.
        if (seen && int32_t(offset - clockOffset) > 250) return false;
        if (!seen || int32_t(offset - clockOffset) < 0) clockOffset = offset;
        if (!seen) windowStart = now;
        lastPacketMs = now;
        ++windowPackets;
        uint32_t elapsed = now - windowStart;
        if (elapsed >= 1000) {
            uint32_t rate = uint64_t(windowPackets) * 10000 / elapsed;
            rateX10 = rate > 65535 ? 65535 : rate;
            windowPackets = 0;
            windowStart = now;
        }
        sequence = next;
        seen = true;
        return true;
    }
    uint16_t packetRateX10(uint32_t now) const {
        return seen && uint32_t(now - lastPacketMs) < 1000 ? rateX10 : 0;
    }
};
}
