package uk.kevinkaye.keystone;

import android.content.Context;
import android.os.Build;
import android.util.Base64;

import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.util.Arrays;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Send report: asks Keystone to zip up its black box (logs, health history, settings, and on the
 * Pi the journal), fetches it in pieces over the link, keeps it in the app's cache and hands it
 * to Android's share sheet (Drive, email, anywhere). Progress goes to the page as KS.onReport({...}).
 */
final class ReportFetcher {
    interface Page { void post(String json); }
    interface Sender { boolean send(String line); }
    interface Sharer { void share(File zip, String note); }

    private final Context ctx;
    private final Page page;
    private final Sender out;
    private final Sharer sharer;
    private final LinkedBlockingQueue<JSONObject> lines = new LinkedBlockingQueue<>();
    private volatile boolean busy;
    private String state = "idle", why = "", name = "", note = "";
    private int pct;
    private long bytes;
    private File last;

    ReportFetcher(Context ctx, Page page, Sender out, Sharer sharer) {
        this.ctx = ctx; this.page = page; this.out = out; this.sharer = sharer;
    }

    /** reportAck, reportReady and reportChunk lines from Keystone. */
    void onLine(String line) {
        try { lines.offer(new JSONObject(line)); } catch (Exception ignored) { }
    }

    void start(String text) {
        if (busy) return;
        busy = true;
        note = text == null ? "" : text.trim();
        lines.clear();
        new Thread(this::make, "report").start();
    }

    /** the last report again, for another app */
    void shareAgain() {
        if (last != null && last.exists()) sharer.share(last, note);
    }

    String statusJson() {
        try {
            return new JSONObject().put("busy", busy).put("state", state).put("pct", pct).put("why", why)
                    .put("name", name).put("bytes", bytes).put("canShare", last != null && last.exists()).toString();
        } catch (Exception e) { return "{}"; }
    }

    private void make() {
        try {
            set("making", 0, "");
            String from = "Android phone " + Build.MANUFACTURER + " " + Build.MODEL + ", app " + BuildConfig.VERSION_NAME;
            if (!out.send(new JSONObject().put("t", "report").put("op", "make").put("note", note).put("from", from).toString())) {
                fail("Not linked to Keystone. Join Keystone Wi-Fi and try again");
                return;
            }
            JSONObject a = wait("reportAck", 15);
            if (a == null) { fail("Keystone didn't answer. It may need updating to send reports"); return; }
            if (!a.optBoolean("ok")) { fail(a.optString("why", "Keystone said no")); return; }
            JSONObject r = wait("reportReady", 180);
            if (r == null) { fail("Keystone took too long making the report. Try again"); return; }
            if (!r.optBoolean("ok")) { fail(r.optString("why", "Keystone couldn't make the report")); return; }
            name = r.optString("name");
            bytes = r.optLong("bytes");
            fetch();
        } catch (Exception e) {
            fail("Sending stopped: " + e.getMessage());
        } finally {
            busy = false;
            post();
        }
    }

    private void fetch() throws Exception {
        File dir = ReportProvider.dir(ctx);
        File part = new File(dir, name + ".part");
        long got = 0, total = Math.max(1, bytes);
        set("fetching", 0, "");
        try (FileOutputStream f = new FileOutputStream(part)) {
            while (true) {
                if (!out.send(new JSONObject().put("t", "report").put("op", "get").put("name", name).put("offset", got).toString())) {
                    fail("The link dropped. Try again");
                    return;
                }
                JSONObject c = wait("reportChunk", 20);
                if (c == null) { fail("Keystone stopped answering. Try again"); return; }
                if (!c.optBoolean("ok")) { fail(c.optString("why", "Keystone couldn't send the report")); return; }
                if (!name.equals(c.optString("name")) || c.optLong("offset") != got) continue;   // an old piece
                total = Math.max(1, c.optLong("total", total));
                byte[] piece = Base64.decode(c.optString("data"), Base64.DEFAULT);
                if (piece.length == 0) break;
                f.write(piece);
                got += piece.length;
                int p = (int) (100 * got / total);
                if (p != pct) set("fetching", p, "");
                if (got >= total) break;
            }
        }
        File zip = new File(dir, name);
        zip.delete();
        if (!part.renameTo(zip)) { fail("Couldn't save the report on the phone"); return; }
        last = zip;
        tidy(dir);
        set("ready", 100, "");
        sharer.share(zip, note);
    }

    private JSONObject wait(String type, int seconds) throws InterruptedException {
        long until = System.currentTimeMillis() + seconds * 1000L;
        while (true) {
            long left = until - System.currentTimeMillis();
            if (left <= 0) return null;
            JSONObject m = lines.poll(left, TimeUnit.MILLISECONDS);
            if (m == null) return null;
            if (type.equals(m.optString("t"))) return m;
        }
    }

    // the three newest reports stay on the phone
    private static void tidy(File dir) {
        File[] all = dir.listFiles((d, n) -> n.endsWith(".zip") || n.endsWith(".part"));
        if (all == null) return;
        Arrays.sort(all, (x, y) -> y.getName().compareTo(x.getName()));
        for (int i = 3; i < all.length; i++) all[i].delete();
    }

    private void set(String s, int p, String w) { state = s; pct = p; why = w; post(); }

    private void fail(String w) { set("failed", 0, w); }

    private void post() { page.post(statusJson()); }
}
