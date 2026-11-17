package uk.kevinkaye.keystone;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.wifi.WifiManager;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Finds Keystone on the local network (normally this phone's own hotspot) and keeps a
 * TCP link to it. No internet needed.
 *
 * Finding it, in order: the last address that worked; Keystone's UDP beacons and replies
 * to a "KEYSTONE?" probe on port 47800; and, if a network blocks broadcasts, a quick sweep
 * of every address on this phone's local networks for port 47801.
 */
final class Link {
    interface Listener {
        void onLine(String line);
        void onStatus(String status, String detail);
    }

    static final int TCP_PORT = 47801, UDP_PORT = 47800;

    private final Context context;
    private final Listener listener;
    private final LinkedBlockingQueue<String> outbox = new LinkedBlockingQueue<>();
    private volatile boolean running;
    private volatile Socket socket;
    private Thread worker;

    Link(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
    }

    void start() {
        if (running) return;
        running = true;
        worker = new Thread(this::loop, "Keystone link");
        worker.start();
    }

    void stop() {
        running = false;
        closeSocket();
        if (worker != null) worker.interrupt();
    }

    void send(String line) {
        if (socket != null) outbox.offer(line);
    }

    /** An address typed on the phone: tried first on every round until it works. */
    void connectTo(String host) {
        prefs().edit().putString("manual", host.trim()).apply();
        Thread w = worker;
        if (w != null) w.interrupt();   // wake from any sleep and try it now
    }

    private void closeSocket() {
        Socket s = socket;
        socket = null;
        if (s != null) try { s.close(); } catch (Exception ignored) { }
    }

    // the Wi-Fi network's gateway (the laptop or Pi when the phone is on Keystone's own Wi-Fi)
    @SuppressWarnings("deprecation")
    private String wifiGateway() {
        try {
            WifiManager wm = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            int g = wm == null ? 0 : wm.getDhcpInfo().gateway;
            if (g == 0) return null;
            return (g & 0xff) + "." + ((g >> 8) & 0xff) + "." + ((g >> 16) & 0xff) + "." + ((g >> 24) & 0xff);
        } catch (Exception e) {
            return null;
        }
    }

    private SharedPreferences prefs() { return context.getSharedPreferences("link", Context.MODE_PRIVATE); }

    private void loop() {
        while (running) {
            String nets = describeNetworks();
            listener.onStatus("searching", nets + " Calling Keystone...");
            InetSocketAddress target = null;
            String manual = prefs().getString("manual", null);
            if (manual != null && !manual.isEmpty() && isOwnAddress(manual)) {
                listener.onStatus("searching", nets + " " + manual + " is this phone. Type the laptop's address instead.");
                manual = null;
            }
            if (manual != null && !manual.isEmpty()) {
                target = tryConnectable(manual, TCP_PORT, 1500);
                if (target == null) listener.onStatus("searching", nets + " Nothing answered at " + manual + ".");
            }
            String last = prefs().getString("last", null);
            if (target == null && last != null) target = tryConnectable(last, TCP_PORT, 700);
            // on Keystone's own Wi-Fi the rig is the network's gateway: try it straight away
            if (target == null) {
                String gw = wifiGateway();
                if (gw != null && !gw.equals(last)) target = tryConnectable(gw, TCP_PORT, 700);
            }
            if (target == null) target = discover(3000);
            if (target == null) {
                listener.onStatus("searching", nets + " No reply to the broadcast call. Checking every address...");
                target = sweep();
            }
            if (target == null) {
                listener.onStatus("searching", nets + " No reply to the broadcast call, and nothing answered on port " + TCP_PORT
                        + " at any of " + lastSweepCount + " addresses (" + lastSweepRange + "). Trying again.");
                sleep(3000);
                continue;
            }
            prefs().edit().putString("last", target.getAddress().getHostAddress()).apply();
            runConnection(target);
        }
    }

    // ------------------------------------------------------------------ connection
    private void runConnection(InetSocketAddress target) {
        Socket s = new Socket();
        try {
            s.setTcpNoDelay(true);
            s.connect(target, 2000);
            s.setSoTimeout(7000);   // Keystone answers pings every 2 s and streams meters
            socket = s;
            outbox.clear();
            listener.onStatus("connected", target.getAddress().getHostAddress());
            final OutputStream out = s.getOutputStream();
            Thread writer = new Thread(() -> {
                try {
                    while (socket == s) {
                        String line = outbox.poll(500, TimeUnit.MILLISECONDS);
                        if (line == null) continue;
                        out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
                        out.flush();
                    }
                } catch (Exception e) {
                    closeSocket();
                }
            }, "Keystone writer");
            writer.start();
            BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8), 65536);
            String line;
            while (running && (line = in.readLine()) != null) listener.onLine(line);
        } catch (Exception ignored) {
        } finally {
            if (socket == s) closeSocket();
            try { s.close(); } catch (Exception ignored) { }
        }
    }

    // ------------------------------------------------------------------ discovery
    private InetSocketAddress discover(int timeoutMs) {
        WifiManager wifi = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
        WifiManager.MulticastLock lock = null;
        DatagramSocket udp = null;
        try {
            if (wifi != null) {
                lock = wifi.createMulticastLock("keystone");
                lock.setReferenceCounted(false);
                lock.acquire();
            }
            try {
                udp = new DatagramSocket(null);
                udp.setReuseAddress(true);
                udp.bind(new InetSocketAddress(UDP_PORT));
            } catch (Exception e) {
                udp = new DatagramSocket();   // beacons may not arrive, but probe replies will
            }
            udp.setBroadcast(true);
            udp.setSoTimeout(300);
            byte[] probe = "KEYSTONE?".getBytes(StandardCharsets.US_ASCII);
            long end = System.currentTimeMillis() + timeoutMs, nextProbe = 0;
            byte[] buf = new byte[512];
            while (running && System.currentTimeMillis() < end) {
                if (System.currentTimeMillis() >= nextProbe) {
                    nextProbe = System.currentTimeMillis() + 700;
                    for (InetAddress b : broadcastAddresses()) {
                        try { udp.send(new DatagramPacket(probe, probe.length, b, UDP_PORT)); } catch (Exception ignored) { }
                    }
                }
                try {
                    DatagramPacket p = new DatagramPacket(buf, buf.length);
                    udp.receive(p);
                    String msg = new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8);
                    if (!msg.startsWith("{")) continue;
                    JSONObject j = new JSONObject(msg);
                    if (!"keystone".equals(j.optString("t"))) continue;
                    return new InetSocketAddress(p.getAddress(), j.optInt("port", TCP_PORT));
                } catch (java.net.SocketTimeoutException ignored) {
                } catch (Exception ignored) { }
            }
        } catch (Exception ignored) {
        } finally {
            if (udp != null) udp.close();
            if (lock != null) try { lock.release(); } catch (Exception ignored) { }
        }
        return null;
    }

    private static boolean isOwnAddress(String host) {
        for (InterfaceAddress ia : localAddresses()) if (ia.getAddress().getHostAddress().equals(host)) return true;
        return false;
    }

    private static List<InetAddress> broadcastAddresses() {
        List<InetAddress> out = new ArrayList<>();
        try { out.add(InetAddress.getByName("255.255.255.255")); } catch (Exception ignored) { }
        for (InterfaceAddress ia : localAddresses()) if (ia.getBroadcast() != null) out.add(ia.getBroadcast());
        return out;
    }

    private static List<InterfaceAddress> localAddresses() {
        List<InterfaceAddress> out = new ArrayList<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                String n = ni.getName().toLowerCase();
                if (n.startsWith("rmnet") || n.startsWith("ccmni") || n.startsWith("dummy") || n.startsWith("tun")) continue;   // mobile data and VPNs
                for (InterfaceAddress ia : ni.getInterfaceAddresses())
                    if (ia.getAddress() instanceof Inet4Address) out.add(ia);
            }
        } catch (Exception ignored) { }
        return out;
    }

    private volatile int lastSweepCount = 0;
    private volatile String lastSweepRange = "none";

    /** The phone's own networks, so the screen can say where it's looking. */
    private static String describeNetworks() {
        List<String> parts = new ArrayList<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InterfaceAddress ia : ni.getInterfaceAddresses())
                    if (ia.getAddress() instanceof Inet4Address)
                        parts.add(ia.getAddress().getHostAddress() + " (" + ni.getName() + ")");
            }
        } catch (Exception ignored) { }
        return parts.isEmpty() ? "This phone has no network up: turn the hotspot on." : "This phone is " + String.join(", ", parts) + ".";
    }

    // When broadcasts are blocked: try every address on each local /24 at once.
    private InetSocketAddress sweep() {
        List<InetAddress> candidates = new ArrayList<>();
        List<String> ranges = new ArrayList<>();
        for (InterfaceAddress ia : localAddresses()) {
            byte[] a = ia.getAddress().getAddress();
            ranges.add((a[0] & 0xff) + "." + (a[1] & 0xff) + "." + (a[2] & 0xff) + ".x");
            for (int host = 1; host < 255; host++) {
                if (host == (a[3] & 0xff)) continue;
                try { candidates.add(InetAddress.getByAddress(new byte[] { a[0], a[1], a[2], (byte) host })); } catch (Exception ignored) { }
            }
        }
        lastSweepCount = candidates.size();
        lastSweepRange = ranges.isEmpty() ? "none" : String.join(", ", ranges);
        if (candidates.isEmpty()) return null;
        final AtomicReference<InetSocketAddress> found = new AtomicReference<>();
        ExecutorService pool = Executors.newFixedThreadPool(48);
        for (InetAddress c : candidates) {
            pool.execute(() -> {
                if (found.get() != null || !running) return;
                InetSocketAddress t = tryConnectable(c.getHostAddress(), TCP_PORT, 350);
                if (t != null) found.compareAndSet(null, t);
            });
        }
        pool.shutdown();
        try { pool.awaitTermination(6, TimeUnit.SECONDS); } catch (InterruptedException ignored) { }
        pool.shutdownNow();
        return found.get();
    }

    private static InetSocketAddress tryConnectable(String host, int port, int timeoutMs) {
        try (Socket s = new Socket()) {
            InetSocketAddress t = new InetSocketAddress(host, port);
            s.connect(t, timeoutMs);
            return t;
        } catch (Exception e) {
            return null;
        }
    }

    private static void sleep(long ms) {
        try { Thread.sleep(ms); } catch (InterruptedException ignored) { }
    }
}
