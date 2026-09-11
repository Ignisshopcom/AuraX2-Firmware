package com.aurax.finder;

/** Preserve short onsets between sends, but never replay a stale capture backlog. */
final class AudioFrameAccumulator {
    static final int SEND_INTERVAL_MS = 10;
    static final class Frame {
        final int[] levels, bands;
        final long captured;
        Frame(int[] levels, int[] bands, long captured) {
            this.levels = levels; this.bands = bands; this.captured = captured;
        }
    }
    private int[] levels = new int[5], bands = new int[64];
    private long first, captured;
    private boolean pending;
    void offer(int[] next, int[] spectrum, long now) {
        if (!pending || now - first > 30) {
            clear();
            first = now;
        }
        for (int i = 0; i < 5; i++) levels[i] = Math.max(levels[i], next[i]);
        for (int i = 0; i < 64; i++) bands[i] = Math.max(bands[i], spectrum[i]);
        captured = now; pending = true;
    }
    Frame take() {
        if (!pending) return null;
        Frame frame = new Frame(levels, bands, captured);
        clear();
        return frame;
    }
    void clear() {
        levels = new int[5]; bands = new int[64]; pending = false;
    }
}
