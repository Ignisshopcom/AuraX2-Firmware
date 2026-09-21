#include "audio_stream.h"
#include <assert.h>
#include <stdio.h>

static void write32(uint8_t* p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = value >> (i * 8);
}
int main() {
    uint8_t token[16] = {1, 2, 3}, packet[AudioStream::Bytes] = {};
    memcpy(packet, "AXA2", 4);
    memcpy(packet + 4, token, 16);
    packet[33] = 64;
    AudioStream::Receiver receiver;
    receiver.reset(token);
    write32(packet + 20, 0xfffffffeu);
    write32(packet + 24, 0xfffffff0u);
    assert(receiver.accept(packet, sizeof(packet), 1000));
    assert(!receiver.accept(packet, sizeof(packet), 1001)); // Duplicate.
    write32(packet + 20, 0xffffffffu); write32(packet + 24, 0);
    assert(receiver.accept(packet, sizeof(packet), 1016)); // Sender clock wrap.
    write32(packet + 20, 0); write32(packet + 24, 16);
    assert(receiver.accept(packet, sizeof(packet), 1032)); // Sequence wrap.
    write32(packet + 20, 5); write32(packet + 24, 96);
    assert(receiver.accept(packet, sizeof(packet), 1112)); // Packet loss is allowed.
    write32(packet + 20, 4);
    assert(!receiver.accept(packet, sizeof(packet), 1113)); // Out of order.
    write32(packet + 20, 6);
    assert(!receiver.accept(packet, sizeof(packet), 1800)); // Delayed audio is not replayed.
    write32(packet + 24, 816);
    assert(receiver.accept(packet, sizeof(packet), 1832)); // Current data recovers.
    write32(packet + 20, 7);
    for (unsigned int size = 0; size < sizeof(packet); ++size)
        assert(!receiver.accept(packet, size, 1833));
    packet[33] = 65; assert(!receiver.accept(packet, sizeof(packet), 1833));
    packet[33] = 64; packet[4] ^= 1;
    assert(!receiver.accept(packet, sizeof(packet), 1833));
    packet[4] ^= 1; packet[0] = 'B';
    assert(!receiver.accept(packet, sizeof(packet), 1833));
    puts("PASS audio stream: owner, version, lengths, loss, ordering, stale frames, clock/sequence wrap");
    packet[0] = 'A';
    receiver.reset(token);
    for (uint32_t ms = 0; ms <= 2000; ms += 10) {
        write32(packet + 20, ms / 10);
        write32(packet + 24, ms);
        assert(receiver.accept(packet, sizeof(packet), ms));
        assert(!receiver.accept(packet, sizeof(packet), ms)); // Duplicates do not inflate diagnostics.
    }
    assert(receiver.packetRateX10(2000) == 1000);
    assert(receiver.packetRateX10(3001) == 0);
    receiver.reset(token);
    assert(receiver.packetRateX10(3001) == 0);
    puts("PASS measured packet rate, duplicate exclusion, timeout and session reset");
}
