#include "photon_protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>

#define LOG(...) ((void)0)
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
    unsigned begins = 0, ends = 0;
    int beginPacket(IPAddress ip, uint16_t port) {
        ++begins; pending = Datagram(); pending.ip = ip; pending.port = port;
        return beginOk;
    }
    size_t write(const uint8_t* data, size_t size) {
        if (!writeOk) return 0;
        pending.bytes.assign(data, data + size); return size;
    }
    int endPacket() { ++ends; if (endOk) sent.push_back(pending); return endOk; }
};
class WifiControl {
public:
    bool _staServicesStarted = true, _apMode = false;
    struct { bool syncEnabled = true; uint16_t syncMask = 1; } _cfg;
    FakeUdp _udp;
    void sendPhotonProgramCommand(bool start, uint16_t prefix = 0);
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
    assert(c._udp.sent.back().bytes == std::vector<uint8_t>({1,0xa3}));
    assert(c._udp.sent.back().port == 5001);

    c = WifiControl(); c._cfg.syncMask = (1 << 1) | (1 << 9);
    WiFi.ip = IPAddress(10,20,4,8); WiFi.mask = IPAddress(255,255,0,0);
    c.sendPhotonProgramCommand(true, 0x1234);
    assert(c._udp.sent.size() == 2);
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned group = i ? 10 : 2;
        const auto& p = c._udp.sent[i];
        assert(p.port == 5000 + group && p.ip == IPAddress(10,20,255,255));
        assert(p.bytes == std::vector<uint8_t>({(uint8_t)group,0xa4,0,0,0,0,0x34,0x12}));
    }
    c = WifiControl(); c._cfg.syncMask = 0xffff;
    c.sendPhotonProgramCommand(false); assert(c._udp.sent.size() == 10);
    for (unsigned i = 0; i < 10; ++i) {
        assert(c._udp.sent[i].port == 5001 + i);
        assert(c._udp.sent[i].bytes == std::vector<uint8_t>({(uint8_t)(i+1),0xa3}));
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
    assert(PhotonProtocol::programCommand(0, true, 1).size == 0);
    assert(PhotonProtocol::programCommand(11, true, 1).size == 0);
    std::puts("PASS Photon: exact START/STOP payloads, group ports, subnet broadcast, multi-group, disabled/AP/offline, failures and recovery");
}
