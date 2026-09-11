package com.aurax.finder;

import org.junit.Test;
import static org.junit.Assert.*;

public class AudioAnalyzerTest {
    private short[] tone(int count, int offset, double hz, double amplitude) {
        short[] data = new short[count];
        for (int i = 0; i < count; i++) data[i] = (short)(Math.sin(2 * Math.PI * hz * (i + offset) / 44100) * amplitude * 32767);
        return data;
    }
    private int[] steady(AudioAnalyzer a, double hz, double amplitude) {
        int[] result = null;
        for (int i = 0; i < 16; i++) result = a.analyze(tone(AudioAnalyzer.HOP, i * AudioAnalyzer.HOP, hz, amplitude), 130, 0, Math.round(i * AudioAnalyzer.HOP * 1000.0 / 44100));
        return result;
    }
    private int peak(int[] bands) {
        int p = 0;
        for (int i = 1; i < bands.length; i++) if (bands[i] > bands[p]) p = i;
        return p;
    }
    @Test public void highGainKeepsDynamicsInsteadOfClippingEveryBand() {
        assertEquals(0, AudioAnalyzer.byteLevel(0));
        assertEquals(0, AudioAnalyzer.byteLevel(Double.NaN));
        assertEquals(51, AudioAnalyzer.byteLevel(0.2)); // Quiet detail is unchanged.
        int previous = 0;
        for (double level : new double[]{0.1, 0.3, 0.55, 0.85, 1, 1.5, 2, 3, 4.5}) {
            int value = AudioAnalyzer.byteLevel(level);
            assertTrue(value > previous && value < 255);
            previous = value;
        }
        AudioAnalyzer a = new AudioAnalyzer(44100);
        steady(a, 1000, 0.7); // Establish a shared loud reference, then vary the music.
        int[] outputs = new int[3];
        for (int j = 0; j < 3; j++) {
            int[] result = null;
            for (int i = 0; i < 12; i++)
                result = a.analyze(tone(AudioAnalyzer.HOP, i * AudioAnalyzer.HOP, 1000, 0.10 + j * 0.12), 300, 0, 1000 + j * 100 + i * 6);
            outputs[j] = result[0];
        }
        System.out.println("High-gain musical dynamics: " + java.util.Arrays.toString(outputs));
        assertTrue(outputs[1] > outputs[0] + 10);
        assertTrue(outputs[2] > outputs[1] + 5);
        assertTrue(outputs[2] < 255);
    }
    @Test public void silenceAndLegacyEncoding() {
        AudioAnalyzer a = new AudioAnalyzer(44100);
        assertArrayEquals(new int[5], steady(a, 100, 0));
        assertArrayEquals(new int[64], a.spectrumBands());
        assertEquals("0100ff0100ff", AudioAnalyzer.encode(new int[]{-1, 300, 1, 0, 255}));
    }
    @Test public void broadBandsAreSeparated() {
        for (int band = 1; band <= 3; band++) {
            AudioAnalyzer a = new AudioAnalyzer(44100);
            int[] levels = steady(a, new int[]{0, 100, 1000, 5000}[band], 0.4);
            assertTrue(levels[0] > 100);
            assertTrue("band " + band, levels[band] > 100);
            for (int other = 1; other <= 3; other++) if (other != band) assertTrue(levels[other] < 20);
        }
    }
    @Test public void nearbyNotesMoveAcrossRealSpectrum() {
        int previous = -1;
        for (double hz : new double[]{100, 200, 440, 800, 1000, 1400, 2400, 5000, 10000}) {
            AudioAnalyzer a = new AudioAnalyzer(44100);
            steady(a, hz, 0.2);
            int bin = peak(a.spectrumBands());
            assertTrue("frequency " + hz + " bin " + bin, bin > previous);
            assertTrue(Math.abs(a.centerHz(bin) - hz) < Math.max(60, hz * 0.12));
            previous = bin;
        }
    }
    @Test public void simultaneousTonesRemainSeparated() {
        AudioAnalyzer a = new AudioAnalyzer(44100);
        for (int frame = 0; frame < 16; frame++) {
            short[] low = tone(AudioAnalyzer.HOP, frame * AudioAnalyzer.HOP, 440, 0.2);
            short[] high = tone(AudioAnalyzer.HOP, frame * AudioAnalyzer.HOP, 2400, 0.1);
            for (int i = 0; i < low.length; i++) low[i] += high[i];
            a.analyze(low, 130, 0, Math.round(frame * AudioAnalyzer.HOP * 1000.0 / 44100));
        }
        int[] spectrum = a.spectrumBands();
        int lowPeak = 0, highPeak = 0, valley = 255;
        for (int i = 0; i < 64; i++) {
            if (Math.abs(a.centerHz(i) - 440) < 100) lowPeak = Math.max(lowPeak, spectrum[i]);
            if (Math.abs(a.centerHz(i) - 2400) < 300) highPeak = Math.max(highPeak, spectrum[i]);
            if (a.centerHz(i) > 900 && a.centerHz(i) < 1300) valley = Math.min(valley, spectrum[i]);
        }
        assertTrue("low peak " + lowPeak, lowPeak > 130);
        assertTrue("high peak " + highPeak, highPeak > 45 && highPeak < lowPeak * 0.8);
        // A half-amplitude tone must not be independently amplified to the louder tone's level.
        assertTrue(valley < Math.min(lowPeak, highPeak) / 4);
    }
    @Test public void onsetAndVolumeDoNotWaitForAFullFftWindow() {
        AudioAnalyzer a = new AudioAnalyzer(44100);
        steady(a, 100, 0);
        int[] first = a.analyze(tone(AudioAnalyzer.HOP, 0, 1000, 0.4), 130, 90, 1000);
        assertTrue(first[0] > 180); // One 5.8 ms capture hop, even at high smoothing.
        int[] released = a.analyze(new short[AudioAnalyzer.HOP], 130, 90, 1012);
        assertTrue(released[0] > 0 && released[0] < first[0]);
        assertEquals(0, a.analyze(new short[AudioAnalyzer.HOP], 130, 0, 1024)[0]);
    }
    @Test public void gainDoesNotTurnSilenceIntoLight() {
        AudioAnalyzer a = new AudioAnalyzer(44100);
        steady(a, 400, 0.7);
        for (int i = 0; i < 1000; i++) a.analyze(new short[AudioAnalyzer.HOP], 300, 95, Math.round(i * AudioAnalyzer.HOP * 1000.0 / 44100) + 1000);
        assertArrayEquals(new int[64], a.spectrumBands());
    }
    @Test public void sensitivityChangesTheEnvelope() {
        short[] data = tone(AudioAnalyzer.SIZE, 0, 1000, 0.02);
        int low = new AudioAnalyzer(44100).analyze(data, 50, 0, 1000)[0];
        int high = new AudioAnalyzer(44100).analyze(data, 300, 0, 1000)[0];
        assertTrue(high > low * 15);
    }

    @Test public void sharedReferencePreservesMixedBandDynamics() {
        AudioAnalyzer a = new AudioAnalyzer(44100);
        int[] levels = null;
        for (int frame = 0; frame < 48; frame++) {
            short[] pcm = tone(AudioAnalyzer.HOP, frame * AudioAnalyzer.HOP, 100, 0.24);
            short[] mid = tone(AudioAnalyzer.HOP, frame * AudioAnalyzer.HOP, 1000, 0.08);
            short[] high = tone(AudioAnalyzer.HOP, frame * AudioAnalyzer.HOP, 5000, 0.024);
            for (int i = 0; i < pcm.length; i++) pcm[i] += mid[i] + high[i];
            levels = a.analyze(pcm, 130, 25, Math.round(frame * AudioAnalyzer.HOP * 1000.0 / 44100));
        }
        System.out.println("Mixed bass/mids/treble: " + java.util.Arrays.toString(levels));
        assertTrue(levels[1] > levels[2] * 2);
        assertTrue(levels[2] > levels[3] * 2);
        assertTrue(levels[0] > levels[2] * 2);
        assertTrue(levels[3] > 5);
    }
    @Test public void controlsHaveExplicitRangeAndGainChangesImmediately() {
        assertEquals(0, AudioAnalyzer.sensitivityGain(0), 0);
        assertEquals(1, AudioAnalyzer.sensitivityGain(130), 0);
        assertTrue(AudioAnalyzer.sensitivityGain(300) > 5.3);
        assertEquals(0, AudioAnalyzer.releaseMs(0), 0);
        assertEquals(600, AudioAnalyzer.releaseMs(95), 0);
        AudioAnalyzer a = new AudioAnalyzer(44100);
        steady(a, 1000, 0.2);
        assertArrayEquals(new int[5], a.analyze(tone(AudioAnalyzer.HOP, 0, 1000, 0.2), 0, 95, 1000));
        assertArrayEquals(new int[64], a.spectrumBands());
        assertTrue(a.analyze(tone(AudioAnalyzer.HOP, AudioAnalyzer.HOP, 1000, 0.2), 130, 95, 1006)[0] > 150);
    }
    @Test public void smoothingChangesReleaseWithoutChangingAttack() {
        int[] released = new int[3];
        int[] smooth = {0, 25, 95};
        for (int index = 0; index < smooth.length; index++) {
            AudioAnalyzer a = new AudioAnalyzer(44100);
            int attack = a.analyze(tone(AudioAnalyzer.HOP, 0, 1000, 0.4), 130, smooth[index], 0)[0];
            assertTrue(attack > 180);
            for (int frame = 1; frame <= 35; frame++)
                released[index] = a.analyze(new short[AudioAnalyzer.HOP], 130, smooth[index], frame * 6)[0];
        }
        System.out.println("Release after 203 ms at smoothing 0/25/95: " + java.util.Arrays.toString(released));
        assertEquals(0, released[0]);
        assertTrue(released[1] < 5);
        assertTrue(released[2] > 130 && released[2] < 180);
    }
    @Test public void crossoverOnsetDoesNotRegressAgainst030() {
        boolean faster = false;
        for (int band = 1; band <= 3; band++) {
            double hz = new int[]{0, 100, 1000, 5000}[band];
            AudioAnalyzer current = new AudioAnalyzer(44100);
            AudioAnalyzer030 old = new AudioAnalyzer030(44100);
            double currentMs = -1, oldMs = -1;
            for (int frame = 0; frame < 32; frame++) {
                int[] next = current.analyze(tone(AudioAnalyzer.HOP, frame * AudioAnalyzer.HOP, hz, 0.4), 130, 0, frame * 6);
                if (currentMs < 0 && next[band] >= 110) currentMs = (frame + 1) * AudioAnalyzer.HOP * 1000.0 / 44100;
                int[] before = old.analyze(tone(AudioAnalyzer030.HOP, frame * AudioAnalyzer030.HOP, hz, 0.4), 130, 0, frame * 12);
                if (oldMs < 0 && before[band] >= 110) oldMs = (frame + 1) * AudioAnalyzer030.HOP * 1000.0 / 44100;
            }
            System.out.println("DSP onset " + hz + " Hz: 0.3.0=" + oldMs + " ms, current=" + currentMs + " ms");
            assertTrue(currentMs > 0 && currentMs <= 18);
            assertTrue(currentMs <= oldMs);
            faster |= currentMs < oldMs;
        }
        assertTrue(faster);
    }
}
