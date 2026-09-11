package com.aurax.finder;

import android.app.*;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.media.*;
import android.media.projection.*;
import android.os.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import org.json.JSONObject;
import org.json.JSONArray;

public final class AudioReactiveService extends Service {
    private static final int NOTIFICATION = 4211;
    private static final String CHANNEL = "aurax-audio";
    private static AudioReactiveService current;
    private static String cancelledSession = "";
    private static volatile String stateJson = "{}";
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ScheduledExecutorService network = Executors.newSingleThreadScheduledExecutor();
    private final Object levelsLock = new Object();
    private final AudioFrameAccumulator pendingFrames = new AudioFrameAccumulator();
    private final AudioRateMeter analysisRate = new AudioRateMeter(), sentRate = new AudioRateMeter();
    private volatile boolean stopped, active;
    private volatile int sensitivity = 130, smoothing = 25;
    private long lastSentMs = -1000;
    private int[] levels = new int[5], bands = new int[64];
    private long captureMs, lastPublishMs;
    private volatile AudioStreamClient stream;
    private boolean detailed;
    private long lastBeatMs = -1000;
    private int failures;
    private String session = "", host = "", source = "";
    private AudioRecord recorder;
    private MediaProjection projection;
    private PowerManager.WakeLock wakeLock;
    private boolean foreground;
    private boolean captureThreadStarted;

    static String state() { return stateJson; }
    static boolean isAvailable() { return current == null; }
    static void settings(String session, int sensitivity, int smoothing) {
        if (current != null && current.session.equals(session)) {
            current.sensitivity = Math.max(0, Math.min(300, sensitivity));
            current.smoothing = Math.max(0, Math.min(95, smoothing));
            synchronized (current.levelsLock) { current.pendingFrames.clear(); }
        }
    }
    static void stopSession(String session, boolean restore) {
        cancelledSession = session;
        if (current != null && current.session.equals(session)) current.finish(restore, "");
    }
    static void stopMicrophone() {
        if (current != null && "microphone".equals(current.source)) current.finish(true, "");
    }

    @Override public IBinder onBind(Intent intent) { return null; }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || "stop".equals(intent.getAction())) { finish(true, ""); return START_NOT_STICKY; }
        if (!session.isEmpty()) return START_NOT_STICKY;
        session = intent.getStringExtra("session");
        if (session == null) session = "";
        host = intent.getStringExtra("host");
        source = intent.getStringExtra("source");
        if (!session.matches("[0-9a-f]{32}") || session.equals(cancelledSession) || !AudioWebBridge.isLocalIp(host)) {
            stopSelf(); return START_NOT_STICKY;
        }
        current = this;
        settings(session, intent.getIntExtra("sensitivity", 130), intent.getIntExtra("smoothing", 25));
        try {
            if (checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED)
                throw new SecurityException("Audio permission was revoked");
            startNotification();
            wakeLock = ((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AuraX:Audio");
            wakeLock.acquire(2 * 60 * 60 * 1000L);
            main.postDelayed(() -> finish(true, "Audio session ended after two hours."), 2 * 60 * 60 * 1000L);
            AudioFormat format = new AudioFormat.Builder().setSampleRate(44100)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_IN_MONO).build();
            int buffer = Math.max(AudioAnalyzer.HOP * 4, AudioRecord.getMinBufferSize(44100, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT));
            AudioRecord.Builder builder = new AudioRecord.Builder().setAudioFormat(format).setBufferSizeInBytes(buffer);
            if ("playback".equals(source)) {
                if (Build.VERSION.SDK_INT < 29) throw new IllegalStateException("Device audio requires Android 10 or later");
                MediaProjectionManager manager = (MediaProjectionManager)getSystemService(MEDIA_PROJECTION_SERVICE);
                projection = manager.getMediaProjection(intent.getIntExtra("result", 0), intent.getParcelableExtra("consent"));
                if (projection == null) throw new IllegalStateException("Device audio permission was not granted");
                projection.registerCallback(new MediaProjection.Callback() {
                    @Override public void onStop() { finish(true, "Audio sharing ended"); }
                }, main);
                builder.setAudioPlaybackCaptureConfig(new AudioPlaybackCaptureConfiguration.Builder(projection)
                    .addMatchingUsage(AudioAttributes.USAGE_MEDIA).addMatchingUsage(AudioAttributes.USAGE_GAME)
                    .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN).build());
            } else if ("microphone".equals(source)) {
                builder.setAudioSource(MediaRecorder.AudioSource.VOICE_RECOGNITION);
            } else throw new IllegalArgumentException("Unknown audio source");
            recorder = builder.build();
            if (recorder.getState() != AudioRecord.STATE_INITIALIZED) throw new IllegalStateException("Audio input unavailable");
            recorder.startRecording();
            AudioRecord input = recorder;
            new Thread(() -> capture(input), "aurax-audio-analysis").start();
            captureThreadStarted = true;
            network.execute(() -> {
                try {
                    String protocol = post("start?transport=udp2", "");
                    detailed = protocol.equals("AXA2:4211:64");
                    if (detailed && !stopped) {
                        stream = new AudioStreamClient(host, session, SystemClock.elapsedRealtime());
                        if (stopped) stream.close();
                    }
                    if (!stopped) { active = true; publish(""); }
                } catch (Exception e) { main.post(() -> finish(true, "AuraX audio could not start. Check firmware and Wi-Fi.")); }
            });
            network.scheduleWithFixedDelay(this::sendLatest, 2, 2, TimeUnit.MILLISECONDS);
        } catch (Exception e) { finish(true, "Audio capture failed: " + e.getMessage()); }
        return START_NOT_STICKY;
    }

    private void capture(AudioRecord input) {
        android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO);
        AudioAnalyzer analyzer = new AudioAnalyzer(44100);
        short[] samples = new short[AudioAnalyzer.HOP];
        try {
            while (!stopped) {
                int offset = 0;
                while (offset < samples.length && !stopped) {
                    int n = input.read(samples, offset, samples.length - offset, AudioRecord.READ_BLOCKING);
                    if (n <= 0) throw new IllegalStateException("Audio input disconnected");
                    offset += n;
                }
                if (stopped) break;
                int[] values = analyzer.analyze(samples, sensitivity, smoothing, SystemClock.elapsedRealtime());
                synchronized (levelsLock) {
                    levels = values;
                    bands = analyzer.spectrumBands();
                    captureMs = SystemClock.elapsedRealtime();
                    analysisRate.mark(captureMs);
                    pendingFrames.offer(values, bands, captureMs);
                    if (values[4] != 0) lastBeatMs = SystemClock.elapsedRealtime();
                }
            }
        } catch (Exception e) {
            if (!stopped) main.post(() -> finish(true, "Audio input disconnected"));
        } finally { input.release(); }
    }

    private void sendLatest() {
        if (stopped || !active) return;
        long now = SystemClock.elapsedRealtime();
        if (now - lastSentMs < (detailed ? AudioFrameAccumulator.SEND_INTERVAL_MS : 25)) return;
        int[] frame, spectrum;
        long captured;
        synchronized (levelsLock) {
            if (captureMs == 0) return;
            if (now - captureMs > 100) {
                main.post(() -> finish(true, "Audio capture fell behind"));
                return;
            }
            AudioFrameAccumulator.Frame pending = pendingFrames.take();
            if (pending == null) return;
            frame = pending.levels; spectrum = pending.bands; captured = pending.captured;
        }
        try {
            if (detailed) {
                if (!stream.send(frame, spectrum, captured, now)) return;
            } else post("data", AudioAnalyzer.encode(frame));
            // Empty capture polls must not defer the next available frame.
            lastSentMs = now;
            sentRate.mark(SystemClock.elapsedRealtime());
            failures = 0;
            if (!stopped && now - lastPublishMs >= 80) { lastPublishMs = now; publish(""); }
        }
        catch (Exception e) {
            if (++failures >= 2 || e instanceof SessionEndedException)
                main.post(() -> finish(true, "Audio connection to AuraX ended"));
        }
    }

    private static final class SessionEndedException extends Exception {}
    private String post(String action, String body) throws Exception {
        HttpURLConnection connection = (HttpURLConnection)new URL("http://" + host + "/audio/" + action +
            (action.contains("?") ? "&" : "?") + "session=" + session).openConnection();
        try {
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(700);
            connection.setReadTimeout(700);
            connection.setRequestMethod("POST");
            connection.setRequestProperty("Content-Type", "text/plain");
            byte[] bytes = body.getBytes(StandardCharsets.US_ASCII);
            connection.setDoOutput(true);
            connection.setFixedLengthStreamingMode(bytes.length);
            try (java.io.OutputStream output = connection.getOutputStream()) { output.write(bytes); }
            int code = connection.getResponseCode();
            if (code == 409) throw new SessionEndedException();
            if (code < 200 || code >= 300) throw new java.io.IOException("HTTP " + code);
            try (java.io.InputStream input = connection.getInputStream()) {
                java.io.ByteArrayOutputStream response = new java.io.ByteArrayOutputStream();
                int value;
                while ((value = input.read()) != -1 && response.size() < 256) response.write(value);
                return response.toString("US-ASCII").trim();
            }
        } finally { connection.disconnect(); }
    }

    private void publish(String error) {
        try {
            JSONObject state = new JSONObject().put("type", "state").put("session", session)
                .put("active", active && !stopped).put("error", error)
                .put("transport", detailed ? "udp2" : "http")
                .put("roundTripMs", stream == null ? -1 : stream.roundTripMs())
                .put("analysisRate", active && !stopped ? analysisRate.perSecond(SystemClock.elapsedRealtime()) : 0)
                .put("sentRate", active && !stopped ? sentRate.perSecond(SystemClock.elapsedRealtime()) : 0);
            synchronized (levelsLock) {
                state.put("spectrum", new JSONArray(bands));
                state.put("levels", new JSONObject().put("volume", levels[0]).put("bass", levels[1])
                    .put("mid", levels[2]).put("treble", levels[3])
                    .put("beat", SystemClock.elapsedRealtime() - lastBeatMs < 120 ? 255 : 0));
            }
            stateJson = state.toString();
        } catch (Exception ignored) {}
    }

    private void startNotification() {
        NotificationManager manager = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26) manager.createNotificationChannel(new NotificationChannel(CHANNEL, "Audio Reactive", NotificationManager.IMPORTANCE_LOW));
        PendingIntent stop = PendingIntent.getService(this, 0, new Intent(this, AudioReactiveService.class).setAction("stop"), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        Notification notification = builder.setSmallIcon(android.R.drawable.ic_btn_speak_now).setContentTitle("AuraX Audio Reactive")
            .setContentText("playback".equals(source) ? "Sharing device audio with AuraX" : "Microphone active for AuraX")
            .setOngoing(true).addAction(new Notification.Action.Builder(null, "Stop", stop).build()).build();
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION, notification,
            "playback".equals(source) ? ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION :
                (Build.VERSION.SDK_INT >= 30 ? ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE : 0));
        else startForeground(NOTIFICATION, notification);
        foreground = true;
    }

    private void finish(boolean restore, String error) {
        if (stopped) return;
        stopped = true;
        active = false;
        if (stream != null) stream.close();
        if (recorder != null) {
            try { recorder.stop(); } catch (Exception ignored) {}
            if (!captureThreadStarted) recorder.release();
            recorder = null; // The capture thread owns release().
        }
        if (projection != null) { projection.stop(); projection = null; }
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        publish(error);
        if (session.matches("[0-9a-f]{32}") && AudioWebBridge.isLocalIp(host) && !network.isShutdown()) network.execute(() -> {
            try { post("stop?restore=" + (restore ? 1 : 0), ""); } catch (Exception ignored) {}
        });
        network.shutdown();
        main.removeCallbacksAndMessages(null);
        if (foreground) stopForeground(true);
        stopSelf();
    }
    @Override public void onTaskRemoved(Intent intent) { finish(true, ""); super.onTaskRemoved(intent); }
    @Override public void onDestroy() {
        finish(true, "");
        if (current == this) current = null;
        super.onDestroy();
    }
}
