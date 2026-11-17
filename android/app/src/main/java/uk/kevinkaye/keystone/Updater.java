package uk.kevinkaye.keystone;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;
import android.util.Base64;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Update firmware and the phone app, from GitHub, with nothing to plug in.
 *
 *  - check: asks GitHub for the latest Keystone release.
 *  - firmware: downloads the laptop's installer (or the Pi's package) on the phone, checks it
 *    against GitHub's checksum, then sends it to Keystone a piece at a time. Keystone checks
 *    it again and hands it to its updater, which puts the old version back if the new one
 *    doesn't start.
 *  - app: downloads the new phone app and asks Android to install it over this one.
 *
 * Results go to the page as KS.onUpdate({...}).
 */
final class Updater {
    interface Page { void post(String json); }
    interface Sender { boolean send(String line); }

    static final String REPO = "kevinwkaye/keystone";
    private static final int PIECE = 48 * 1024;

    private final Context ctx;
    private final Page page;
    private final Sender out;
    private final LinkedBlockingQueue<JSONObject> acks = new LinkedBlockingQueue<>();
    private volatile boolean busy;

    Updater(Context ctx, Page page, Sender out) { this.ctx = ctx; this.page = page; this.out = out; }

    void onAck(JSONObject ack) { acks.offer(ack); }

    // ---------------------------------------------------------------- the latest release
    void check() {
        new Thread(() -> {
            try {
                JSONObject rel = new JSONObject(get("https://api.github.com/repos/" + REPO + "/releases/latest"));
                JSONArray in = rel.getJSONArray("assets"), assets = new JSONArray();
                for (int i = 0; i < in.length(); i++) {
                    JSONObject a = in.getJSONObject(i);
                    assets.put(new JSONObject().put("name", a.getString("name")).put("size", a.getLong("size"))
                            .put("url", a.getString("browser_download_url")).put("digest", a.optString("digest", "")));
                }
                String tag = rel.getString("tag_name");
                post(new JSONObject().put("t", "latest").put("version", tag.startsWith("v") ? tag.substring(1) : tag).put("assets", assets));
            } catch (Exception e) {
                post(err("check", "Couldn't reach GitHub. The phone needs mobile data or Wi-Fi with internet"));
            }
        }, "update check").start();
    }

    // ---------------------------------------------------------------- firmware for the laptop or Pi
    void firmware(String url, String name, long size, String digest, String version) {
        if (busy) return;
        busy = true;
        new Thread(() -> {
            try {
                File f = download(url, name, size, "firmware");
                if (f == null) return;
                String sum = sha256(f);
                if (!digest.isEmpty() && !digest.replace("sha256:", "").equalsIgnoreCase(sum)) {
                    f.delete();
                    post(err("firmware", "The download was damaged. Try again"));
                    return;
                }
                if (!send(new JSONObject().put("t", "fw").put("op", "begin").put("version", version).put("file", name).put("size", f.length()).put("sha256", sum))) return;
                long sent = 0;
                int seq = 0;
                try (InputStream in = new FileInputStream(f)) {
                    byte[] buf = new byte[PIECE];
                    int got;
                    while ((got = readFully(in, buf)) > 0) {
                        if (!send(new JSONObject().put("t", "fw").put("op", "chunk").put("seq", seq++).put("data", Base64.encodeToString(buf, 0, got, Base64.NO_WRAP)))) return;
                        sent += got;
                        progress("firmware", "Sending to Keystone", (int) (100 * sent / f.length()));
                    }
                }
                if (!send(new JSONObject().put("t", "fw").put("op", "end"))) return;
                f.delete();
                post(new JSONObject().put("t", "done").put("what", "firmware").put("version", version));
            } catch (Exception e) {
                post(err("firmware", "Update stopped: " + e.getMessage()));
            } finally {
                busy = false;
            }
        }, "firmware").start();
    }

    // one step of the conversation with Keystone: send, then wait for its answer
    private boolean send(JSONObject m) throws Exception {
        acks.clear();
        if (!out.send(m.toString())) { post(err("firmware", "Lost the link to Keystone. Try again")); return false; }
        JSONObject a = acks.poll(m.optString("op").equals("end") ? 60 : 15, TimeUnit.SECONDS);
        if (a == null) { post(err("firmware", "Keystone stopped answering. Try again")); return false; }
        if (!a.optBoolean("ok")) { post(err("firmware", a.optString("why", "Keystone said no"))); return false; }
        return true;
    }

    // ---------------------------------------------------------------- the phone app
    void app(String url, long size) {
        if (busy) return;
        busy = true;
        new Thread(() -> {
            try {
                File f = download(url, "Keystone.apk", size, "app");
                if (f == null) return;
                progress("app", "Installing", 100);
                PackageInstaller pi = ctx.getPackageManager().getPackageInstaller();
                PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                params.setAppPackageName(ctx.getPackageName());
                int id = pi.createSession(params);
                try (PackageInstaller.Session s = pi.openSession(id)) {
                    try (OutputStream o = s.openWrite("keystone", 0, f.length()); InputStream in = new FileInputStream(f)) {
                        byte[] buf = new byte[65536];
                        int r;
                        while ((r = in.read(buf)) > 0) o.write(buf, 0, r);
                        s.fsync(o);
                    }
                    Intent done = new Intent(ctx, InstallResult.class);
                    int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
                    s.commit(PendingIntent.getBroadcast(ctx, id, done, flags).getIntentSender());
                }
                f.delete();
            } catch (Exception e) {
                post(err("app", "Couldn't install: " + e.getMessage()));
            } finally {
                busy = false;
            }
        }, "app update").start();
    }

    /** Android's answer to the install: usually "ask the user first". */
    public static final class InstallResult extends BroadcastReceiver {
        @Override
        public void onReceive(Context c, Intent i) {
            int status = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                Intent ask = i.getParcelableExtra(Intent.EXTRA_INTENT);
                if (ask != null) { ask.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); c.startActivity(ask); }
            }
        }
    }

    // ---------------------------------------------------------------- plumbing
    private File download(String url, String name, long size, String what) throws Exception {
        File dir = new File(ctx.getCacheDir(), "updates");
        dir.mkdirs();
        File f = new File(dir, name);
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setRequestProperty("User-Agent", "Keystone");
        try (InputStream in = c.getInputStream(); OutputStream o = new FileOutputStream(f)) {
            byte[] buf = new byte[65536];
            long got = 0;
            int r, last = -1;
            while ((r = in.read(buf)) > 0) {
                o.write(buf, 0, r);
                got += r;
                int pct = (int) (100 * got / Math.max(1, size));
                if (pct != last) { progress(what, "Downloading", pct); last = pct; }
            }
        } finally {
            c.disconnect();
        }
        if (size > 0 && f.length() != size) {
            f.delete();
            post(err(what, "The download was cut short. Try again"));
            return null;
        }
        return f;
    }

    private static int readFully(InputStream in, byte[] buf) throws Exception {
        int got = 0;
        while (got < buf.length) {
            int r = in.read(buf, got, buf.length - got);
            if (r < 0) break;
            got += r;
        }
        return got;
    }

    private static String sha256(File f) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[65536];
            int r;
            while ((r = in.read(buf)) > 0) md.update(buf, 0, r);
        }
        StringBuilder sb = new StringBuilder();
        for (byte b : md.digest()) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    private static String get(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(10000);
        c.setReadTimeout(15000);
        c.setRequestProperty("User-Agent", "Keystone");
        c.setRequestProperty("Accept", "application/vnd.github+json");
        try (InputStream in = c.getInputStream()) {
            java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int r;
            while ((r = in.read(buf)) > 0) b.write(buf, 0, r);
            return b.toString("UTF-8");
        } finally {
            c.disconnect();
        }
    }

    private void progress(String what, String stage, int pct) {
        try { post(new JSONObject().put("t", "progress").put("what", what).put("stage", stage).put("pct", pct)); } catch (Exception ignored) { }
    }

    private static JSONObject err(String what, String why) {
        try { return new JSONObject().put("t", "error").put("what", what).put("why", why); } catch (Exception e) { return new JSONObject(); }
    }

    private void post(JSONObject o) { page.post(o.toString()); }
}
