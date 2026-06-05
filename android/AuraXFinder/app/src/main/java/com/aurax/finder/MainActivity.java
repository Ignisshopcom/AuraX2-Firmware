package com.aurax.finder;

import android.app.Activity;
import android.app.DownloadManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.webkit.URLUtil;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

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
    private static final int FILE_CHOOSER_REQUEST = 1001;
    private static final int BG = Color.rgb(14, 15, 18);
    private static final int PANEL = Color.rgb(25, 27, 32);
    private static final int PANEL_2 = Color.rgb(34, 37, 43);
    private static final int BORDER = Color.rgb(48, 52, 60);
    private static final int TEXT = Color.rgb(242, 244, 247);
    private static final int MUTED = Color.rgb(154, 163, 173);
    private static final int ACCENT = Color.rgb(242, 140, 56);

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Map<String, Device> devices = new LinkedHashMap<>();
    private ExecutorService workers;
    private LinearLayout list;
    private TextView status;
    private DatagramSocket udpSocket;
    private WifiManager.MulticastLock multicastLock;
    private ValueCallback<Uri[]> fileChooserCallback;
    private boolean showingWebView = false;
    private boolean scanActive = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setupSystemBars();
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
        root.setBackgroundColor(BG);
        applySystemBarPadding(root, 14, 12, 14, 14);

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(0, 0, 0, dp(16));

        ImageView icon = new ImageView(this);
        icon.setImageResource(R.drawable.ignis_mark);
        icon.setAdjustViewBounds(true);
        header.addView(icon, new LinearLayout.LayoutParams(dp(40), dp(40)));

        LinearLayout titleBlock = new LinearLayout(this);
        titleBlock.setOrientation(LinearLayout.VERTICAL);
        titleBlock.setPadding(dp(11), 0, dp(10), 0);
        TextView title = text("AuraX", 23, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setSingleLine(true);
        title.setEllipsize(TextUtils.TruncateAt.END);
        titleBlock.addView(title);
        status = text("Local devices", 13, MUTED);
        titleBlock.addView(status);
        header.addView(titleBlock, new LinearLayout.LayoutParams(0, -2, 1));

        header.addView(button("Scan", v -> startScan()), new LinearLayout.LayoutParams(dp(82), dp(42)));
        root.addView(header);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(0, dp(4), 0, dp(18));
        scroll.addView(list, new ScrollView.LayoutParams(-1, -1));
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));

        setContentView(root);
        renderDevices();
    }

    private void startScan() {
        if (workers != null) workers.shutdownNow();
        stopUdp();
        devices.clear();
        scanActive = true;
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
                scanActive = false;
                if (devices.isEmpty()) {
                    status.setText("Local devices");
                } else {
                    status.setText(deviceCountText());
                }
                renderDevices();
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
        if (p.length > 7) {
            d.syncEnabled = p[6];
            d.syncMask = p[7];
        } else if (p.length > 6) {
            d.syncMask = p[6];
            d.syncEnabled = "0".equals(p[6]) ? "0" : "1";
        }
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
            d.syncEnabled = json.has("sync_enabled") ? (json.optBoolean("sync_enabled") ? "1" : "0") : "";
            if (json.has("sync_mask")) {
                d.syncMask = String.valueOf(json.optInt("sync_mask"));
            } else if (json.has("sync_channel")) {
                int channel = json.optInt("sync_channel");
                d.syncMask = channel >= 1 && channel <= 10 ? String.valueOf(1 << (channel - 1)) : "0";
            }
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
            list.setGravity(Gravity.CENTER);
            TextView empty = text(scanActive ? "Scanning..." : "No devices found", 16, MUTED);
            empty.setGravity(Gravity.CENTER);
            list.addView(empty, new LinearLayout.LayoutParams(-1, -2));
            return;
        }
        list.setGravity(Gravity.NO_GRAVITY);
        for (Device d : devices.values()) {
            list.addView(deviceCard(d));
        }
    }

    private View deviceCard(Device d) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(14), dp(13), dp(14), dp(13));
        card.setBackground(rounded(PANEL, 8, BORDER));
        card.setClickable(true);
        card.setOnClickListener(v -> openDevice(d));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.setMargins(0, 0, 0, dp(9));
        card.setLayoutParams(lp);

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);

        TextView name = text(displayName(d.hostname), 18, TEXT);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.END);
        top.addView(name, new LinearLayout.LayoutParams(0, -2, 1));

        TextView chevron = text(">", 20, ACCENT);
        chevron.setGravity(Gravity.CENTER);
        top.addView(chevron, new LinearLayout.LayoutParams(dp(24), -2));
        card.addView(top);

        LinearLayout meta = new LinearLayout(this);
        meta.setOrientation(LinearLayout.HORIZONTAL);
        meta.setGravity(Gravity.CENTER_VERTICAL);
        meta.setPadding(0, dp(11), 0, 0);
        meta.addView(chip("Battery " + valueOrDash(d.battery, "%")), chipParams(true));
        meta.addView(chip(wifiText(d.rssi)), chipParams(false));
        card.addView(meta);

        TextView sync = chip(syncText(d.syncEnabled, d.syncMask));
        sync.setSingleLine(false);
        LinearLayout.LayoutParams syncLp = new LinearLayout.LayoutParams(-1, -2);
        syncLp.setMargins(0, dp(8), 0, 0);
        card.addView(sync, syncLp);

        return card;
    }

    private TextView chip(String value) {
        TextView tv = text(value, 12, MUTED);
        tv.setSingleLine(true);
        tv.setGravity(Gravity.CENTER);
        tv.setEllipsize(TextUtils.TruncateAt.END);
        tv.setBackground(rounded(PANEL_2, 8, BORDER));
        tv.setPadding(dp(8), dp(7), dp(8), dp(7));
        return tv;
    }

    private LinearLayout.LayoutParams chipParams(boolean rightMargin) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, -2, 1);
        lp.setMargins(0, 0, rightMargin ? dp(7) : 0, 0);
        return lp;
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

    private String syncText(String enabled, String maskValue) {
        if ("0".equals(enabled)) return "Sync off";
        int mask = 0;
        try {
            if (!TextUtils.isEmpty(maskValue)) mask = Integer.parseInt(maskValue);
        } catch (NumberFormatException ignored) {
        }
        mask &= 0x03FF;
        if (mask == 0) return "Sync off";

        StringBuilder channels = new StringBuilder();
        for (int ch = 1; ch <= 10; ch++) {
            if ((mask & (1 << (ch - 1))) == 0) continue;
            if (channels.length() > 0) channels.append(", ");
            channels.append(ch);
        }
        return "Sync channels " + channels;
    }

    private String wifiText(String value) {
        if (TextUtils.isEmpty(value) || "0".equals(value)) return "Wi-Fi --";
        try {
            int rssi = Integer.parseInt(value);
            if (rssi < 0) return "Wi-Fi " + rssiToPercent(rssi) + "%";
            if (rssi <= 100) return "Wi-Fi " + rssi + "%";
        } catch (NumberFormatException ignored) {
        }
        return "Wi-Fi --";
    }

    private void openDevice(Device d) {
        showingWebView = true;
        stopUdp();
        if (workers != null) workers.shutdownNow();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        applySystemBarPadding(root, 0, 0, 0, 0);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(10), dp(10), dp(10), dp(10));
        bar.setBackgroundColor(BG);
        bar.addView(button("Back", v -> showFinder()), new LinearLayout.LayoutParams(dp(78), dp(42)));

        TextView title = text(displayName(d.hostname), 16, TEXT);
        title.setGravity(Gravity.CENTER_VERTICAL);
        title.setSingleLine(true);
        title.setEllipsize(TextUtils.TruncateAt.END);
        title.setPadding(dp(10), 0, dp(10), 0);
        bar.addView(title, new LinearLayout.LayoutParams(0, dp(44), 1));

        WebView web = new WebView(this);
        bar.addView(button("Reload", v -> web.reload()), new LinearLayout.LayoutParams(dp(84), dp(42)));
        root.addView(bar);

        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setLoadWithOverviewMode(true);
        settings.setUseWideViewPort(true);
        settings.setAllowFileAccess(true);
        settings.setAllowContentAccess(true);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                String url = request.getUrl().toString();
                if (isFirmwareDownloadUrl(url)) {
                    downloadFromWebView(url, null, null, "application/octet-stream");
                    return true;
                }
                return false;
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                if (isFirmwareDownloadUrl(url)) {
                    downloadFromWebView(url, null, null, "application/octet-stream");
                    return true;
                }
                return false;
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView webView, ValueCallback<Uri[]> filePathCallback,
                                             WebChromeClient.FileChooserParams fileChooserParams) {
                if (fileChooserCallback != null) {
                    fileChooserCallback.onReceiveValue(null);
                }
                fileChooserCallback = filePathCallback;

                Intent intent;
                try {
                    intent = fileChooserParams.createIntent();
                } catch (Exception ignored) {
                    intent = new Intent(Intent.ACTION_GET_CONTENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("*/*");
                }

                try {
                    startActivityForResult(intent, FILE_CHOOSER_REQUEST);
                } catch (Exception e) {
                    fileChooserCallback = null;
                    filePathCallback.onReceiveValue(null);
                    Toast.makeText(MainActivity.this, "File picker unavailable", Toast.LENGTH_SHORT).show();
                }
                return true;
            }
        });
        web.setDownloadListener((url, userAgent, contentDisposition, mimeType, contentLength) ->
                downloadFromWebView(url, userAgent, contentDisposition, mimeType));
        root.addView(web, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        web.loadUrl("http://" + d.ip + "/");
    }

    private void downloadFromWebView(String url, String userAgent, String contentDisposition, String mimeType) {
        try {
            String filename = URLUtil.guessFileName(url, contentDisposition, mimeType);
            DownloadManager.Request request = new DownloadManager.Request(Uri.parse(url));
            request.setTitle(filename);
            request.setDescription("Downloading firmware");
            if (!TextUtils.isEmpty(userAgent)) {
                request.addRequestHeader("User-Agent", userAgent);
            }
            if (!TextUtils.isEmpty(mimeType)) {
                request.setMimeType(mimeType);
            }
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            request.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, filename);

            DownloadManager manager = (DownloadManager) getSystemService(Context.DOWNLOAD_SERVICE);
            if (manager == null) {
                throw new IllegalStateException("Download manager unavailable");
            }
            manager.enqueue(request);
            Toast.makeText(this, "Downloading " + filename, Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (Exception ignored) {
                Toast.makeText(this, "Download failed", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private boolean isFirmwareDownloadUrl(String url) {
        if (TextUtils.isEmpty(url)) return false;
        try {
            String path = Uri.parse(url).getPath();
            return path != null && path.toLowerCase().endsWith(".bin");
        } catch (Exception ignored) {
            return false;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != FILE_CHOOSER_REQUEST || fileChooserCallback == null) {
            return;
        }
        Uri[] result = WebChromeClient.FileChooserParams.parseResult(resultCode, data);
        fileChooserCallback.onReceiveValue(result);
        fileChooserCallback = null;
    }

    @Override
    public void onBackPressed() {
        if (showingWebView) {
            showFinder();
        } else {
            super.onBackPressed();
        }
    }

    private void setupSystemBars() {
        Window window = getWindow();
        window.setStatusBarColor(BG);
        window.setNavigationBarColor(BG);
        if (Build.VERSION.SDK_INT >= 28) {
            window.setNavigationBarDividerColor(BG);
        }
        if (Build.VERSION.SDK_INT >= 23) {
            window.getDecorView().setSystemUiVisibility(0);
        }
    }

    private void applySystemBarPadding(View root, int leftDp, int topDp, int rightDp, int bottomDp) {
        root.setPadding(dp(leftDp), dp(topDp), dp(rightDp), dp(bottomDp));
        if (Build.VERSION.SDK_INT >= 20) {
            root.setOnApplyWindowInsetsListener((view, insets) -> {
                view.setPadding(
                    dp(leftDp) + insets.getSystemWindowInsetLeft(),
                    dp(topDp) + insets.getSystemWindowInsetTop(),
                    dp(rightDp) + insets.getSystemWindowInsetRight(),
                    dp(bottomDp) + insets.getSystemWindowInsetBottom());
                return insets;
            });
            root.requestApplyInsets();
        }
    }

    private int rssiToPercent(int rssi) {
        if (rssi >= -50) return 100;
        if (rssi <= -100) return 0;
        return (rssi + 100) * 2;
    }

    private Button button(String label, View.OnClickListener click) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextColor(BG);
        b.setTextSize(14);
        b.setAllCaps(false);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(rounded(ACCENT, 8, 0));
        b.setMinHeight(0);
        b.setMinWidth(0);
        b.setPadding(dp(8), 0, dp(8), 0);
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
        String syncEnabled = "";
        String syncMask = "";
        String rssi = "";

        Device(String hostname, String ip) {
            this.hostname = TextUtils.isEmpty(hostname) ? "aurax" : hostname;
            this.ip = ip;
        }
    }
}
