package com.aurax.finder;

import org.jtransforms.fft.FloatFFT_1D;

/** Short overlapping spectrum plus independent low-latency crossover envelopes. */
final class AudioAnalyzer {
    static final int SIZE = 2048, HOP = 256, BANDS = 64;
    private final FloatFFT_1D fft = new FloatFFT_1D(SIZE);
    private final float[] spectrum = new float[SIZE], history = new float[SIZE], window = new float[SIZE];
    private final double[] power = new double[SIZE / 2], filtered = new double[BANDS], raw = new double[BANDS];
    private final double[] centers = new double[BANDS], edges = new double[BANDS + 2], smoothed = new double[4];
    private final int sampleRate;
    private final Bandpass[] crossovers;
    private final double[] energy = new double[4];
    private final int[] bands = new int[BANDS];
    private final double powerScale;
    private int cursor;
    private double reference = 0.12, bassAverage, fluxAverage;
    private long lastBeat = -1000;

    AudioAnalyzer(int sampleRate) {
        this.sampleRate = sampleRate;
        double windowEnergy = 0;
        for (int i = 0; i < SIZE; i++) {
            window[i] = (float)(0.5 - 0.5 * Math.cos(2 * Math.PI * i / (SIZE - 1)));
            windowEnergy += window[i] * window[i];
        }
        powerScale = 2 / (SIZE * windowEnergy);
        double low = mel(30), high = mel(Math.min(16000, sampleRate * 0.45));
        for (int i = 0; i < edges.length; i++) edges[i] = hz(low + (high - low) * i / (BANDS + 1));
        for (int i = 0; i < BANDS; i++) centers[i] = edges[i + 1];
        crossovers = new Bandpass[]{new Bandpass(sampleRate, 35, 250), new Bandpass(sampleRate, 250, 2000),
            new Bandpass(sampleRate, 2000, Math.min(16000, sampleRate * 0.45))};
    }
    int[] spectrumBands() { return bands.clone(); }
    double centerHz(int band) { return centers[band]; }
    static double sensitivityGain(int value) {
        return Math.pow(Math.max(0, Math.min(300, value)) / 130.0, 2);
    }
    static double releaseMs(int value) {
        return 600 * Math.pow(Math.max(0, Math.min(95, value)) / 95.0, 2);
    }

    int[] analyze(short[] pcm, int sensitivity, int smoothing, long nowMs) {
        java.util.Arrays.fill(energy, 0);
        for (short value : pcm) {
            float sample = value / 32768f;
            energy[0] += sample * sample;
            for (int b = 0; b < 3; b++) {
                double v = crossovers[b].process(sample);
                energy[b + 1] += v * v;
            }
            history[cursor] = sample;
            cursor = (cursor + 1) % SIZE;
        }
        for (int i = 0; i < SIZE; i++) spectrum[i] = history[(cursor + i) % SIZE] * window[i];
        fft.realForward(spectrum);
        for (int k = 1; k < power.length; k++) {
            double re = spectrum[2 * k], im = spectrum[2 * k + 1];
            power[k] = (re * re + im * im) * powerScale;
        }
        double dt = pcm.length / (double)sampleRate;
        double rms = Math.sqrt(energy[0] / Math.max(1, pcm.length));
        // One reference for ALL bands. Never independently promote a quiet band to full brightness.
        reference = Math.max(0.08, Math.max(rms, reference * Math.exp(-dt / 3)));
        double gain = sensitivityGain(sensitivity);
        double decayMs = releaseMs(smoothing);
        double release = decayMs == 0 ? 0 : Math.exp(-dt * 1000 / decayMs);
        boolean silence = rms < 0.0005;
        double flux = 0;
        for (int b = 0; b < BANDS; b++) {
            int first = Math.max(1, (int)Math.floor(edges[b] * SIZE / sampleRate));
            int last = Math.min(power.length - 1, (int)Math.ceil(edges[b + 2] * SIZE / sampleRate));
            double sum = 0;
            for (int k = first; k <= last; k++) {
                double f = k * (double)sampleRate / SIZE;
                double weight = f < centers[b] ? (f - edges[b]) / (centers[b] - edges[b])
                    : (edges[b + 2] - f) / (edges[b + 2] - centers[b]);
                sum += power[k] * Math.max(0, weight);
            }
            raw[b] = silence ? 0 : Math.max(0, Math.sqrt(sum) - 0.0004) / reference;
            flux += Math.max(0, raw[b] - filtered[b]);
            filtered[b] = raw[b] >= filtered[b] ? raw[b] : release * filtered[b] + (1 - release) * raw[b];
            bands[b] = byteLevel(filtered[b] * gain * 0.85);
        }
        flux /= BANDS;
        double bass = Math.sqrt(energy[1] / Math.max(1, pcm.length));
        boolean beat = gain > 0 && !silence && rms > 0.008 && nowMs - lastBeat > 140 &&
            (bass > Math.max(0.008, bassAverage * 1.6) || flux > Math.max(0.035, fluxAverage * 1.8));
        double baselineRelease = Math.exp(-dt / 0.35);
        bassAverage = bassAverage * baselineRelease + bass * (1 - baselineRelease);
        fluxAverage = fluxAverage * baselineRelease + flux * (1 - baselineRelease);
        if (beat) lastBeat = nowMs;
        int[] result = new int[5];
        for (int i = 0; i < 4; i++) {
            double value = silence ? 0 : Math.max(0, Math.sqrt(energy[i] / Math.max(1, pcm.length)) - 0.0005) / reference;
            smoothed[i] = value >= smoothed[i] ? value : release * smoothed[i] + (1 - release) * value;
            // Gain follows smoothing: reducing sensitivity takes effect in this frame, not after a long tail.
            result[i] = byteLevel(smoothed[i] * gain * 0.85);
        }
        result[4] = beat ? 255 : 0;
        return result;
    }
    static int byteLevel(double v) {
        // Linear quiet detail, then a soft knee: high gain must not flatten music at 255.
        if (!Double.isFinite(v) || v <= 0) return 0;
        if (v > 0.55) v = 0.55 + 0.45 * (1 - 0.45 / (v - 0.10));
        return Math.min(255, (int)Math.round(v * 255));
    }
    private static double mel(double f) { return 2595 * Math.log10(1 + f / 700); }
    private static double hz(double m) { return 700 * (Math.pow(10, m / 2595) - 1); }

    // RBJ bilinear-transform low/high-pass biquads; Butterworth fourth-order Q pairs.
    // Formula reference: https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
    private static final class Biquad {
        final double b0, b1, b2, a1, a2;
        double z1, z2;
        Biquad(int rate, double frequency, double q, boolean high) {
            double w = 2 * Math.PI * frequency / rate, c = Math.cos(w), alpha = Math.sin(w) / (2 * q);
            double denominator = 1 + alpha;
            b0 = (high ? 1 + c : 1 - c) / (2 * denominator);
            b1 = (high ? -(1 + c) : 1 - c) / denominator;
            b2 = b0; a1 = -2 * c / denominator; a2 = (1 - alpha) / denominator;
        }
        double process(double x) {
            double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    }
    private static final class Bandpass {
        final Biquad[] filters;
        Bandpass(int rate, double low, double high) {
            filters = new Biquad[]{new Biquad(rate, low, 0.5411961, true), new Biquad(rate, low, 1.30656296, true),
                new Biquad(rate, high, 0.5411961, false), new Biquad(rate, high, 1.30656296, false)};
        }
        double process(double x) {
            for (Biquad filter : filters) x = filter.process(x);
            return x;
        }
    }
    static String encode(int[] levels) {
        StringBuilder frame = new StringBuilder("01");
        for (int value : levels) {
            int v = Math.max(0, Math.min(255, value));
            frame.append("0123456789abcdef".charAt(v >> 4)).append("0123456789abcdef".charAt(v & 15));
        }
        return frame.toString();
    }
}
