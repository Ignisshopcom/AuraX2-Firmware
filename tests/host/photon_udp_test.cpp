#include "photon_protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>

#define LOG(...) ((void)0)
static int64_t clockUs = 0;
int64_t esp_timer_get_time() { return clockUs; }
enum { WL_CONNECTED = 3 };
struct IPAddress {
    uint8_t octets[4];
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : octets{a,b,c,d} {}
    uint8_t operator[](unsigned i) const { return octets[i]; }
    bool operator==(const IPAddress& other) const {
        for (unsigned i = 0; i < 4; ++i) if (octets[i] != other.octets[i]) return false;
        return true;
    }
};
struct {
    int state = WL_CONNECTED;
    IPAddress ip{192,168,8,22}, mask{255,255,255,0};
    int status() const { return state; }
    IPAddress localIP() const { return ip; }
    IPAddress subnetMask() const { return mask; }
} WiFi;
struct Datagram {
    IPAddress ip{0,0,0,0};
    uint16_t port = 0;
    std::vector<uint8_t> bytes;
};
struct FakeUdp {
    Datagram pending;
    std::vector<Datagram> sent;
    bool beginOk = true, writeOk = true, endOk = true;
    int64_t sendCostUs = 0;
    unsigned begins = 0, ends = 0;
    int beginPacket(IPAddress ip, uint16_t port) {
        ++begins; pending = Datagram(); pending.ip = ip; pending.port = port;
        return beginOk;
    }
    size_t write(const uint8_t* data, size_t size) {
        if (!writeOk) return 0;
        pending.bytes.assign(data, data + size); return size;
    }
    int endPacket() { ++ends; if (endOk) sent.push_back(pending); clockUs += sendCostUs; return endOk; }
};
class WifiControl {
public:
    bool _staServicesStarted = true, _apMode = false;
    struct { bool syncEnabled = true; uint16_t syncMask = 1; } _cfg;
    FakeUdp _udp;
    uint8_t _photonPacketId = 0;
    void sendPhotonProgramCommand(bool start, uint16_t prefix = 0, int64_t startUs = 0);
};
#include "photon_send_source.inc"

int main() {
    WifiControl c;
    c.sendPhotonProgramCommand(true, 1);
    assert(c._udp.sent.size() == 1);
    auto packet = c._udp.sent.back();
    assert(packet.ip == IPAddress(192,168,8,255) && packet.port == 5001);
    assert(packet.bytes == std::vector<uint8_t>({1,0xa4,0,0,0,0,1,0}));
    c.sendPhotonProgramCommand(false);
    assert(c._udp.sent.back().bytes == std::vector<uint8_t>({2,0xa3}));
    assert(c._udp.sent.back().port == 5001);

    c = WifiControl(); c._cfg.syncMask = (1 << 1) | (1 << 9);
    WiFi.ip = IPAddress(10,20,4,8); WiFi.mask = IPAddress(255,255,0,0);
    c.sendPhotonProgramCommand(true, 0x1234);
    assert(c._udp.sent.size() == 2);
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned group = i ? 10 : 2;
        const auto& p = c._udp.sent[i];
        assert(p.port == 5000 + group && p.ip == IPAddress(10,20,255,255));
        assert(p.bytes == std::vector<uint8_t>({1,0xa4,0,0,0,0,0x34,0x12}));
    }
    c = WifiControl(); c._cfg.syncMask = 0xffff;
    c.sendPhotonProgramCommand(false); assert(c._udp.sent.size() == 10);
    for (unsigned i = 0; i < 10; ++i) {
        assert(c._udp.sent[i].port == 5001 + i);
        assert(c._udp.sent[i].bytes == std::vector<uint8_t>({1,0xa3}));
    }

    c = WifiControl(); c.sendPhotonProgramCommand(true, 0); assert(c._udp.begins == 0);
    c._cfg.syncMask = 0; c.sendPhotonProgramCommand(false); assert(c._udp.begins == 0);
    c._cfg.syncMask = 0x8000; c.sendPhotonProgramCommand(false); assert(c._udp.begins == 0);
    c = WifiControl(); c._cfg.syncEnabled = false; c.sendPhotonProgramCommand(true, 1); assert(c._udp.begins == 0);
    c = WifiControl(); c._apMode = true; c.sendPhotonProgramCommand(true, 1); assert(c._udp.begins == 0);
    c = WifiControl(); c._staServicesStarted = false; c.sendPhotonProgramCommand(false); assert(c._udp.begins == 0);
    c = WifiControl(); WiFi.state = 0; c.sendPhotonProgramCommand(false); assert(c._udp.begins == 0);
    WiFi.state = WL_CONNECTED; WiFi.ip = IPAddress(0,0,0,0);
    c.sendPhotonProgramCommand(false); assert(c._udp.begins == 0);
    WiFi.ip = IPAddress(192,168,8,22); WiFi.mask = IPAddress(0,0,0,0);
    c.sendPhotonProgramCommand(false); assert(c._udp.begins == 0);
    WiFi.mask = IPAddress(255,255,255,0);
    c._udp.beginOk = false; c.sendPhotonProgramCommand(false); assert(c._udp.ends == 0);
    c._udp.beginOk = true; c._udp.writeOk = false;
    c.sendPhotonProgramCommand(false); assert(c._udp.ends == 0);
    c._udp.writeOk = true; c._udp.endOk = false;
    c.sendPhotonProgramCommand(false); assert(c._udp.sent.empty());
    c._udp.endOk = true; c.sendPhotonProgramCommand(false); assert(c._udp.sent.size() == 1);
    // Replay real Photon packet 2c a4 e1 14 0a 00 01 00 from 2026-09-22.
    c = WifiControl(); c._photonPacketId = 43;
    clockUs = 7000000;
    c.sendPhotonProgramCommand(true, 1, 392950);
    assert(c._udp.sent.back().bytes == std::vector<uint8_t>({0x2c,0xa4,0xe1,0x14,0x0a,0,1,0}));
    // Browser delay and local processing contribute to the same elapsed time.
    c = WifiControl(); c._cfg.syncMask = 3; c._udp.sendCostUs = 2000;
    clockUs = 1170000;
    c.sendPhotonProgramCommand(true, 2, 980000);
    assert(c._udp.sent[0].bytes == std::vector<uint8_t>({1,0xa4,0x38,0x4a,0,0,2,0}));
    assert(c._udp.sent[1].bytes == std::vector<uint8_t>({1,0xa4,0,0x4b,0,0,2,0}));
    assert(PhotonProtocol::elapsedTicks(10, 10) == 0);
    assert(PhotonProtocol::elapsedTicks(10, 11) == 0);
    assert(PhotonProtocol::elapsedTicks(100009, 0) == 10000);
    assert(PhotonProtocol::elapsedTicks(10000, -2000000) == 201000);
    assert(PhotonProtocol::elapsedTicks((int64_t)UINT32_MAX * 10 + 100, 0) == UINT32_MAX);
    c = WifiControl(); c._photonPacketId = 255;
    c.sendPhotonProgramCommand(false); c.sendPhotonProgramCommand(false);
    assert(c._udp.sent[0].bytes[0] == 0 && c._udp.sent[1].bytes[0] == 1);
    std::puts("PASS Photon: exact START/STOP payloads, group ports, subnet broadcast, multi-group, disabled/AP/offline, failures and recovery");
    std::puts("PASS Photon timing: captured wire format, 10-us elapsed time, browser/local delays, per-group send time, counter wrap");
}
