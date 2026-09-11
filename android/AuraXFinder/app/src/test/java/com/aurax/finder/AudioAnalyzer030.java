package com.aurax.finder;

import org.jtransforms.fft.FloatFFT_1D;

/** Overlapping spectral analysis. Only envelopes leave the phone, never PCM. */
final class AudioAnalyzer030 {
    static final int SIZE = 4096, HOP = 512, BANDS = 64;
    private final FloatFFT_1D fft = new FloatFFT_1D(SIZE);
    private final float[] spectrum = new float[SIZE], history = new float[SIZE], window = new float[SIZE];
    private final double[] power = new double[SIZE / 2], filtered = new double[BANDS];
    private final double[] centers = new double[BANDS], edges = new double[BANDS + 2];
    private final double[] smoothed = new double[4];
    private final int sampleRate;
    private int cursor;
    private double spectralPeak = 0.025, rmsPeak = 0.12, bassAverage, fluxAverage;
    private long lastBeat = -1000;
    private final int[] bands = new int[BANDS];

    AudioAnalyzer030(int sampleRate) {
        this.sampleRate = sampleRate;
        for (int i = 0; i < SIZE; ++i) window[i] = (float)(0.5 - 0.5 * Math.cos(2 * Math.PI * i / (SIZE - 1)));
        double low = mel(30), high = mel(Math.min(16000, sampleRate * 0.45));
        for (int i = 0; i < edges.length; i++) edges[i] = hz(low + (high - low) * i / (BANDS + 1));
        for (int i = 0; i < BANDS; i++) centers[i] = edges[i + 1];
    }

    int[] spectrumBands() { return bands.clone(); }
    double centerHz(int band) { return centers[band]; }

    int[] analyze(short[] pcm, int sensitivity, int smoothing, long nowMs) {
        double squares = 0;
        for (short value : pcm) {
            float sample = value / 32768f;
            squares += sample * sample;
            history[cursor] = sample;
            cursor = (cursor + 1) % SIZE;
        }
        for (int i = 0; i < SIZE; i++) spectrum[i] = history[(cursor + i) % SIZE] * window[i];
        fft.realForward(spectrum);
        for (int k = 1; k < power.length; k++) {
            double re = spectrum[2 * k], im = spectrum[2 * k + 1];
            power[k] = (re * re + im * im) * 8.0 / (SIZE * (double)SIZE);
        }
        double dt = pcm.length / (double)sampleRate;
        double rms = Math.sqrt(squares / Math.max(1, pcm.length));
        double gain = Math.max(50, Math.min(300, sensitivity)) / 130.0;
        rmsPeak = Math.max(0.08, Math.max(rms, rmsPeak * Math.exp(-dt / 2)));
        double[] raw = new double[BANDS];
        double peak = 0;
        for (int b = 0; b < BANDS; b++) {
            int first = Math.max(1, (int)Math.floor(edges[b] * SIZE / sampleRate));
            int last = Math.min(power.length - 1, (int)Math.ceil(edges[b + 2] * SIZE / sampleRate));
            double energy = 0;
            for (int k = first; k <= last; k++) {
                double f = k * (double)sampleRate / SIZE;
                double weight = f < centers[b] ? (f - edges[b]) / (centers[b] - edges[b])
                    : (edges[b + 2] - f) / (edges[b + 2] - centers[b]);
                energy += power[k] * Math.max(0, weight);
            }
            // Mild spectral tilt prevents bass from hiding the higher harmonics.
            raw[b] = Math.sqrt(energy) * Math.min(3, Math.max(0.7, Math.sqrt(centers[b] / 250)));
            peak = Math.max(peak, raw[b]);
        }
        spectralPeak = Math.max(0.012, Math.max(peak, spectralPeak * Math.exp(-dt / 2)));
        double release = smoothing == 0 ? 0 : Math.exp(-dt / (0.015 + Math.min(95, smoothing) * 0.002));
        double flux = 0;
        for (int b = 0; b < BANDS; b++) {
            double target = rms < 0.0005 ? 0 : normalized(Math.max(0, raw[b] - 0.0004), spectralPeak, gain);
            flux += Math.max(0, target - filtered[b]);
            filtered[b] = target >= filtered[b] ? target : release * filtered[b] + (1 - release) * target;
            bands[b] = byteLevel(filtered[b]);
        }
        flux /= BANDS;
        double bass = band(35, 250), mid = band(250, 2000), treble = band(2000, 16000);
        double[] values = {normalized(Math.max(0, rms - 0.0005), rmsPeak, gain),
            normalized(bass, Math.max(0.012, spectralPeak), gain),
            normalized(mid, Math.max(0.012, spectralPeak), gain),
            normalized(treble, Math.max(0.012, spectralPeak), gain)};
        boolean beat = rms > 0.008 && nowMs - lastBeat > 140 &&
            ((bass > Math.max(0.008, bassAverage * 1.6)) || flux > Math.max(0.035, fluxAverage * 1.8));
        double baselineRelease = Math.exp(-dt / 0.35);
        bassAverage = bassAverage * baselineRelease + bass * (1 - baselineRelease);
        fluxAverage = fluxAverage * baselineRelease + flux * (1 - baselineRelease);
        if (beat) lastBeat = nowMs;
        int[] result = new int[5];
        for (int i = 0; i < 4; i++) {
            if (rms < 0.0005) values[i] = 0;
            smoothed[i] = values[i] >= smoothed[i] ? values[i] : release * smoothed[i] + (1 - release) * values[i];
            result[i] = byteLevel(smoothed[i]);
        }
        result[4] = beat ? 255 : 0;
        return result;
    }

    private double band(int low, int high) {
        double energy = 0;
        int first = Math.max(1, (int)Math.ceil(low * (double)SIZE / sampleRate));
        int last = Math.min(power.length - 1, (int)Math.floor(high * (double)SIZE / sampleRate));
        for (int k = first; k <= last; k++) energy += power[k];
        return Math.sqrt(energy);
    }
    private static double normalized(double v, double reference, double gain) {
        return Math.min(1, Math.log1p(5 * v / reference * gain) / Math.log(9));
    }
    private static int byteLevel(double v) { return Math.max(0, Math.min(255, (int)Math.round(v * 255))); }
    private static double mel(double f) { return 2595 * Math.log10(1 + f / 700); }
    private static double hz(double m) { return 700 * (Math.pow(10, m / 2595) - 1); }

    static String encode(int[] levels) {
        StringBuilder frame = new StringBuilder("01");
        for (int value : levels) {
            int v = Math.max(0, Math.min(255, value));
            frame.append("0123456789abcdef".charAt(v >> 4)).append("0123456789abcdef".charAt(v & 15));
        }
        return frame.toString();
    }
}
