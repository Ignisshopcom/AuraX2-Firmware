package com.aurax.finder;

import org.junit.Test;
import static org.junit.Assert.*;
import java.net.*;
import java.nio.*;

public class AudioStreamClientTest {
    @Test public void sendsWithoutWaitingForAckAndBoundsFrames() throws Exception {
        try (DatagramSocket receiver = new DatagramSocket(0);
             AudioStreamClient client = new AudioStreamClient("127.0.0.1", receiver.getLocalPort(),
                 "00112233445566778899aabbccddeeff", 1000)) {
            receiver.setSoTimeout(1000);
            int[] bands = new int[64]; bands[3] = 123; bands[63] = 999;
            client.send(new int[]{256, -1, 2, 3, 255}, bands, 1000, 1000);
            byte[] bytes = new byte[200];
            DatagramPacket packet = new DatagramPacket(bytes, bytes.length);
            receiver.receive(packet);
            assertEquals(98, packet.getLength());
            assertEquals("AXA2", new String(bytes, 0, 4, "US-ASCII"));
            assertEquals(1, ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).getInt(20));
            assertEquals(255, bytes[28] & 255);
            assertEquals(0, bytes[29] & 255);
            assertEquals(64, bytes[33]);
            assertEquals(123, bytes[37]);
            assertEquals(255, bytes[97] & 255);
            bytes[2] = 'K';
            receiver.send(new DatagramPacket(bytes, 28, packet.getSocketAddress()));
            client.send(new int[5], bands, 1017, 1017);
            assertTrue(client.roundTripMs() >= 0);
            try { client.send(new int[5], bands, 3000, 3000); fail("Missing ACK must stop capture"); }
            catch (java.io.IOException expected) {}
        }
    }
    @Test public void staleCaptureAndInvalidOwnersAreRejected() throws Exception {
        try (AudioStreamClient client = new AudioStreamClient("127.0.0.1", "0".repeat(32), 1000)) {
            try { client.send(new int[5], new int[64], 1000, 1200); fail("Old audio"); }
            catch (java.io.IOException expected) {}
        }
        try { new AudioStreamClient("127.0.0.1", "wrong", 1000); fail(); }
        catch (IllegalArgumentException expected) {}
    }
}
