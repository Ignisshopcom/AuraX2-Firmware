#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Hardware calls are stubs; the included event and retry functions are extracted
// verbatim from wifi_control.cpp by run_wifi_reconnect_test.cjs.
static uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define LOG(...) ((void)0)
#define LOGLN(...) ((void)0)
enum { WIFI_REASON_AUTH_EXPIRE = 1, WIFI_REASON_AUTH_FAIL,
       WIFI_REASON_ASSOC_EXPIRE, WIFI_REASON_ASSOC_FAIL,
       WIFI_REASON_HANDSHAKE_TIMEOUT, WIFI_REASON_BEACON_TIMEOUT,
       WIFI_REASON_NO_AP_FOUND, WIFI_REASON_ASSOC_LEAVE };
enum WiFiEvent_t { ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
                   ARDUINO_EVENT_WIFI_STA_CONNECTED, ARDUINO_EVENT_WIFI_STA_GOT_IP };
struct WiFiEventInfo_t { struct { uint8_t reason; } wifi_sta_disconnected; };
enum { WL_CONNECTED = 3 };
struct FakeWifi {
    int state = 0;
    bool autoReconnect = true;
    int status() const { return state; }
    void setAutoReconnect(bool value) { autoReconnect = value; }
} WiFi;
using esp_err_t = int;
static unsigned connects = 0;
esp_err_t esp_wifi_connect() { ++connects; return 0; }
struct Service { void end() {} void stop() {} } MDNS;

class WifiControl {
public:
    bool _apActive = false, _apHadClient = false, _staServicesStarted = false;
    uint32_t _lastStaRetryMs = 0, _staDisconnectedSinceMs = 0, _lastAnnounceMs = 0;
    uint8_t _staRetryCount = 0;
    Service _udp;
    const char* ssid = "test";
    const char* staSsid() { return ssid; }
    uint8_t apClientCount() { return 0; }
    void stopRealtimeUdp() {}
    void startStaServices() { _staServicesStarted = true; }
    void startFallbackAp() { enableManagedReconnect(false); _apActive = true; }
    void enableManagedReconnect(bool);
    void processManagedReconnect(uint32_t);
    void maintainWifi();
};

#include "wifi_reconnect_source.inc"

static void disconnectAt(uint32_t at, uint8_t reason = WIFI_REASON_NO_AP_FOUND) {
    clockMs = at;
    logWifiEvent(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, {{reason}});
}
static WifiControl fresh() {
    WifiControl control;
    control.enableManagedReconnect(false);
    control.enableManagedReconnect(true);
    WiFi.state = 0;
    connects = 0;
    clockMs = 0;
    return control;
}

int main() {
    auto c = fresh();
    assert(!WiFi.autoReconnect);
    disconnectAt(100);
    c.processManagedReconnect(8099); assert(connects == 0);
    c.processManagedReconnect(8100); assert(connects == 1);
    disconnectAt(8200);
    c.processManagedReconnect(9199); assert(connects == 1);
    c.processManagedReconnect(9200); assert(connects == 2);
    disconnectAt(9300);
    c.processManagedReconnect(11299); assert(connects == 2);
    c.processManagedReconnect(11300); assert(connects == 3);
    disconnectAt(11400);
    c.processManagedReconnect(15399); assert(connects == 3);
    c.processManagedReconnect(15400); assert(connects == 4);
    disconnectAt(15500);
    c.processManagedReconnect(19500); assert(connects == 5);

    c = fresh();
    disconnectAt(100); disconnectAt(200); // Framework completed its own first retry.
    c.processManagedReconnect(699); assert(connects == 0);
    c.processManagedReconnect(700); assert(connects == 1);

    c = fresh();
    c.processManagedReconnect(7999); assert(connects == 0);
    c.processManagedReconnect(8000); assert(connects == 1);
    logWifiEvent(ARDUINO_EVENT_WIFI_STA_CONNECTED, {{0}});
    c.processManagedReconnect(20000); assert(connects == 1); // Waiting for DHCP.
    clockMs = 20000;
    logWifiEvent(ARDUINO_EVENT_WIFI_STA_GOT_IP, {{0}});
    WiFi.state = WL_CONNECTED;
    c.maintainWifi(); assert(c._staRetryCount == 0 && c._staServicesStarted);
    c.processManagedReconnect(90000); assert(connects == 1);

    c = fresh();
    logWifiEvent(ARDUINO_EVENT_WIFI_STA_CONNECTED, {{0}});
    WiFi.state = WL_CONNECTED;
    clockMs = 100; c.maintainWifi();
    // Arduino 2.0.6 retains WL_CONNECTED after AUTH_EXPIRE.
    disconnectAt(200, WIFI_REASON_AUTH_EXPIRE);
    clockMs = 201; c.maintainWifi();
    assert(!c._staServicesStarted && c._staDisconnectedSinceMs == 201);
    c.processManagedReconnect(8199); assert(connects == 0);
    c.processManagedReconnect(8200); assert(connects == 1);

    c = fresh();
    c.enableManagedReconnect(false);
    disconnectAt(100);
    assert(!wifiEventSnapshot().reconnectPending);
    c.processManagedReconnect(90000); assert(connects == 0);
    c = fresh();
    disconnectAt(100, WIFI_REASON_ASSOC_LEAVE);
    assert(!wifiEventSnapshot().reconnectPending);

    c = fresh();
    clockMs = 100; c.maintainWifi();
    clockMs = 30101; c.maintainWifi();
    assert(c._apActive && !wifiEventSnapshot().reconnectEnabled);
    const auto before = connects;
    clockMs = 90000; c.maintainWifi();
    assert(connects == before);
    c = fresh(); c.ssid = "";
    clockMs = 90000; c.maintainWifi(); assert(connects == 0);

    c = fresh();
    disconnectAt(UINT32_MAX - 1000);
    disconnectAt(UINT32_MAX - 100);
    c.processManagedReconnect(398); assert(connects == 0);
    c.processManagedReconnect(399); assert(connects == 1);
    std::puts("PASS: production reconnect functions: grace, backoff, watchdog, DHCP, connected, disabled, AP, no SSID, clock wrap");
}
