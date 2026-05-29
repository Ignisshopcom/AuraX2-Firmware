import SwiftUI
import WebKit

private enum AuraXColors {
    static let bg = Color(red: 14.0 / 255.0, green: 15.0 / 255.0, blue: 18.0 / 255.0)
    static let panel = Color(red: 25.0 / 255.0, green: 27.0 / 255.0, blue: 32.0 / 255.0)
    static let panel2 = Color(red: 34.0 / 255.0, green: 37.0 / 255.0, blue: 43.0 / 255.0)
    static let border = Color(red: 48.0 / 255.0, green: 52.0 / 255.0, blue: 60.0 / 255.0)
    static let text = Color(red: 242.0 / 255.0, green: 244.0 / 255.0, blue: 247.0 / 255.0)
    static let muted = Color(red: 154.0 / 255.0, green: 163.0 / 255.0, blue: 173.0 / 255.0)
    static let accent = Color(red: 242.0 / 255.0, green: 140.0 / 255.0, blue: 56.0 / 255.0)
}

struct ContentView: View {
    @StateObject private var scanner = DeviceScanner()

    var body: some View {
        NavigationView {
            ZStack {
                AuraXColors.bg.ignoresSafeArea()
                VStack(spacing: 0) {
                    header
                    deviceList
                }
                .padding(.horizontal, 14)
                .padding(.top, 12)
                .padding(.bottom, 14)
            }
            .navigationBarHidden(true)
        }
        .navigationViewStyle(.stack)
        .preferredColorScheme(.dark)
        .onAppear {
            scanner.scan()
        }
    }

    private var header: some View {
        HStack(spacing: 11) {
            Image("IgnisMark")
                .resizable()
                .scaledToFit()
                .frame(width: 40, height: 40)

            VStack(alignment: .leading, spacing: 1) {
                Text("AuraX")
                    .font(.system(size: 23, weight: .bold))
                    .foregroundStyle(AuraXColors.text)
                    .lineLimit(1)
                Text(subtitle)
                    .font(.system(size: 13, weight: .regular))
                    .foregroundStyle(AuraXColors.muted)
                    .lineLimit(1)
            }

            Spacer(minLength: 8)

            Button {
                scanner.scan()
            } label: {
                Text("Scan")
                    .font(.system(size: 14, weight: .bold))
                    .frame(width: 82, height: 42)
            }
            .foregroundStyle(AuraXColors.bg)
            .background(AuraXColors.accent)
            .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
        }
        .padding(.bottom, 16)
    }

    private var subtitle: String {
        if scanner.isScanning { return "Scanning..." }
        let count = scanner.devices.count
        if count == 0 { return "Local devices" }
        return count == 1 ? "1 device found" : "\(count) devices found"
    }

    private var deviceList: some View {
        Group {
            if scanner.devices.isEmpty {
                Spacer()
                Text(scanner.isScanning ? "Scanning..." : "No devices found")
                    .font(.system(size: 16))
                    .foregroundStyle(AuraXColors.muted)
                Spacer()
            } else {
                ScrollView {
                    LazyVStack(spacing: 9) {
                        ForEach(scanner.devices) { device in
                            NavigationLink {
                                DeviceWebView(device: device)
                            } label: {
                                DeviceCard(device: device)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    .padding(.bottom, 18)
                }
            }
        }
    }
}

private struct DeviceCard: View {
    let device: AuraXDevice

    var body: some View {
        VStack(alignment: .leading, spacing: 11) {
            HStack {
                Text(device.name)
                    .font(.system(size: 18, weight: .bold))
                    .foregroundStyle(AuraXColors.text)
                    .lineLimit(1)
                Spacer()
                Text(">")
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundStyle(AuraXColors.accent)
            }

            HStack(spacing: 7) {
                chip(batteryText)
                chip(syncText)
                chip(wifiText)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 13)
        .background(AuraXColors.panel)
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(AuraXColors.border, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private var syncText: String {
        guard let channel = device.syncChannel, channel > 0 else { return "Sync off" }
        return "Sync \(channel)"
    }

    private var batteryText: String {
        if let battery = device.battery { return "Battery \(battery)%" }
        return "Battery --"
    }

    private var wifiText: String {
        if let wifi = device.wifiPercent { return "Wi-Fi \(wifi)%" }
        return "Wi-Fi --"
    }

    private func chip(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 12))
            .foregroundStyle(AuraXColors.muted)
            .lineLimit(1)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 7)
            .padding(.horizontal, 8)
            .background(AuraXColors.panel2)
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(AuraXColors.border, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }
}

private struct DeviceWebView: View {
    let device: AuraXDevice

    var body: some View {
        ZStack {
            AuraXColors.bg.ignoresSafeArea()
            if let url = URL(string: "http://\(device.ip)/") {
                WebView(url: url)
                    .ignoresSafeArea(edges: .bottom)
            }
        }
        .navigationTitle(device.name)
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct WebView: UIViewRepresentable {
    let url: URL

    func makeUIView(context: Context) -> WKWebView {
        let webView = WKWebView(frame: .zero)
        webView.backgroundColor = UIColor(red: 14.0 / 255.0, green: 15.0 / 255.0, blue: 18.0 / 255.0, alpha: 1)
        webView.scrollView.backgroundColor = webView.backgroundColor
        webView.load(URLRequest(url: url))
        return webView
    }

    func updateUIView(_ uiView: WKWebView, context: Context) {}
}
