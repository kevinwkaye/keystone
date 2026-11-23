package uk.kevinkaye.keystone.watch;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.RouteInfo;
import android.net.wifi.WifiManager;
import android.net.wifi.WifiNetworkSpecifier;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.VibrationEffect;
import android.os.Vibrator;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Keeps the watch talking to Keystone while the app is open, screen on or off.
 *
 * Wi-Fi: Wear OS switches Wi-Fi off while the phone is near, so the service asks for a
 * Wi-Fi network outright (no internet needed, Keystone's own network has none) and binds
 * its sockets to it. Finding Keystone: the last address that worked, then the network's
 * gateway (on Keystone's own Wi-Fi the rig is the gateway), then its UDP beacons.
 *
 * The wrist: the game rotation vector (gyro and accelerometer, no compass, so no drift
 * from metal on stage) as a quaternion, 10 times a second while waiting and 50 while live.
 * Keystone does all the maths, so the feel can change with a Keystone update alone.
 */
public final class WristService extends Service implements SensorEventListener {
    static final int TCP_PORT = 47801, UDP_PORT = 47800;

    /** What the screen shows. Written by the link thread, read by the screen. */
    static final class State {
        volatile boolean connected;
        volatile String status = "Starting";
        volatile int bank;
        volatile String jobName = "";
        volatile float jobv;
        volatile boolean jobOn, live;
        volatile boolean tunerOn, tunerValid;
        volatile String note = "";
        volatile float cents;
        volatile int drops;              // since the app opened
        volatile String net = "";        // which Wi-Fi, pinned or not, and Keystone's address
    }
    static final String VERSION = BuildConfig.VERSION_NAME;   // from CMakeLists.txt, via build.gradle
    static final State STATE = new State();
    static volatile WristService running;

    private SensorManager sensors;
    private Vibrator vibrator;
    private PowerManager.WakeLock wake;
    private WifiManager.WifiLock wifiLock, wifiLockLow;
    private ConnectivityManager cm;
    private ConnectivityManager.NetworkCallback netCallback;
    private volatile Network wifi;
    private volatile Socket socket;
    private volatile OutputStream out;
    private volatile boolean alive;
    private Thread linkThread;
    private HandlerThread sensorThread;   // sensors and writes stay off the main thread
    private Handler sensorHandler;
    private long lastSent;
    private boolean wasLive;
    private float lastJobv = -1;

    @Override public IBinder onBind(Intent i) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        running = this;
        alive = true;
        startInForeground();
        vibrator = getSystemService(Vibrator.class);
        PowerManager pm = getSystemService(PowerManager.class);
        wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "keystone:wrist");
        wake.acquire();
        holdWifiAwake();
        sensors = getSystemService(SensorManager.class);
        Sensor rv = sensors.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);
        if (rv == null) rv = sensors.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        sensorThread = new HandlerThread("Keystone wrist");
        sensorThread.start();
        sensorHandler = new Handler(sensorThread.getLooper());
        if (rv != null) sensors.registerListener(this, rv, 20000, sensorHandler);
        else STATE.status = "This watch has no motion sensor";
        askForWifi();
        linkThread = new Thread(this::linkLoop, "Keystone watch link");
        linkThread.start();
    }

    @Override
    public void onDestroy() {
        alive = false;
        running = null;
        sensors.unregisterListener(this);
        if (netCallback != null) try { cm.unregisterNetworkCallback(netCallback); } catch (Exception ignored) { }
        closeSocket();
        if (linkThread != null) linkThread.interrupt();
        if (wake.isHeld()) wake.release();
        if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        if (wifiLockLow != null && wifiLockLow.isHeld()) wifiLockLow.release();
        sensorThread.quitSafely();
        STATE.connected = false;
        super.onDestroy();
    }

    private void startInForeground() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel("link", "Keystone link", NotificationManager.IMPORTANCE_LOW));
        PendingIntent open = PendingIntent.getActivity(this, 0, new Intent(this, WatchActivity.class), PendingIntent.FLAG_IMMUTABLE);
        Notification n = new Notification.Builder(this, "link")
                .setContentTitle("Keystone")
                .setContentText("Your wrist is ready for the pedal")
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentIntent(open)
                .setOngoing(true)
                .build();
        startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
    }

    // ---------------------------------------------------------------- Wi-Fi
    /**
     * The watch uses the Wi-Fi it's already on. Only if Keystone can't be found there for ten
     * seconds (the watch has wandered to a home network with internet) does it ask for
     * Keystone's network by name. Asking by name while the watch is already on Keystone's
     * network made a second connection to it, with a different address, and the two kept
     * taking over from each other: that was the dropouts in 0.4.19.
     */
    private void askForWifi() {
        cm = getSystemService(ConnectivityManager.class);
        String ssid = prefs().getString("ssid", null), pass = prefs().getString("pass", null);
        NetworkRequest.Builder b = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
        pinned = wantPin && ssid != null && pass != null && pass.length() >= 8;
        if (pinned) b.setNetworkSpecifier(new WifiNetworkSpecifier.Builder().setSsid(ssid).setWpa2Passphrase(pass).build());
        final boolean wasPinned = pinned;
        netCallback = new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network n) { wifi = n; }
            @Override public void onLost(Network n) { if (n.equals(wifi)) { wifi = null; closeSocket(); } }
            @Override public void onUnavailable() {
                // declined, or Keystone's network isn't around: fall back to any Wi-Fi
                if (wasPinned) { unpinned = true; wantPin = false; rerequestWifi(false); }
            }
        };
        try {
            if (pinned && !unpinned) cm.requestNetwork(b.build(), netCallback, 60000);
            else {
                pinned = false;
                cm.requestNetwork(new NetworkRequest.Builder()
                        .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                        .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).build(), netCallback);
            }
        } catch (Exception e) { STATE.status = "Wi-Fi not allowed: " + e.getMessage(); }
    }

    /**
     * Wear OS dozes the Wi-Fi radio between packets to save battery, so Keystone's meters
     * arrive in bursts and its send buffer backs up until it gives up on the watch. These
     * locks keep the radio awake while the app runs: low latency while the screen is on,
     * high performance (the older lock, still honoured by Wear OS) with it off.
     */
    @SuppressWarnings("deprecation")
    private void holdWifiAwake() {
        try {
            WifiManager wm = getApplicationContext().getSystemService(WifiManager.class);
            wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "keystone:wifi");
            wifiLock.setReferenceCounted(false);
            wifiLock.acquire();
            wifiLockLow = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "keystone:wifi-low");
            wifiLockLow.setReferenceCounted(false);
            wifiLockLow.acquire();
        } catch (Exception e) { /* no lock: it still works, just with more drops */ }
    }

    private volatile boolean pinned, unpinned;   // unpinned: this run only, it tries again next time
    private volatile boolean wantPin;            // ask for Keystone's network by name
    private boolean triedPin;                    // once per run, so it can't flip back and forth
    private long missingSince;                   // when Keystone was last out of reach, or 0

    private void rerequestWifi(boolean pin) {
        if (netCallback != null) try { cm.unregisterNetworkCallback(netCallback); } catch (Exception ignored) { }
        wifi = null;
        wantPin = pin;
        if (pin) unpinned = false;
        askForWifi();
    }

    /** Keystone out of reach on this Wi-Fi: after ten seconds, ask for its network by name. */
    private void notFound() {
        long now = System.currentTimeMillis();
        if (missingSince == 0) { missingSince = now; return; }
        if (wantPin || triedPin || now - missingSince < 10000) return;
        String ssid = prefs().getString("ssid", null), pass = prefs().getString("pass", null);
        if (ssid == null || pass == null || pass.length() < 8) return;
        triedPin = true;
        missingSince = 0;
        rerequestWifi(true);
    }

    private SharedPreferences prefs() { return getSharedPreferences("link", Context.MODE_PRIVATE); }

    // ---------------------------------------------------------------- the link
    private void linkLoop() {
        while (alive) {
            Network net = wifi;
            if (net == null) {
                STATE.connected = false;
                STATE.status = "Waiting for Wi-Fi. Join the Keystone network in this watch's Wi-Fi settings.";
                notFound();
                sleep(1000);
                continue;
            }
            STATE.status = "Looking for Keystone";
            InetSocketAddress target = find(net);
            if (target == null) { notFound(); sleep(800); continue; }
            missingSince = 0;
            try {
                Socket s = net.getSocketFactory().createSocket();
                s.setTcpNoDelay(true);
                s.connect(target, 1500);
                s.setSoTimeout(5000);
                socket = s;
                out = s.getOutputStream();
                prefs().edit().putString("last", target.getAddress().getHostAddress()).apply();
                STATE.connected = true;
                STATE.status = "Connected";
                buzz(new long[] { 0, 30, 80, 30 });
                STATE.net = (pinned && !unpinned ? "Keystone Wi-Fi by name" : "Wi-Fi") + ", " + target.getAddress().getHostAddress();
                send("{\"t\":\"watchHello\",\"v\":\"" + VERSION + "\",\"net\":\"" + STATE.net + "\",\"drops\":" + STATE.drops + "}");
                BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8));
                String line;
                while (alive && (line = in.readLine()) != null) onLine(line);
            } catch (Exception e) {
                STATE.status = "Lost Keystone, looking again";
            }
            closeSocket();
            if (STATE.connected) { buzz(new long[] { 0, 200 }); STATE.drops++; }
            STATE.connected = false;
            STATE.live = false;
            sleep(500);
        }
    }

    private InetSocketAddress find(Network net) {
        List<String> tries = new ArrayList<>();
        String last = prefs().getString("last", null);
        if (last != null) tries.add(last);
        try {
            LinkProperties lp = cm.getLinkProperties(net);
            if (lp != null) {
                for (RouteInfo r : lp.getRoutes())
                    if (r.getGateway() instanceof Inet4Address && !r.getGateway().isAnyLocalAddress())
                        tries.add(r.getGateway().getHostAddress());
                Inet4Address dhcp = lp.getDhcpServerAddress();
                if (dhcp != null) tries.add(dhcp.getHostAddress());
            }
        } catch (Exception ignored) { }
        for (String host : tries) if (reachable(net, host)) return new InetSocketAddress(host, TCP_PORT);
        // Keystone's beacon, or its answer to a probe
        try (DatagramSocket ds = new DatagramSocket(null)) {
            ds.setReuseAddress(true);
            ds.setBroadcast(true);
            ds.bind(new InetSocketAddress(UDP_PORT));
            net.bindSocket(ds);
            ds.setSoTimeout(1500);
            byte[] probe = "KEYSTONE?".getBytes(StandardCharsets.UTF_8);
            ds.send(new DatagramPacket(probe, probe.length, InetAddress.getByName("255.255.255.255"), UDP_PORT));
            byte[] buf = new byte[512];
            DatagramPacket p = new DatagramPacket(buf, buf.length);
            long until = System.currentTimeMillis() + 1500;
            while (System.currentTimeMillis() < until) {
                ds.receive(p);
                String text = new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8);
                if (!text.startsWith("KEYSTONE?")) return new InetSocketAddress(p.getAddress(), TCP_PORT);
            }
        } catch (Exception ignored) { }
        return null;
    }

    private boolean reachable(Network net, String host) {
        try (Socket s = net.getSocketFactory().createSocket()) {
            s.connect(new InetSocketAddress(host, TCP_PORT), 600);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private void onLine(String line) {
        try {
            JSONObject m = new JSONObject(line);
            if ("watchWifi".equals(m.optString("t"))) {
                String ssid = m.optString("ssid", ""), pass = m.optString("pass", "");
                boolean changed = !ssid.equals(prefs().getString("ssid", "")) || !pass.equals(prefs().getString("pass", ""));
                if (!ssid.isEmpty() && pass.length() >= 8 && (changed || !pinned)) {
                    // kept for when the watch wanders off Keystone's network; it's on it now
                    prefs().edit().putString("ssid", ssid).putString("pass", pass).apply();
                }
                return;
            }
            if (!"meter".equals(m.optString("t"))) return;
            STATE.bank = m.optInt("bank", STATE.bank);
            STATE.jobName = m.optString("jobName", STATE.jobName);
            STATE.jobv = (float) m.optDouble("jobv", STATE.jobv);
            STATE.jobOn = m.optBoolean("jobOn", false);
            JSONObject w = m.optJSONObject("watch");
            boolean live = w != null && w.optBoolean("live", false);
            JSONObject t = m.optJSONObject("tuner");
            STATE.tunerOn = t != null && t.optBoolean("on", false);
            if (t != null) {
                STATE.tunerValid = t.optBoolean("valid", false);
                STATE.note = t.optString("note", "");
                STATE.cents = (float) t.optDouble("cents", 0);
            }
            // a buzz when the wrist takes over and when it lets go, a tick at heel and toe
            if (live != wasLive) buzz(live ? new long[] { 0, 45 } : new long[] { 0, 20, 60, 20 });
            if (live && lastJobv >= 0) {
                boolean edge = (STATE.jobv >= 0.999f && lastJobv < 0.999f) || (STATE.jobv <= 0.001f && lastJobv > 0.001f);
                if (edge) buzz(new long[] { 0, 15 });
            }
            wasLive = live;
            lastJobv = STATE.jobv;
            STATE.live = live;
        } catch (Exception ignored) { }
    }

    /** Tap on the screen: the tuner on or off. */
    void toggleTuner() {
        final String line = "{\"t\":\"tuner\",\"on\":" + (!STATE.tunerOn) + "}";
        sensorHandler.post(() -> send(line));
    }

    private void send(String line) {
        OutputStream o = out;
        if (o == null) return;
        try {
            synchronized (this) { o.write((line + "\n").getBytes(StandardCharsets.UTF_8)); o.flush(); }
        } catch (Exception e) {
            closeSocket();
        }
    }

    private void closeSocket() {
        Socket s = socket;
        socket = null;
        out = null;
        if (s != null) try { s.close(); } catch (Exception ignored) { }
    }

    // ---------------------------------------------------------------- the wrist
    private final float[] q = new float[4];

    @Override
    public void onSensorChanged(SensorEvent e) {
        if (out == null) return;
        long now = System.nanoTime();
        long every = STATE.live ? 18_000_000L : 95_000_000L;
        if (now - lastSent < every) return;
        lastSent = now;
        SensorManager.getQuaternionFromVector(q, e.values);   // w, x, y, z
        send(String.format(Locale.ROOT, "{\"t\":\"wrist\",\"q\":[%.5f,%.5f,%.5f,%.5f]}", q[0], q[1], q[2], q[3]));
    }

    @Override public void onAccuracyChanged(Sensor s, int a) { }

    private void buzz(long[] pattern) {
        try { if (vibrator != null) vibrator.vibrate(VibrationEffect.createWaveform(pattern, -1)); } catch (Exception ignored) { }
    }

    private static void sleep(long ms) {
        try { Thread.sleep(ms); } catch (InterruptedException ignored) { }
    }
}
