package com.aurax.finder;
import org.junit.Test;
import static org.junit.Assert.*;

public class AudioFrameAccumulatorTest {
    @Test public void shortNotesSurviveUntilNextSendWithoutReplay() {
        AudioFrameAccumulator a = new AudioFrameAccumulator();
        int[] spectrum = new int[64]; spectrum[30] = 190;
        a.offer(new int[]{100, 0, 0, 160, 255}, spectrum, 1000);
        a.offer(new int[5], new int[64], 1006);
        AudioFrameAccumulator.Frame frame = a.take();
        assertEquals(190, frame.bands[30]); assertEquals(160, frame.levels[3]);
        assertEquals(255, frame.levels[4]); assertEquals(1006, frame.captured);
        assertNull(a.take());
        a.offer(new int[5], new int[64], 1012);
        assertArrayEquals(new int[64], a.take().bands);
    }
    @Test public void stalePeakAndSettingsChangesClearPendingValues() {
        AudioFrameAccumulator a = new AudioFrameAccumulator();
        a.offer(new int[]{255,255,255,255,255}, new int[64], 1000);
        a.offer(new int[5], new int[64], 1100);
        assertArrayEquals(new int[5], a.take().levels);
        a.offer(new int[]{255,255,255,255,255}, new int[64], 1110);
        a.clear(); assertNull(a.take());
    }
    @Test public void bounded100HzCadencePreservesBursts() {
        assertEquals(10, AudioFrameAccumulator.SEND_INTERVAL_MS);
        AudioFrameAccumulator a = new AudioFrameAccumulator();
        int sent = 0;
        for (int ms = 0; ms < 1000; ms++) {
            if (ms % 6 == 0) a.offer(new int[5], new int[64], ms);
            if (ms % AudioFrameAccumulator.SEND_INTERVAL_MS == 0 && a.take() != null) sent++;
        }
        assertEquals(100, sent);
    }
}
