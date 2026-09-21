#include "audio_group.h"
#include <assert.h>
#include <stdio.h>

using namespace AudioGroup;
int main() {
    Packet p;
    p.mask = 1; p.session[0] = 42; p.sequence = 1;
    uint8_t mac[6] = {1,2,3,4,5,6}, other[6] = {6,5,4,3,2,1};
    assert(sizeof(p) <= 250 && valid(p));
    Receiver rods[10];
    for (uint32_t frame = 1; frame <= 300; ++frame) {
        p.sequence = frame; p.ageMs = frame * IntervalMs;
        p.levels[1] = frame % 256; p.beats = frame / 20;
        p.mode = (frame / 30) % 10;
        for (int i = 0; i < 10; ++i) {
            if (i == 3 && frame % 7 == 0) continue; // An individual receiver loses packets.
            assert(rods[i].accept(p, mac, p.ageMs, 2, true, 1) == Receiver::Frame);
            assert(rods[i].accept(p, mac, p.ageMs, 2, true, 1) == Receiver::Rejected);
        }
    }
    p.sequence++; p.ageMs += IntervalMs;
    assert(rods[0].accept(p, other, p.ageMs, 0, true, 1) == Receiver::Rejected);
    assert(rods[0].accept(p, mac, p.ageMs, 0, false, 1) == Receiver::Rejected);
    assert(rods[0].accept(p, mac, p.ageMs, 0, true, 2) == Receiver::Rejected);
    assert(rods[0].accept(p, mac, p.ageMs, 101, true, 1) == Receiver::Rejected);
    auto malformed = p; malformed.mode = 10;
    assert(rods[0].accept(malformed, mac, p.ageMs, 0, true, 1) == Receiver::Rejected);
    malformed = p; malformed.magic[3] = '2';
    assert(!valid(malformed));
    malformed = p; malformed.mask = 0x8000;
    assert(!valid(malformed));
    p.kind = Stop;
    for (auto& rod : rods) {
        assert(rod.accept(p, mac, p.ageMs, 0, true, 1) == Receiver::Stopped);
        auto late = p; late.kind = Data; late.sequence++;
        assert(rod.accept(late, mac, p.ageMs + 10, 0, true, 1) == Receiver::Rejected);
        assert(!rod.active());
    }
    p.kind = Data; p.session[0]++; p.sequence = 1; p.ageMs = 0;
    assert(rods[0].accept(p, mac, 10000, 0, true, 1) == Receiver::Frame);
    rods[0].block(); // Local STOP, ON/OFF or program overrides only this receiver.
    p.sequence++;
    assert(rods[0].accept(p, mac, 10017, 0, true, 1) == Receiver::Rejected);
    assert(rods[1].accept(p, mac, 10017, 0, true, 1) == Receiver::Frame);
    assert(rods[1].expired(11000));
    rods[1].expire(); // A transient gap can recover; an explicit stop cannot.
    assert(rods[1].accept(p, mac, 11000, 0, true, 1) == Receiver::Rejected);
    p.sequence++; p.ageMs += 1000;
    assert(rods[1].accept(p, mac, 11017, 0, true, 1) == Receiver::Frame);
    Receiver lateJoin;
    assert(lateJoin.accept(p, mac, 11017, 0, true, 1) == Receiver::Frame);
    lateJoin.expire();
    lateJoin.block(); // A command DURING a network gap also retires the last owner.
    ++p.sequence;
    assert(lateJoin.accept(p, mac, 11034, 0, true, 1) == Receiver::Rejected);
    Receiver wrapped;
    p.sequence = 0xfffffffe; p.ageMs = 0xfffffff0;
    assert(wrapped.accept(p, mac, 0xfffffff0, 0, true, 1) == Receiver::Frame);
    p.sequence = 1; p.ageMs = 4;
    assert(wrapped.accept(p, mac, 4, 0, true, 1) == Receiver::Frame);
    puts("PASS 10 simulated receivers, loss, duplicate/order, groups, competing senders, STOP, override, recovery, wrap");
}
