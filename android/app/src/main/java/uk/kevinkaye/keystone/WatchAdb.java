package uk.kevinkaye.keystone;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import android.sun.security.x509.AlgorithmId;
import android.sun.security.x509.CertificateAlgorithmId;
import android.sun.security.x509.CertificateExtensions;
import android.sun.security.x509.CertificateIssuerName;
import android.sun.security.x509.CertificateSerialNumber;
import android.sun.security.x509.CertificateSubjectName;
import android.sun.security.x509.CertificateValidity;
import android.sun.security.x509.CertificateVersion;
import android.sun.security.x509.CertificateX509Key;
import android.sun.security.x509.KeyIdentifier;
import android.sun.security.x509.SubjectKeyIdentifierExtension;
import android.sun.security.x509.X500Name;
import android.sun.security.x509.X509CertImpl;
import android.sun.security.x509.X509CertInfo;

import androidx.annotation.NonNull;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.Date;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

import io.github.muntashirakon.adb.AbsAdbConnectionManager;
import io.github.muntashirakon.adb.AdbStream;

/**
 * The phone talking to the watch the way Bugjaeger does: Wear OS's wireless debugging.
 * Pair once with the six numbers the watch shows, then the phone can install the watch app
 * over Wi-Fi. The phone finds the watch's pairing and debugging services by name on the
 * network (mDNS), so there's no address or port to type unless that fails.
 *
 * The phone's key and certificate live in the app's own files, so the watch remembers it.
 */
final class WatchAdb extends AbsAdbConnectionManager {
    static final String WATCH_PACKAGE = "uk.kevinkaye.keystone.watch";
    static final String PAIRING = "_adb-tls-pairing._tcp", CONNECT = "_adb-tls-connect._tcp";

    private static WatchAdb instance;

    static synchronized WatchAdb get(Context ctx) throws Exception {
        if (instance == null) instance = new WatchAdb(ctx.getApplicationContext());
        return instance;
    }

    private final PrivateKey key;
    private final Certificate cert;

    private WatchAdb(Context ctx) throws Exception {
        setApi(30);   // Android 11, where wireless debugging began; every Wear OS 3+ watch speaks it
        setTimeout(15, TimeUnit.SECONDS);
        File keyFile = new File(ctx.getFilesDir(), "watch-adb.key"), certFile = new File(ctx.getFilesDir(), "watch-adb.crt");
        PrivateKey k = null;
        Certificate c = null;
        if (keyFile.exists() && certFile.exists()) {
            try {
                k = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(Files.readAllBytes(keyFile.toPath())));
                try (InputStream in = new FileInputStream(certFile)) { c = CertificateFactory.getInstance("X.509").generateCertificate(in); }
            } catch (Exception e) { k = null; c = null; }
        }
        if (k == null || c == null) {
            KeyPairGenerator g = KeyPairGenerator.getInstance("RSA");
            g.initialize(2048, new SecureRandom());
            KeyPair pair = g.generateKeyPair();
            PublicKey pub = pair.getPublic();
            k = pair.getPrivate();
            String alg = "SHA512withRSA";
            X500Name name = new X500Name("CN=Keystone phone");
            Date from = new Date(), to = new Date(System.currentTimeMillis() + 20L * 365 * 86400000L);
            CertificateExtensions ext = new CertificateExtensions();
            ext.set("SubjectKeyIdentifier", new SubjectKeyIdentifierExtension(new KeyIdentifier(pub).getIdentifier()));
            X509CertInfo info = new X509CertInfo();
            info.set("version", new CertificateVersion(2));
            info.set("serialNumber", new CertificateSerialNumber(new SecureRandom().nextInt() & Integer.MAX_VALUE));
            info.set("algorithmID", new CertificateAlgorithmId(AlgorithmId.get(alg)));
            info.set("subject", new CertificateSubjectName(name));
            info.set("key", new CertificateX509Key(pub));
            info.set("validity", new CertificateValidity(from, to));
            info.set("issuer", new CertificateIssuerName(name));
            info.set("extensions", ext);
            X509CertImpl signed = new X509CertImpl(info);
            signed.sign(k, alg);
            c = CertificateFactory.getInstance("X.509").generateCertificate(new ByteArrayInputStream(signed.getEncoded()));
            try (OutputStream o = new FileOutputStream(keyFile)) { o.write(k.getEncoded()); }
            try (OutputStream o = new FileOutputStream(certFile)) { o.write(c.getEncoded()); }
        }
        key = k;
        cert = c;
    }

    @NonNull @Override protected PrivateKey getPrivateKey() { return key; }
    @NonNull @Override protected Certificate getCertificate() { return cert; }
    @NonNull @Override protected String getDeviceName() { return "Keystone phone"; }

    /**
     * Looks for the watch's pairing or debugging service on the Wi-Fi, for up to timeoutMs.
     * host: the watch's address as Keystone sees it, so the phone never picks itself or
     * another device; empty takes the first it finds.
     */
    static InetSocketAddress find(Context ctx, String type, String host, long timeoutMs) {
        NsdManager nsd = ctx.getSystemService(NsdManager.class);
        WifiManager wm = ctx.getApplicationContext().getSystemService(WifiManager.class);
        WifiManager.MulticastLock lock = wm.createMulticastLock("keystone:watch");
        LinkedBlockingQueue<NsdServiceInfo> found = new LinkedBlockingQueue<>();
        LinkedBlockingQueue<InetSocketAddress> resolved = new LinkedBlockingQueue<>();
        NsdManager.DiscoveryListener listener = new NsdManager.DiscoveryListener() {
            @Override public void onStartDiscoveryFailed(String t, int e) { }
            @Override public void onStopDiscoveryFailed(String t, int e) { }
            @Override public void onDiscoveryStarted(String t) { }
            @Override public void onDiscoveryStopped(String t) { }
            @Override public void onServiceFound(NsdServiceInfo s) { found.offer(s); }
            @Override public void onServiceLost(NsdServiceInfo s) { }
        };
        lock.setReferenceCounted(false);
        lock.acquire();
        try {
            nsd.discoverServices(type, NsdManager.PROTOCOL_DNS_SD, listener);
            long until = System.currentTimeMillis() + timeoutMs;
            while (System.currentTimeMillis() < until) {
                NsdServiceInfo s = found.poll(Math.max(1, until - System.currentTimeMillis()), TimeUnit.MILLISECONDS);
                if (s == null) break;
                // one resolve at a time: older Android refuses a second while one is running
                nsd.resolveService(s, new NsdManager.ResolveListener() {
                    @Override public void onResolveFailed(NsdServiceInfo i, int e) { resolved.offer(new InetSocketAddress(0)); }
                    @Override public void onServiceResolved(NsdServiceInfo i) {
                        resolved.offer(i.getHost() == null ? new InetSocketAddress(0) : new InetSocketAddress(i.getHost(), i.getPort()));
                    }
                });
                InetSocketAddress a = resolved.poll(5, TimeUnit.SECONDS);
                if (a == null || a.getAddress() == null || a.getPort() <= 0) continue;
                String ip = a.getAddress().getHostAddress();
                if (host == null || host.isEmpty() || host.equals(ip)) return a;
            }
        } catch (Exception ignored) {
        } finally {
            try { nsd.stopServiceDiscovery(listener); } catch (Exception ignored) { }
            if (lock.isHeld()) lock.release();
        }
        return null;
    }

    interface Progress { void at(int pct); }

    /** Installs the APK over the open connection; returns null when it worked, or why not. */
    String install(File apk, Progress progress) throws Exception {
        String why;
        try (AdbStream s = openStream("exec:cmd package install -r -S " + apk.length())) {
            OutputStream o = s.openOutputStream();
            try (InputStream in = new FileInputStream(apk)) {
                byte[] buf = new byte[64 * 1024];
                long sent = 0;
                int r, last = -1;
                while ((r = in.read(buf)) > 0) {
                    o.write(buf, 0, r);
                    sent += r;
                    int pct = (int) (100 * sent / Math.max(1, apk.length()));
                    if (pct != last) { progress.at(pct); last = pct; }
                }
                o.flush();
            }
            why = readAll(s.openInputStream()).trim();
        }
        if (why.contains("Success")) return null;
        return why.isEmpty() ? "The watch didn't say whether it worked" : why;
    }

    /** Opens the watch app again after the install closed it. */
    void openWatchApp() {
        try (AdbStream s = openStream("shell:am start -n " + WATCH_PACKAGE + "/.WatchActivity")) {
            readAll(s.openInputStream());
        } catch (Exception ignored) { }
    }

    private static String readAll(InputStream in) {
        StringBuilder b = new StringBuilder();
        byte[] buf = new byte[4096];
        try {
            int r;
            while ((r = in.read(buf)) > 0) b.append(new String(buf, 0, r, StandardCharsets.UTF_8));
        } catch (Exception ignored) { }
        return b.toString();
    }
}
