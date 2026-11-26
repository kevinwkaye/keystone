package uk.kevinkaye.keystone.watch;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.net.Network;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * The watch updates itself from Keystone (0.6.6). Keystone offers {"t":"watchUpdate"} when
 * this app is older than the release on the Pi; Install fetches the APK over Keystone's own
 * Wi-Fi (a plain TCP stream of the file's bytes) and hands it to Android's package installer.
 * Android asks the wearer to confirm, at least the first time. No wireless debugging needed.
 */
final class WatchUpdate {
    private WatchUpdate() { }

    /** "0.6.6" newer than "0.6.5". */
    static boolean newer(String a, String b) {
        String[] pa = a.split("\\."), pb = b.split("\\.");
        for (int i = 0; i < Math.max(pa.length, pb.length); i++) {
            int x = i < pa.length ? num(pa[i]) : 0, y = i < pb.length ? num(pb[i]) : 0;
            if (x != y) return x > y;
        }
        return false;
    }

    private static int num(String s) {
        try { return Integer.parseInt(s.trim()); } catch (Exception e) { return 0; }
    }

    /** On its own thread: download from Keystone, then install. Progress goes to STATE. */
    static void run(Context ctx, Network net, String host) {
        final WristService.State s = WristService.STATE;
        new Thread(() -> {
            File apk = new File(ctx.getCacheDir(), "keystone-watch-update.apk");
            try {
                s.updPct = 0;
                s.updStage = WristService.UPD_DOWNLOADING;
                if (net == null || host == null || host.isEmpty()) throw new Exception("Not connected to Keystone");
                long want = s.updSize;
                long got = 0;
                try (Socket sock = net.getSocketFactory().createSocket()) {
                    sock.connect(new InetSocketAddress(host, s.updPort), 5000);
                    sock.setSoTimeout(20000);
                    try (InputStream in = sock.getInputStream(); OutputStream out = new FileOutputStream(apk)) {
                        byte[] buf = new byte[32768];
                        int n;
                        while ((n = in.read(buf)) > 0) {
                            out.write(buf, 0, n);
                            got += n;
                            if (want > 0) s.updPct = (int) Math.min(99, got * 100 / want);
                        }
                    }
                }
                if (want > 0 && got != want) throw new Exception("The download stopped early. Stay near the pedal and try again.");
                s.updPct = 100;
                install(ctx, apk, got);
            } catch (Exception e) {
                fail(e.getMessage() == null ? "Couldn't get the update" : e.getMessage());
            }
        }, "Keystone watch update").start();
    }

    private static void install(Context ctx, File apk, long size) throws Exception {
        PackageInstaller pi = ctx.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams p = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        p.setAppPackageName(ctx.getPackageName());
        p.setSize(size);
        // after the first update this app is its own installer, so Android may skip the prompt
        if (Build.VERSION.SDK_INT >= 31) p.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
        int id = pi.createSession(p);
        try (PackageInstaller.Session session = pi.openSession(id)) {
            try (InputStream in = new FileInputStream(apk); OutputStream out = session.openWrite("keystone-watch", 0, size)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                session.fsync(out);
            }
            ctx.getSharedPreferences("link", Context.MODE_PRIVATE).edit().putString("updatingFrom", WristService.VERSION).apply();
            WristService.STATE.updStage = WristService.UPD_INSTALLING;
            Intent done = new Intent(ctx, Result.class);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            session.commit(PendingIntent.getBroadcast(ctx, id, done, flags).getIntentSender());
        }
    }

    static void fail(String why) {
        WristService.STATE.updErr = why;
        WristService.STATE.updStage = WristService.UPD_FAILED;
    }

    /** Android's answer: ask the wearer, done, or why not. */
    public static final class Result extends BroadcastReceiver {
        @Override
        public void onReceive(Context ctx, Intent i) {
            int status = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            WristService.State s = WristService.STATE;
            if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                Intent ask = i.getParcelableExtra(Intent.EXTRA_INTENT);
                if (ask == null) { fail("Android didn't ask to install"); return; }
                ask.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try { ctx.startActivity(ask); } catch (Exception e) { fail("Couldn't show Android's install prompt"); }
            } else if (status == PackageInstaller.STATUS_SUCCESS) {
                s.updStage = WristService.UPD_INSTALLING;   // Android restarts the app now
            } else if (status == PackageInstaller.STATUS_FAILURE_ABORTED) {
                // cancelled: back to the pill, nothing lost
                ctx.getSharedPreferences("link", Context.MODE_PRIVATE).edit().remove("updatingFrom").apply();
                s.updScreen = false;
                s.updStage = WristService.UPD_OFFERED;
            } else {
                ctx.getSharedPreferences("link", Context.MODE_PRIVATE).edit().remove("updatingFrom").apply();
                String msg = i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
                fail(status == PackageInstaller.STATUS_FAILURE_BLOCKED
                        ? "This watch won't let apps install updates. Use Update watch on the phone."
                        : "Android didn't install it" + (msg == null ? "" : ": " + msg));
            }
        }
    }

    /** After an update Android stops the app: start it again so the watch rejoins Keystone. */
    public static final class Replaced extends BroadcastReceiver {
        @Override
        public void onReceive(Context ctx, Intent i) {
            if (!Intent.ACTION_MY_PACKAGE_REPLACED.equals(i.getAction())) return;
            try {
                ctx.startActivity(new Intent(ctx, WatchActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (Exception e) {
                try { ctx.startForegroundService(new Intent(ctx, WristService.class)); } catch (Exception ignored) { }
            }
        }
    }
}
