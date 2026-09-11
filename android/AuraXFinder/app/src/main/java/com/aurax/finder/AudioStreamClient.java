package com.aurax.finder;

import java.io.IOException;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.nio.channels.DatagramChannel;

/** Unicast envelope stream. There is deliberately no resend queue. */
final class AudioStreamClient implements AutoCloseable {
    static final int PORT = 4211, BYTES = 98;
    private final DatagramChannel channel;
    private final byte[] token = new byte[16], bytes = new byte[BYTES], ack = new byte[29];
    private final ByteBuffer incoming = ByteBuffer.wrap(ack).order(ByteOrder.LITTLE_ENDIAN);
    private final ByteBuffer buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    private int sequence;
    private long lastAckMs;
    private int roundTripMs = -1;

    AudioStreamClient(String host, String session, long now) throws IOException {
        this(host, PORT, session, now);
    }
    AudioStreamClient(String host, int port, String session, long now) throws IOException {
        if (!session.matches("[0-9a-f]{32}")) throw new IllegalArgumentException("Invalid audio owner");
        for (int i = 0; i < 16; i++) token[i] = (byte)Integer.parseInt(session.substring(i * 2, i * 2 + 2), 16);
        channel = DatagramChannel.open();
        try {
            channel.configureBlocking(false);
            channel.connect(new InetSocketAddress(InetAddress.getByName(host), port));
            channel.socket().setSendBufferSize(4096);
        } catch (IOException | RuntimeException error) { channel.close(); throw error; }
        bytes[0] = 'A'; bytes[1] = 'X'; bytes[2] = 'A'; bytes[3] = '2';
        System.arraycopy(token, 0, bytes, 4, 16);
        bytes[33] = 64;
        lastAckMs = now;
    }
    boolean send(int[] levels, int[] bands, long captureMs, long now) throws IOException {
        if (levels.length != 5 || bands.length != 64) throw new IllegalArgumentException("Invalid spectrum size");
        if (now - captureMs > 100) throw new IOException("Audio capture fell behind");
        readAcks(now);
        if (now - lastAckMs > 1300) throw new IOException("AuraX stopped acknowledging audio");
        buffer.putInt(20, ++sequence);
        buffer.putInt(24, (int)captureMs);
        for (int i = 0; i < 5; i++) bytes[28 + i] = clamp(levels[i]);
        for (int i = 0; i < 64; i++) bytes[34 + i] = clamp(bands[i]);
        buffer.rewind();
        // A busy socket drops this frame. Never wait for an ACK or queue stale audio.
        return channel.write(buffer) == BYTES;
    }
    private void readAcks(long now) throws IOException {
        for (int i = 0; i < 8; i++) {
            incoming.clear();
            int count = channel.read(incoming);
            if (count == 0) break;
            if (count != 28 || ack[0] != 'A' || ack[1] != 'X' || ack[2] != 'K' || ack[3] != '2' ||
                !Arrays.equals(token, Arrays.copyOfRange(ack, 4, 20))) continue;
            int behind = sequence - incoming.getInt(20);
            int elapsed = (int)now - incoming.getInt(24);
            if (behind >= 0 && behind < 120 && elapsed >= 0 && elapsed < 1300) {
                lastAckMs = now;
                roundTripMs = elapsed;
            }
        }
    }
    int roundTripMs() { return roundTripMs; }
    private static byte clamp(int value) { return (byte)Math.max(0, Math.min(255, value)); }
    @Override public void close() { try { channel.close(); } catch (IOException ignored) {} }
}
