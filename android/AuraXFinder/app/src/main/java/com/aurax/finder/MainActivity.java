package com.aurax.finder;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity {
    private static final int DISCOVERY_PORT = 4210;
    private static final int BG = Color.rgb(19, 19, 19);
    private static final int SURFACE = Color.rgb(31, 31, 31);
    private static final int SURFACE_ALT = Color.rgb(42, 42, 42);
    private static final int BORDER = Color.rgb(55, 55, 55);
    private static final int TEXT = Color.rgb(242, 237, 231);
    private static final int MUTED = Color.rgb(154, 148, 140);
    private static final int ACCENT = Color.rgb(234, 104, 32);

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Map<String, Device> devices = new LinkedHashMap<>();
    private ExecutorService workers;
    private LinearLayout list;
    private TextView status;
    private DatagramSocket udpSocket;
    private WifiManager.MulticastLock multicastLock;
    private boolean showingWebView = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        showFinder();
        startScan();
    }

    @Override
    protected void onStop() {
        stopUdp();
        super.onStop();
    }

    private void showFinder() {
        showingWebView = false;

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(18), dp(18), dp(10));
        root.setBackgroundColor(BG);

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(0, 0, 0, dp(14));

        ImageView icon = new ImageView(this);
        icon.setImageResource(R.drawable.ignis_icon);
        icon.setAdjustViewBounds(true);
        header.addView(icon, new LinearLayout.LayoutParams(dp(44), dp(44)));

        LinearLayout titleBlock = new LinearLayout(this);
        titleBlock.setOrientation(LinearLayout.VERTICAL);
        titleBlock.setPadding(dp(12), 0, dp(12), 0);
        TextView title = text("AuraX Finder", 24, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        titleBlock.addView(title);
        TextView subtitle = text("Local devices", 13, MUTED);
        titleBlock.addView(subtitle);
        header.addView(titleBlock, new LinearLayout.LayoutParams(0, -2, 1));

        header.addView(button("Scan", v -> startScan()), new LinearLayout.LayoutParams(dp(92), dp(44)));
        root.addView(header);

        status = text("Scanning...", 14, MUTED);
        status.setPadding(0, 0, 0, dp(10));
        root.addView(status);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(false);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(0, dp(2), 0, dp(16));
        scroll.addView(list);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));

        setContentView(root);
        renderDevices();
    }

    private void startScan() {
        if (workers != null) workers.shutdownNow();
        stopUdp();
        devices.clear();
        renderDevices();
        if (status != null) status.setText("Scanning...");

        workers = Executors.newFixedThreadPool(36);
        acquireMulticastLock();
        listenUdp(12000);
        sendDiscoveryQueries();
        scanHttpNetworks();
    }

    private void acquireMulticastLock() {
        try {
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                multicastLock = wifi.createMulticastLock("aurax-discovery");
                multicastLock.setReferenceCounted(false);
                multicastLock.acquire();
            }
        } catch (Exception ignored) {
        }
    }

    private void listenUdp(long ms) {
        workers.execute(() -> {
            long until = System.currentTimeMillis() + ms;
            try {
                udpSocket = new DatagramSocket(null);
                udpSocket.setReuseAddress(true);
                udpSocket.setBroadcast(true);
                udpSocket.bind(new InetSocketAddress(DISCOVERY_PORT));
                udpSocket.setSoTimeout(600);
                byte[] buf = new byte[256];
                while (System.currentTimeMillis() < until && !Thread.currentThread().isInterrupted()) {
                    DatagramPacket packet = new DatagramPacket(buf, buf.length);
                    try {
                        udpSocket.receive(packet);
                        String msg = new String(packet.getData(), 0, packet.getLength(), StandardCharsets.UTF_8).trim();
                        parseUdp(msg, packet.getAddress().getHostAddress());
                    } catch (SocketTimeoutException ignored) {
                    }
                }
            } catch (Exception ignored) {
            } finally {
                stopUdp();
            }
        });
    }

    private void sendDiscoveryQueries() {
        workers.execute(() -> {
            Set<String> targets = new LinkedHashSet<>();
            targets.add("255.255.255.255");
            targets.add("192.168.4.255");
            for (String prefix : localPrefixes()) targets.add(prefix + ".255");

            byte[] query = "AURAX?".getBytes(StandardCharsets.UTF_8);
            for (int i = 0; i < 3; i++) {
                for (String target : targets) {
                    try (DatagramSocket socket = new DatagramSocket()) {
                        socket.setBroadcast(true);
                        DatagramPacket packet = new DatagramPacket(
                            query, query.length, InetAddress.getByName(target), DISCOVERY_PORT);
                        socket.send(packet);
                    } catch (Exception ignored) {
                    }
                }
                sleep(700);
            }
        });
    }

    private void scanHttpNetworks() {
        workers.execute(() -> {
            Set<String> ips = new LinkedHashSet<>();
            ips.add("192.168.4.1");
            for (String prefix : localPrefixes()) {
                for (int i = 1; i < 255; i++) ips.add(prefix + "." + i);
            }

            for (String ip : ips) {
                try {
                    workers.execute(() -> probeStatus(ip));
                } catch (Exception ignored) {
                }
            }

            sleep(6500);
            main.post(() -> {
                if (devices.isEmpty()) {
                    status.setText("No devices found");
                } else {
                    status.setText(deviceCountText());
                }
            });
        });
    }

    private List<String> localPrefixes() {
        Set<String> prefixes = new LinkedHashSet<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InetAddress addr : Collections.list(ni.getInetAddresses())) {
                    if (!(addr instanceof Inet4Address)) continue;
                    String ip = addr.getHostAddress();
                    if (!isPrivateIp(ip)) continue;
                    String[] parts = ip.split("\\.");
                    if (parts.length == 4) prefixes.add(parts[0] + "." + parts[1] + "." + parts[2]);
                }
            }
        } catch (Exception ignored) {
        }
        return new ArrayList<>(prefixes);
    }

    private void parseUdp(String msg, String fallbackIp) {
        if (!msg.startsWith("AURAX ")) return;
        String[] p = msg.split("\\s+");
        if (p.length < 3) return;
        String host = p[1];
        String ip = isPrivateIp(p[2]) ? p[2] : fallbackIp;
        Device d = new Device(host, ip);
        if (p.length > 4) d.battery = p[4];
        if (p.length > 5) d.rssi = p[5];
        if (p.length > 6) d.sync = p[6];
        addDevice(d);
    }

    private void probeStatus(String ip) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL("http://" + ip + "/status");
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(450);
            conn.setReadTimeout(650);
            conn.setUseCaches(false);
            if (conn.getResponseCode() != 200) return;
            BufferedReader reader = new BufferedReader(new InputStreamReader(conn.getInputStream()));
            StringBuilder body = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) body.append(line);
            JSONObject json = new JSONObject(body.toString());
            String host = json.optString("hostname", "aurax");
            Device d = new Device(host, ip);
            d.battery = json.has("battery_pct") ? String.valueOf(json.optInt("battery_pct")) : "";
            d.sync = json.has("sync_channel") ? String.valueOf(json.optInt("sync_channel")) : "";
            d.rssi = json.has("rssi") ? String.valueOf(json.optInt("rssi")) : "";
            addDevice(d);
        } catch (Exception ignored) {
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    private void addDevice(Device device) {
        if (!isPrivateIp(device.ip)) return;
        main.post(() -> {
            devices.put(device.ip, device);
            if (status != null) status.setText(deviceCountText());
            renderDevices();
        });
    }

    private String deviceCountText() {
        int count = devices.size();
        return count == 1 ? "1 device found" : count + " devices found";
    }

    private void renderDevices() {
        if (list == null) return;
        list.removeAllViews();
        if (devices.isEmpty()) {
            TextView empty = text("No devices found", 16, MUTED);
            empty.setGravity(Gravity.CENTER);
            empty.setPadding(0, dp(72), 0, 0);
            list.addView(empty, new LinearLayout.LayoutParams(-1, -2));
            return;
        }
        for (Device d : devices.values()) {
            list.addView(deviceCard(d));
        }
    }

    private View deviceCard(Device d) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(15), dp(14), dp(15), dp(14));
        card.setBackground(rounded(SURFACE, 8, BORDER));
        card.setClickable(true);
        card.setOnClickListener(v -> openDevice(d));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.setMargins(0, 0, 0, dp(10));
        card.setLayoutParams(lp);

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);

        TextView name = text(displayName(d.hostname), 19, TEXT);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.END);
        top.addView(name, new LinearLayout.LayoutParams(0, -2, 1));

        TextView chevron = text(">", 22, ACCENT);
        chevron.setGravity(Gravity.CENTER);
        top.addView(chevron, new LinearLayout.LayoutParams(dp(24), -2));
        card.addView(top);

        LinearLayout meta = new LinearLayout(this);
        meta.setOrientation(LinearLayout.HORIZONTAL);
        meta.setGravity(Gravity.LEFT);
        meta.setPadding(0, dp(12), 0, 0);
        meta.addView(chip("Battery " + valueOrDash(d.battery, "%")));
        meta.addView(chip(syncText(d.sync)));
        meta.addView(chip(wifiText(d.rssi)));
        card.addView(meta);

        return card;
    }

    private TextView chip(String value) {
        TextView tv = text(value, 12, MUTED);
        tv.setSingleLine(true);
        tv.setBackground(rounded(SURFACE_ALT, 999, BORDER));
        tv.setPadding(dp(9), dp(6), dp(9), dp(6));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
        lp.setMargins(0, 0, dp(7), 0);
        tv.setLayoutParams(lp);
        return tv;
    }

    private String displayName(String host) {
        if (TextUtils.isEmpty(host)) return "aurax";
        String out = host;
        if (out.endsWith(".local")) out = out.substring(0, out.length() - 6);
        return out;
    }

    private String valueOrDash(String value, String suffix) {
        if (TextUtils.isEmpty(value)) return "--";
        return value + suffix;
    }

    private String syncText(String value) {
        if (TextUtils.isEmpty(value) || "0".equals(value)) return "Sync off";
        return "Sync " + value;
    }

    private String wifiText(String value) {
        if (TextUtils.isEmpty(value) || "0".equals(value)) return "Wi-Fi --";
        return "Wi-Fi " + value + " dBm";
    }

    private void openDevice(Device d) {
        showingWebView = true;
        stopUdp();
        if (workers != null) workers.shutdownNow();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(8), dp(8), dp(8), dp(8));
        bar.setBackgroundColor(BG);
        bar.addView(button("Back", v -> showFinder()), new LinearLayout.LayoutParams(dp(82), dp(44)));

        TextView title = text(displayName(d.hostname), 16, TEXT);
        title.setGravity(Gravity.CENTER_VERTICAL);
        title.setSingleLine(true);
        title.setEllipsize(TextUtils.TruncateAt.END);
        title.setPadding(dp(10), 0, dp(10), 0);
        bar.addView(title, new LinearLayout.LayoutParams(0, dp(44), 1));

        WebView web = new WebView(this);
        bar.addView(button("Reload", v -> web.reload()), new LinearLayout.LayoutParams(dp(92), dp(44)));
        root.addView(bar);

        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setLoadWithOverviewMode(true);
        settings.setUseWideViewPort(true);
        web.setWebViewClient(new WebViewClient());
        root.addView(web, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        web.loadUrl("http://" + d.ip + "/");
    }

    @Override
    public void onBackPressed() {
        if (showingWebView) {
            showFinder();
        } else {
            super.onBackPressed();
        }
    }

    private Button button(String label, View.OnClickListener click) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextColor(Color.WHITE);
        b.setTextSize(14);
        b.setAllCaps(false);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(rounded(ACCENT, 8, 0));
        b.setOnClickListener(click);
        return b;
    }

    private TextView text(String value, int sp, int color) {
        TextView tv = new TextView(this);
        tv.setText(value);
        tv.setTextSize(sp);
        tv.setTextColor(color);
        return tv;
    }

    private GradientDrawable rounded(int color, int radiusDp, int strokeColor) {
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(color);
        bg.setCornerRadius(dp(radiusDp));
        if (strokeColor != 0) bg.setStroke(dp(1), strokeColor);
        return bg;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void stopUdp() {
        if (udpSocket != null) {
            udpSocket.close();
            udpSocket = null;
        }
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
    }

    private boolean isPrivateIp(String ip) {
        if (ip == null) return false;
        String[] p = ip.split("\\.");
        if (p.length != 4) return false;
        try {
            int a = Integer.parseInt(p[0]);
            int b = Integer.parseInt(p[1]);
            return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
        } catch (NumberFormatException e) {
            return false;
        }
    }

    private void sleep(long ms) {
        try {
            TimeUnit.MILLISECONDS.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static class Device {
        final String hostname;
        final String ip;
        String battery = "";
        String sync = "";
        String rssi = "";

        Device(String hostname, String ip) {
            this.hostname = TextUtils.isEmpty(hostname) ? "aurax" : hostname;
            this.ip = ip;
        }
    }
}
