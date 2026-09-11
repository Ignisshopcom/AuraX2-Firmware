package com.aurax.finder;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.projection.MediaProjectionManager;
import android.os.*;
import android.webkit.WebView;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;
import java.util.Collections;
import java.lang.ref.WeakReference;
import org.json.JSONObject;

/** Messages are limited to the selected LAN origin and its main frame. */
final class AudioWebBridge {
    static final int AUDIO_PERMISSION = 1101, PROJECTION_PERMISSION = 1102;
    private static WeakReference<AudioWebBridge> permissionOwner = new WeakReference<>(null);
    private static WeakReference<AudioWebBridge> projectionOwner = new WeakReference<>(null);
    private final Activity activity;
    private final WebView web;
    private final String host;
    private final Handler main = new Handler(Looper.getMainLooper());
    private String session = "", lastState = "";
    private JSONObject pending;
    private boolean closed;

    AudioWebBridge(Activity activity, WebView web, String host) {
        this.activity = activity; this.web = web; this.host = host;
        if (isLocalIp(host) && WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            WebViewCompat.addWebMessageListener(web, "AuraXAudio", Collections.singleton("http://" + host),
                (view, message, origin, isMainFrame, reply) -> {
                    if (closed || !isMainFrame || !"http".equals(origin.getScheme()) || !host.equals(origin.getHost()) ||
                        (origin.getPort() != -1 && origin.getPort() != 80)) return;
                    try { command(new JSONObject(message.getData())); } catch (Exception e) { fail("Invalid audio request"); }
                });
        }
        main.post(poll);
    }

    static boolean isLocalIp(String host) {
        if (host == null || !host.matches("[0-9]{1,3}(\\.[0-9]{1,3}){3}")) return false;
        String[] parts = host.split("\\."); int[] n = new int[4];
        for (int i = 0; i < 4; ++i) { n[i] = Integer.parseInt(parts[i]); if (n[i] > 255) return false; }
        return n[0] == 10 || (n[0] == 172 && n[1] >= 16 && n[1] <= 31) ||
            (n[0] == 192 && n[1] == 168) || (n[0] == 169 && n[1] == 254);
    }

    private void command(JSONObject data) throws Exception {
        String action = data.optString("action");
        if ("capabilities".equals(action)) {
            emit(new JSONObject().put("type", "capabilities").put("playback", Build.VERSION.SDK_INT >= 29).toString());
        } else if ("start".equals(action)) {
            if (pending != null || !session.isEmpty()) return;
            String id = data.optString("session"), source = data.optString("source");
            if (!id.matches("[0-9a-f]{32}") || !("microphone".equals(source) || "playback".equals(source))) return;
            session = id; pending = data;
            if (permissionOwner.get() != null || projectionOwner.get() != null) { fail("Close the existing permission dialog first"); return; }
            if (activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED ||
                (Build.VERSION.SDK_INT >= 33 && activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)) {
                permissionOwner = new WeakReference<>(this);
                String[] permissions = Build.VERSION.SDK_INT >= 33
                    ? new String[]{Manifest.permission.RECORD_AUDIO, Manifest.permission.POST_NOTIFICATIONS}
                    : new String[]{Manifest.permission.RECORD_AUDIO};
                try { activity.requestPermissions(permissions, AUDIO_PERMISSION); }
                catch (Exception e) { permissionOwner.clear(); fail("Audio permission request failed"); }
            } else requestCapture();
        } else if ("stop".equals(action) && session.equals(data.optString("session"))) {
            pending = null;
            AudioReactiveService.stopSession(session, data.optBoolean("restore", true));
            session = "";
        } else if ("settings".equals(action)) {
            AudioReactiveService.settings(data.optString("session"), data.optInt("sensitivity", 130), data.optInt("smoothing", 25));
        }
    }
    private void requestCapture() {
        if (closed || pending == null) return;
        if ("playback".equals(pending.optString("source"))) {
            if (Build.VERSION.SDK_INT < 29) { fail("Device audio requires Android 10 or later"); return; }
            try {
                MediaProjectionManager manager = (MediaProjectionManager)activity.getSystemService(Activity.MEDIA_PROJECTION_SERVICE);
                projectionOwner = new WeakReference<>(this);
                activity.startActivityForResult(manager.createScreenCaptureIntent(), PROJECTION_PERMISSION);
            } catch (Exception e) { projectionOwner.clear(); fail("Device audio sharing is unavailable"); }
        } else launch(Activity.RESULT_OK, null);
    }
    static void permissionResult(int[] results) {
        AudioWebBridge owner = permissionOwner.get(); permissionOwner.clear();
        if (owner == null || owner.closed || owner.pending == null) return;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) owner.requestCapture();
        else owner.fail("Audio permission was not granted");
    }
    static void projectionResult(int result, Intent consent) {
        AudioWebBridge owner = projectionOwner.get(); projectionOwner.clear();
        if (owner == null || owner.closed || owner.pending == null) return;
        if (result == Activity.RESULT_OK && consent != null) owner.launch(result, consent);
        else owner.fail("Audio sharing was cancelled");
    }
    private void launch(int result, Intent consent) {
        if (closed || pending == null) return;
        if (!AudioReactiveService.isAvailable()) {
            main.postDelayed(() -> launch(result, consent), 100);
            return;
        }
        Intent intent = new Intent(activity, AudioReactiveService.class).putExtra("host", host)
            .putExtra("session", session).putExtra("source", pending.optString("source"))
            .putExtra("sensitivity", pending.optInt("sensitivity", 130)).putExtra("smoothing", pending.optInt("smoothing", 25))
            .putExtra("result", result).putExtra("consent", consent);
        pending = null;
        try {
            if (Build.VERSION.SDK_INT >= 26) activity.startForegroundService(intent);
            else activity.startService(intent);
        } catch (Exception e) { fail("Audio could not start. Keep AuraX Finder open and try again."); }
    }
    private void fail(String error) {
        try { emit(new JSONObject().put("type", "state").put("session", session).put("active", false).put("error", error).toString()); }
        catch (Exception ignored) {}
        AudioReactiveService.stopSession(session, true);
        session = ""; pending = null;
    }
    private void emit(String json) {
        if (!closed) web.evaluateJavascript("window.dispatchEvent(new CustomEvent('aurax-audio',{detail:" + json + "}))", null);
    }
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (closed) return;
            String state = AudioReactiveService.state();
            if (!state.equals(lastState)) {
                lastState = state; emit(state);
                try {
                    JSONObject data = new JSONObject(state);
                    if (session.equals(data.optString("session")) && !data.optBoolean("active") && pending == null) session = "";
                } catch (Exception ignored) {}
            }
            main.postDelayed(this, 100);
        }
    };
    void onBackground() { if (pending == null) AudioReactiveService.stopMicrophone(); }
    void onNavigation() {
        pending = null; AudioReactiveService.stopSession(session, true); session = "";
    }
    void close() {
        onNavigation(); closed = true; main.removeCallbacksAndMessages(null);
        if (WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) WebViewCompat.removeWebMessageListener(web, "AuraXAudio");
    }
}
