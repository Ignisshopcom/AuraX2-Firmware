package com.aurax.finder;

import org.junit.Test;
import static org.junit.Assert.*;

public class AudioRateMeterTest {
    @Test public void reportsRealCadenceAndExpiresWhenStopped() {
        AudioRateMeter rate = new AudioRateMeter();
        assertEquals(0, rate.perSecond(0), 0);
        for (int t = 0; t <= 2000; t += 20) rate.mark(t);
        assertEquals(50, rate.perSecond(2000), 0.01);
        assertEquals(0, rate.perSecond(3000), 0);
    }
    @Test public void batchedCaptureIsNotConfusedWithPacketCadence() {
        AudioRateMeter analysis = new AudioRateMeter(), sent = new AudioRateMeter();
        for (int t = 0; t <= 2000; t += 20) {
            for (int hop = 0; hop < 4; hop++) analysis.mark(t);
            sent.mark(t);
        }
        assertEquals(200, analysis.perSecond(2000), 0.01);
        assertEquals(50, sent.perSecond(2000), 0.01);
    }
}
