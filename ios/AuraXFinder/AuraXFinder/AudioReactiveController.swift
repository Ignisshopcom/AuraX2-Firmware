import AVFoundation
import Accelerate
import Network
import UIKit
import WebKit

private enum AudioClock {
    private static let origin = ProcessInfo.processInfo.systemUptime
    static var elapsed: TimeInterval {
        let start = origin
        return max(0, ProcessInfo.processInfo.systemUptime - start)
    }
}

private final class AudioNoRedirect: NSObject, URLSessionTaskDelegate {
    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
}

private final class AudioBiquad {
    private let b0: Double, b1: Double, b2: Double, a1: Double, a2: Double
    private var z1 = 0.0, z2 = 0.0
    init(rate: Double, frequency: Double, q: Double, high: Bool) {
        let w = 2 * Double.pi * frequency / rate, c = cos(w), alpha = sin(w) / (2 * q)
        let denominator = 1 + alpha
        let numerator = high ? 1 + c : 1 - c
        let coefficient = numerator / (2 * denominator)
        b0 = coefficient; b2 = coefficient
        b1 = (high ? -(1 + c) : 1 - c) / denominator
        a1 = -2 * c / denominator; a2 = (1 - alpha) / denominator
    }
    func process(_ x: Double) -> Double {
        let y = b0 * x + z1
        z1 = b1 * x - a1 * y + z2
        z2 = b2 * x - a2 * y
        return y
    }
}

private final class AudioSpectrum {
    static let size = 2048, hop = 256, bands = 64
    private let setup: vDSP_DFT_Setup
    private let sampleRate: Double
    private var real = [Float](repeating: 0, count: AudioSpectrum.size)
    private let imaginary = [Float](repeating: 0, count: AudioSpectrum.size)
    private var outReal = [Float](repeating: 0, count: AudioSpectrum.size)
    private var outImaginary = [Float](repeating: 0, count: AudioSpectrum.size)
    private var window = [Float](repeating: 0, count: AudioSpectrum.size)
    private var samples = [Float](repeating: 0, count: AudioSpectrum.size)
    private var power = [Double](repeating: 0, count: AudioSpectrum.size / 2)
    private var edges = [Double](repeating: 0, count: AudioSpectrum.bands + 2)
    private var filtered = [Double](repeating: 0, count: AudioSpectrum.bands)
    private var raw = [Double](repeating: 0, count: AudioSpectrum.bands)
    private var crossovers = [[AudioBiquad]]()
    private var energy = [Double](repeating: 0, count: 4)
    private var cursor = 0, count = 0
    private var powerScale = 0.0, reference = 0.12, bassAverage = 0.0, fluxAverage = 0.0
    private var smoothed = [Double](repeating: 0, count: 4)
    private var lastBeat: TimeInterval = -1
    private(set) var envelopes = [Int](repeating: 0, count: AudioSpectrum.bands)

    init?(sampleRate: Double) {
        guard let setup = vDSP_DFT_zop_CreateSetup(nil, vDSP_Length(Self.size), .FORWARD) else { return nil }
        self.setup = setup
        self.sampleRate = sampleRate
        var windowEnergy = 0.0
        for i in 0..<Self.size {
            window[i] = Float(0.5 - 0.5 * cos(2 * Double.pi * Double(i) / Double(Self.size - 1)))
            windowEnergy += Double(window[i] * window[i])
        }
        powerScale = 2 / (Double(Self.size) * windowEnergy)
        let low = 2595 * log10(1 + 30.0 / 700)
        let high = 2595 * log10(1 + min(16000, sampleRate * 0.45) / 700)
        for i in edges.indices { edges[i] = 700 * (pow(10, (low + (high - low) * Double(i) / Double(Self.bands + 1)) / 2595) - 1) }
        for (lower, upper) in [(35.0, 250.0), (250.0, 2000.0), (2000.0, min(16000, sampleRate * 0.45))] {
            crossovers.append([
                AudioBiquad(rate: sampleRate, frequency: lower, q: 0.5411961, high: true),
                AudioBiquad(rate: sampleRate, frequency: lower, q: 1.30656296, high: true),
                AudioBiquad(rate: sampleRate, frequency: upper, q: 0.5411961, high: false),
                AudioBiquad(rate: sampleRate, frequency: upper, q: 1.30656296, high: false)
            ])
        }
    }
    deinit { vDSP_DFT_DestroySetup(setup) }
    func append(_ data: UnsafePointer<Float>, count frameCount: Int, sensitivity: Int, smoothing: Int, result: ([Int]) -> Void) {
        for i in 0..<frameCount {
            samples[cursor] = data[i]
            let sample = Double(data[i])
            energy[0] += sample * sample
            for band in 0..<3 {
                var value = sample
                for filter in crossovers[band] { value = filter.process(value) }
                energy[band + 1] += value * value
            }
            cursor = (cursor + 1) % Self.size
            count += 1
            if count == Self.hop {
                count = 0
                result(analyze(sensitivity: sensitivity, smoothing: smoothing))
                for band in energy.indices { energy[band] = 0 }
            }
        }
    }
    private func byteLevel(_ value: Double) -> Int {
        guard value.isFinite && value > 0 else { return 0 }
        let shaped = value <= 0.55 ? value : 0.55 + 0.45 * (1 - 0.45 / (value - 0.10))
        return min(255, Int((shaped * 255).rounded()))
    }
    private func analyze(sensitivity: Int, smoothing: Int) -> [Int] {
        for i in 0..<Self.size { real[i] = samples[(cursor + i) % Self.size] * window[i] }
        vDSP_DFT_Execute(setup, real, imaginary, &outReal, &outImaginary)
        for k in 1..<power.count {
            power[k] = Double(outReal[k] * outReal[k] + outImaginary[k] * outImaginary[k]) * powerScale
        }
        let dt = Double(Self.hop) / sampleRate, rms = sqrt(energy[0] / Double(Self.hop))
        let gain = pow(Double(max(0, min(300, sensitivity))) / 130, 2)
        reference = max(0.08, max(rms, reference * exp(-dt / 3)))
        let decayMs = 600 * pow(Double(max(0, min(95, smoothing))) / 95, 2)
        let release = decayMs == 0 ? 0 : exp(-dt * 1000 / decayMs)
        let silence = rms < 0.0005
        var flux = 0.0
        for b in raw.indices {
            let first = max(1, Int(floor(edges[b] * Double(Self.size) / sampleRate)))
            let last = min(power.count - 1, Int(ceil(edges[b + 2] * Double(Self.size) / sampleRate)))
            let center = edges[b + 1]
            var sum = 0.0
            if first <= last { for k in first...last {
                let f = Double(k) * sampleRate / Double(Self.size)
                let weight = f < center ? (f - edges[b]) / (center - edges[b]) : (edges[b + 2] - f) / (edges[b + 2] - center)
                sum += power[k] * max(0, weight)
            } }
            raw[b] = silence ? 0 : max(0, sqrt(sum) - 0.0004) / reference
            flux += max(0, raw[b] - filtered[b])
            filtered[b] = raw[b] >= filtered[b] ? raw[b] : release * filtered[b] + (1 - release) * raw[b]
            envelopes[b] = byteLevel(filtered[b] * gain * 0.85)
        }
        flux /= Double(Self.bands)
        let bass = sqrt(energy[1] / Double(Self.hop))
        let now = AudioClock.elapsed
        let beat = gain > 0 && !silence && rms > 0.008 && now - lastBeat > 0.14 &&
            (bass > max(0.008, bassAverage * 1.6) || flux > max(0.035, fluxAverage * 1.8))
        let baselineRelease = exp(-dt / 0.35)
        bassAverage = bassAverage * baselineRelease + bass * (1 - baselineRelease)
        fluxAverage = fluxAverage * baselineRelease + flux * (1 - baselineRelease)
        if beat { lastBeat = now }
        for i in 0..<4 {
            let value = silence ? 0 : max(0, sqrt(energy[i] / Double(Self.hop)) - 0.0005) / reference
            smoothed[i] = value >= smoothed[i] ? value : release * smoothed[i] + (1 - release) * value
        }
        return smoothed.map { byteLevel($0 * gain * 0.85) } + [beat ? 255 : 0]
    }
}

// Lifecycle and HTTP run on the main queue; the audio tap only analyzes samples.
final class AudioReactiveController: NSObject, WKScriptMessageHandler {
    weak var webView: WKWebView?
    var allowedHost: String
    private var session = ""
    private var engine: AVAudioEngine?
    private var timer: Timer?
    private var permissionTimer: Timer?
    private var busy = false
    private var active = false
    private var failures = 0
    private let lock = NSLock()
    private var tapSession = ""
    private var sensitivity = 130
    private var smoothing = 25
    private var latest = [Int](repeating: 0, count: 5)
    private var pendingLevels = [Int](repeating: 0, count: 5)
    private var pendingBands = [Int](repeating: 0, count: 64)
    private var pending = false
    private var pendingSince: UInt32 = 0
    private var latestBands = [Int](repeating: 0, count: 64)
    private var captureMs: UInt32 = 0, streamSequence: UInt32 = 0
    private var audioStream: NWConnection?
    private var streamToken = [UInt8]()
    private var lastAck = 0.0, lastPublish = 0.0
    private var roundTripMs = -1
    private static var nowMs: UInt32 { UInt32(truncatingIfNeeded: UInt64(AudioClock.elapsed * 1000)) }
    private var observers = [NSObjectProtocol]()
    private let network: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 1
        configuration.timeoutIntervalForResource = 1.5
        configuration.httpMaximumConnectionsPerHost = 1
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: configuration, delegate: AudioNoRedirect(), delegateQueue: nil)
    }()

    init(host: String) {
        allowedHost = host
        super.init()
        for notification in [UIApplication.didEnterBackgroundNotification, AVAudioSession.interruptionNotification,
                             AVAudioSession.mediaServicesWereResetNotification] {
            observers.append(NotificationCenter.default.addObserver(forName: notification, object: nil, queue: .main) { [weak self] _ in
                self?.stop()
            })
        }
    }
    deinit {
        observers.forEach { NotificationCenter.default.removeObserver($0) }
        network.finishTasksAndInvalidate()
    }

    static func isLocalIP(_ host: String) -> Bool {
        let parts = host.split(separator: ".")
        let n = parts.compactMap { Int($0) }
        guard parts.count == 4, n.count == 4, n.allSatisfy({ (0...255).contains($0) }) else { return false }
        return n[0] == 10 || (n[0] == 172 && (16...31).contains(n[1])) ||
            (n[0] == 192 && n[1] == 168) || (n[0] == 169 && n[1] == 254)
    }

    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        let origin = message.frameInfo.securityOrigin
        guard message.frameInfo.isMainFrame, origin.protocol == "http", origin.host == allowedHost,
              origin.port == 80 || origin.port == 0, Self.isLocalIP(allowedHost),
              let text = message.body as? String, let bytes = text.data(using: .utf8),
              let data = (try? JSONSerialization.jsonObject(with: bytes)) as? [String: Any] else { return }
        switch data["action"] as? String {
        case "capabilities": emit(["type": "capabilities", "playback": false])
        case "start":
            guard session.isEmpty, let id = data["session"] as? String,
                  id.range(of: "^[0-9a-f]{32}$", options: .regularExpression) != nil else { return }
            session = id
            guard data["source"] as? String == "microphone" else {
                stop(error: "Internal audio from other apps is unavailable on iOS. Select Microphone.")
                return
            }
            settings(data)
            permissionTimer = Timer.scheduledTimer(withTimeInterval: 60, repeats: false) { [weak self] _ in self?.stop(error: "Audio permission timed out") }
            AVAudioSession.sharedInstance().requestRecordPermission { [weak self] granted in
                DispatchQueue.main.async {
                    guard let self = self, self.session == id else { return }
                    self.permissionTimer?.invalidate()
                    if granted { self.startMicrophone(id: id) }
                    else { self.stop(error: "Microphone permission was not granted") }
                }
            }
        case "settings": settings(data)
        case "stop":
            if data["session"] as? String == session { stop(restore: data["restore"] as? Bool ?? true) }
        default: break
        }
    }
    private func settings(_ data: [String: Any]) {
        lock.lock()
        sensitivity = max(0, min(300, data["sensitivity"] as? Int ?? 130))
        smoothing = max(0, min(95, data["smoothing"] as? Int ?? 25))
        pending = false
        lock.unlock()
    }
    private func startMicrophone(id: String) {
        do {
            let audio = AVAudioSession.sharedInstance()
            try audio.setCategory(.playAndRecord, mode: .measurement, options: [.mixWithOthers, .defaultToSpeaker, .allowBluetoothA2DP])
            try audio.setPreferredIOBufferDuration(0.01)
            try audio.setActive(true)
            let engine = AVAudioEngine()
            let input = engine.inputNode
            let format = input.outputFormat(forBus: 0)
            guard format.sampleRate > 0, format.channelCount > 0, let spectrum = AudioSpectrum(sampleRate: format.sampleRate) else {
                throw NSError(domain: "AuraX", code: 1, userInfo: [NSLocalizedDescriptionKey: "Microphone input unavailable"])
            }
            lock.lock(); tapSession = id; latest = [Int](repeating: 0, count: 5); pending = false; lock.unlock()
            input.installTap(onBus: 0, bufferSize: AVAudioFrameCount(AudioSpectrum.hop), format: format) { [weak self] buffer, _ in
                guard let self = self, let samples = buffer.floatChannelData?[0] else { return }
                self.lock.lock()
                let sensitivity = self.sensitivity, smoothing = self.smoothing
                let current = self.tapSession == id
                self.lock.unlock()
                guard current else { return }
                spectrum.append(samples, count: Int(buffer.frameLength), sensitivity: sensitivity, smoothing: smoothing) { levels in
                    self.lock.lock()
                    if self.tapSession == id {
                        self.latest = levels; self.latestBands = spectrum.envelopes
                        self.captureMs = Self.nowMs
                        if !self.pending || self.captureMs &- self.pendingSince > 30 {
                            self.pendingLevels = [Int](repeating: 0, count: 5)
                            self.pendingBands = [Int](repeating: 0, count: 64)
                            self.pendingSince = self.captureMs
                        }
                        for i in 0..<5 { self.pendingLevels[i] = max(self.pendingLevels[i], levels[i]) }
                        for i in 0..<64 { self.pendingBands[i] = max(self.pendingBands[i], self.latestBands[i]) }
                        self.pending = true
                    }
                    self.lock.unlock()
                }
            }
            self.engine = engine
            try engine.start()
            let host = allowedHost
            post(host: host, action: "start?transport=udp2", id: id) { [weak self] code, protocolName in
                guard let self = self else { return }
                guard self.session == id else { self.post(host: host, action: "stop", id: id); return }
                guard (200...299).contains(code) else { self.stop(error: "AuraX audio could not start. Check firmware and Wi-Fi."); return }
                self.active = true; self.busy = false; self.failures = 0
                if protocolName == "AXA2:4211:64" { self.startStream(id: id) }
                self.emitState(id: id)
                let timer = Timer(timeInterval: self.audioStream == nil ? 0.025 : 0.010, repeats: true) { [weak self] _ in self?.sendLatest(id: id) }
                self.timer = timer
                RunLoop.main.add(timer, forMode: .common)
            }
        } catch { stop(error: "Audio capture failed: \(error.localizedDescription)") }
    }
    private func sendLatest(id: String) {
        guard session == id, active, !busy else { return }
        lock.lock()
        let stale = Self.nowMs &- captureMs > 100
        let hasFrame = pending
        let values = pendingLevels, bands = pendingBands, captured = captureMs
        pending = false
        lock.unlock()
        if stale { stop(error: "Audio capture fell behind"); return }
        guard hasFrame else { return }
        if audioStream != nil { sendStream(id: id, values: values, bands: bands, captured: captured); return }
        let frame = ([1] + values).map { String(format: "%02x", max(0, min(255, $0))) }.joined()
        busy = true
        post(host: allowedHost, action: "data", id: id, body: frame) { [weak self] code, _ in
            guard let self = self, self.session == id else { return }
            self.busy = false
            self.failures = (200...299).contains(code) ? 0 : self.failures + 1
            if self.failures >= 2 || code == 409 { self.stop(error: "Audio connection to AuraX ended") }
            else { self.emitState(id: id) }
        }
    }
    private func startStream(id: String) {
        streamToken = stride(from: 0, to: 32, by: 2).map { offset in
            let start = id.index(id.startIndex, offsetBy: offset), end = id.index(start, offsetBy: 2)
            return UInt8(id[start..<end], radix: 16) ?? 0
        }
        streamSequence = 0; roundTripMs = -1
        lastAck = AudioClock.elapsed
        let connection = NWConnection(host: NWEndpoint.Host(allowedHost), port: 4211, using: .udp)
        audioStream = connection
        connection.stateUpdateHandler = { [weak self] state in
            guard let self = self, self.session == id else { return }
            if case .failed = state { self.stop(error: "Audio stream disconnected") }
        }
        connection.start(queue: .main)
        receiveAck(connection, id: id)
    }
    private func receiveAck(_ connection: NWConnection, id: String) {
        connection.receiveMessage { [weak self] data, _, _, error in
            guard let self = self, self.session == id, self.audioStream === connection else { return }
            if error != nil { self.stop(error: "Audio stream disconnected"); return }
            if let data = data, data.count == 28 {
                let bytes = [UInt8](data)
                if Array(bytes[0..<4]) == Array("AXK2".utf8) && Array(bytes[4..<20]) == self.streamToken {
                    let read32: (Int) -> UInt32 = { i in UInt32(bytes[i]) | UInt32(bytes[i+1]) << 8 | UInt32(bytes[i+2]) << 16 | UInt32(bytes[i+3]) << 24 }
                    let behind = self.streamSequence &- read32(20), elapsed = Self.nowMs &- read32(24)
                    if behind < 120 && elapsed < 1300 {
                        self.lastAck = AudioClock.elapsed
                        self.roundTripMs = Int(elapsed)
                    }
                }
            }
            self.receiveAck(connection, id: id)
        }
    }
    private func sendStream(id: String, values: [Int], bands: [Int], captured: UInt32) {
        guard let connection = audioStream else { return }
        let now = AudioClock.elapsed
        guard now - lastAck < 1.3, Self.nowMs &- captured <= 100 else {
            stop(error: "Audio stream timed out"); return
        }
        var bytes = Array("AXA2".utf8) + streamToken
        streamSequence &+= 1
        for value in [streamSequence, captured] {
            for shift in stride(from: 0, to: 32, by: 8) { bytes.append(UInt8(truncatingIfNeeded: value >> shift)) }
        }
        bytes += values.map { UInt8(clamping: $0) }
        bytes.append(64)
        bytes += bands.map { UInt8(clamping: $0) }
        busy = true
        connection.send(content: Data(bytes), completion: .contentProcessed { [weak self] error in
            guard let self = self, self.session == id else { return }
            self.busy = false
            if error != nil { self.stop(error: "Audio stream disconnected") }
        })
        if now - lastPublish >= 0.08 { lastPublish = now; emitState(id: id) }
    }
    private func post(host: String, action: String, id: String, body: String = "", completion: ((Int, String) -> Void)? = nil) {
        let separator = action.contains("?") ? "&" : "?"
        guard let url = URL(string: "http://\(host)/audio/\(action)\(separator)session=\(id)") else { completion?(0, ""); return }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("text/plain", forHTTPHeaderField: "Content-Type")
        request.httpBody = body.data(using: .utf8)
        network.dataTask(with: request) { data, response, _ in
            let code = (response as? HTTPURLResponse)?.statusCode ?? 0
            DispatchQueue.main.async { completion?(code, data.flatMap { String(data: $0, encoding: .utf8) } ?? "") }
        }.resume()
    }
    func stop(restore: Bool = true, error: String = "") {
        guard !session.isEmpty else { return }
        let id = session
        session = ""; active = false; busy = false
        audioStream?.cancel(); audioStream = nil
        permissionTimer?.invalidate(); permissionTimer = nil
        timer?.invalidate(); timer = nil
        lock.lock(); tapSession = ""; lock.unlock()
        engine?.stop(); engine?.inputNode.removeTap(onBus: 0); engine = nil
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        post(host: allowedHost, action: "stop?restore=\(restore ? 1 : 0)", id: id)
        emitState(id: id, error: error)
    }
    private func emitState(id: String, error: String = "") {
        lock.lock(); let values = latest, bands = latestBands; lock.unlock()
        emit(["type": "state", "session": id, "active": active, "error": error, "spectrum": bands,
              "transport": audioStream == nil ? "http" : "udp2", "roundTripMs": roundTripMs,
              "levels": ["volume": values[0], "bass": values[1], "mid": values[2], "treble": values[3], "beat": values[4]]])
    }
    private func emit(_ data: [String: Any]) {
        guard let bytes = try? JSONSerialization.data(withJSONObject: data), let json = String(data: bytes, encoding: .utf8) else { return }
        webView?.evaluateJavaScript("window.dispatchEvent(new CustomEvent('aurax-audio',{detail:\(json)}))", completionHandler: nil)
    }
}
