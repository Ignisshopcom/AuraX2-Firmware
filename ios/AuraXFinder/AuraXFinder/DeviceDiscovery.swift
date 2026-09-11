import Foundation
import Darwin

struct AuraXDevice: Identifiable, Equatable {
    var id: String { ip }
    let name: String
    let ip: String
    var battery: Int?
    var wifiPercent: Int?
    var syncChannel: Int?
}

final class DeviceScanner: NSObject, ObservableObject, NetServiceBrowserDelegate, NetServiceDelegate {
    @Published private(set) var devices: [AuraXDevice] = []
    @Published private(set) var isScanning = false

    private var browser: NetServiceBrowser?
    private var services: [NetService] = []
    private var timeoutWorkItem: DispatchWorkItem?

    func scan() {
        timeoutWorkItem?.cancel()
        browser?.stop()
        services.removeAll()
        devices.removeAll()
        isScanning = true

        let browser = NetServiceBrowser()
        browser.delegate = self
        browser.searchForServices(ofType: "_aurax._tcp.", inDomain: "local.")
        self.browser = browser

        probe(ip: "192.168.4.1", fallbackName: "aurax")

        let done = DispatchWorkItem { [weak self] in
            self?.isScanning = false
            self?.browser?.stop()
        }
        timeoutWorkItem = done
        DispatchQueue.main.asyncAfter(deadline: .now() + 7, execute: done)
    }

    func netServiceBrowser(_ browser: NetServiceBrowser, didFind service: NetService, moreComing: Bool) {
        service.delegate = self
        services.append(service)
        service.resolve(withTimeout: 4)
    }

    func netServiceDidResolveAddress(_ sender: NetService) {
        let fallbackName = cleanName(sender.name)
        var resolved = false
        for data in sender.addresses ?? [] {
            if let ip = ipv4String(from: data) {
                resolved = true
                probe(ip: ip, fallbackName: fallbackName)
            }
        }
        if !resolved, let host = sender.hostName {
            probe(host: host, fallbackName: fallbackName)
        }
    }

    private func probe(ip: String, fallbackName: String) {
        guard isPrivateIp(ip) else { return }
        probe(urlHost: ip, displayIp: ip, fallbackName: fallbackName)
    }

    private func probe(host: String, fallbackName: String) {
        probe(urlHost: host, displayIp: host.replacingOccurrences(of: ".local.", with: ".local"), fallbackName: fallbackName)
    }

    private func probe(urlHost: String, displayIp: String, fallbackName: String) {
        guard let url = URL(string: "http://\(urlHost)/status") else { return }
        var request = URLRequest(url: url)
        request.timeoutInterval = 1.2
        URLSession.shared.dataTask(with: request) { [weak self] data, _, _ in
            guard let self, let data else { return }
            guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return }

            let ip = (json["ip"] as? String).flatMap { self.isPrivateIp($0) ? $0 : nil } ?? displayIp
            let name = (json["device_name"] as? String) ?? (json["hostname"] as? String) ?? fallbackName
            let rssi = json["rssi"] as? Int
            let wifi = rssi.map { $0 < 0 ? Self.rssiPercent($0) : min(max($0, 0), 100) }
            let device = AuraXDevice(
                name: self.cleanName(name),
                ip: ip,
                battery: json["battery_pct"] as? Int,
                wifiPercent: wifi,
                syncChannel: json["sync_channel"] as? Int
            )
            DispatchQueue.main.async {
                self.upsert(device)
            }
        }.resume()
    }

    private func upsert(_ device: AuraXDevice) {
        if let idx = devices.firstIndex(where: { $0.ip == device.ip }) {
            devices[idx] = device
        } else {
            devices.append(device)
            devices.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        }
    }

    private func ipv4String(from data: Data) -> String? {
        data.withUnsafeBytes { rawBuffer in
            guard let base = rawBuffer.baseAddress else { return nil }
            let sockaddrPtr = base.assumingMemoryBound(to: sockaddr.self)
            guard sockaddrPtr.pointee.sa_family == sa_family_t(AF_INET) else { return nil }
            let addr = base.assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr
            var copy = addr
            var buffer = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
            guard inet_ntop(AF_INET, &copy, &buffer, socklen_t(INET_ADDRSTRLEN)) != nil else { return nil }
            return String(cString: buffer)
        }
    }

    private func cleanName(_ raw: String) -> String {
        var out = raw
        if out.hasSuffix(".local.") { out.removeLast(7) }
        if out.hasSuffix(".local") { out.removeLast(6) }
        return out.isEmpty ? "aurax" : out
    }

    private func isPrivateIp(_ ip: String) -> Bool {
        let parts = ip.split(separator: ".").compactMap { Int($0) }
        guard parts.count == 4 else { return false }
        return parts[0] == 10 || (parts[0] == 172 && (16...31).contains(parts[1])) || (parts[0] == 192 && parts[1] == 168)
    }

    private static func rssiPercent(_ rssi: Int) -> Int {
        if rssi >= -50 { return 100 }
        if rssi <= -100 { return 0 }
        return (rssi + 100) * 2
    }
}
