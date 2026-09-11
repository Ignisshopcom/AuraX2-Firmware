package com.aurax.finder;

/** Counts completed work, not timer ticks; safe across capture and UI threads. */
final class AudioRateMeter {
    private long start = -1, last = -1;
    private int count;
    private double rate;
    synchronized void mark(long now) {
        if (start < 0) start = now;
        else ++count;
        last = now;
        if (now - start >= 1000) {
            rate = count * 1000.0 / (now - start);
            start = now;
            count = 0;
        }
    }
    synchronized double perSecond(long now) {
        return last < 0 || now - last >= 1000 ? 0 : rate;
    }
}
