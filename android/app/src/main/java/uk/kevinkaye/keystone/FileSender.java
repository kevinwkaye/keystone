package uk.kevinkaye.keystone;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Base64;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Send files to Keystone (0.5.9): captures (.nam), spaces (.wav) and videos, picked on the
 * phone and sent over the link a piece at a time. Keystone files each by type into
 * Captures, Spaces or Visuals. Progress goes to the page as KS.onFiles({...}).
 */
final class FileSender {
    interface Page { void post(String json); }
    interface Sender { boolean send(String line); }

    private static final int PIECE = 48 * 1024;

    private final Context ctx;
    private final Page page;
    private final Sender out;
    private final LinkedBlockingQueue<JSONObject> acks = new LinkedBlockingQueue<>();
    private final List<Item> queue = new ArrayList<>();
    private volatile boolean busy, stop;

    private static final class Item {
        Uri uri; String name; long size; String state = "next"; int pct; String why = "";
    }

    FileSender(Context ctx, Page page, Sender out) { this.ctx = ctx; this.page = page; this.out = out; }

    void onAck(JSONObject ack) { acks.offer(ack); }

    void cancel() { stop = true; }

    void clear() {
        synchronized (queue) { if (!busy) queue.clear(); }
        report();
    }

    // files the person picked: added to the list and sent one after another
    void add(List<Uri> uris) {
        ContentResolver cr = ctx.getContentResolver();
        synchronized (queue) {
            for (Uri u : uris) {
                Item it = new Item();
                it.uri = u;
                it.name = "file";
                it.size = -1;
                try (Cursor c = cr.query(u, new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE }, null, null, null)) {
                    if (c != null && c.moveToFirst()) {
                        if (!c.isNull(0)) it.name = c.getString(0);
                        if (!c.isNull(1)) it.size = c.getLong(1);
                    }
                } catch (Exception ignored) { }
                queue.add(it);
            }
        }
        report();
        if (!busy) start();
    }

    private void start() {
        busy = true;
        stop = false;
        new Thread(() -> {
            try {
                while (!stop) {
                    Item it = null;
                    synchronized (queue) {
                        for (Item q : queue) if (q.state.equals("next")) { it = q; break; }
                    }
                    if (it == null) break;
                    sendOne(it);
                    report();
                }
                if (stop) {
                    synchronized (queue) { for (Item q : queue) if (q.state.equals("next") || q.state.equals("sending")) { q.state = "stopped"; } }
                    try { out.send(new JSONObject().put("t", "file").put("op", "cancel").toString()); } catch (Exception ignored) { }
                }
            } finally {
                busy = false;
                report();
            }
        }, "send-files").start();
    }

    private void sendOne(Item it) {
        it.state = "sending";
        it.pct = 0;
        report();
        try {
            if (it.size <= 0) { fail(it, "Couldn't read that file on the phone"); return; }
            JSONObject a = ask(new JSONObject().put("t", "file").put("op", "begin").put("name", it.name).put("size", it.size));
            if (!ok(it, a)) return;
            long sent = 0;
            int seq = 0, lastPct = -1;
            try (InputStream in = ctx.getContentResolver().openInputStream(it.uri)) {
                if (in == null) { fail(it, "Couldn't read that file on the phone"); return; }
                byte[] buf = new byte[PIECE];
                int got;
                while ((got = readFully(in, buf)) > 0) {
                    if (stop) { it.state = "stopped"; return; }
                    a = ask(new JSONObject().put("t", "file").put("op", "chunk").put("seq", seq++).put("data", Base64.encodeToString(buf, 0, got, Base64.NO_WRAP)));
                    if (!ok(it, a)) return;
                    sent += got;
                    it.pct = (int) (100 * sent / it.size);
                    if (it.pct != lastPct) { lastPct = it.pct; report(); }
                }
            }
            a = ask(new JSONObject().put("t", "file").put("op", "end"));
            if (!ok(it, a)) return;
            it.state = "sent";
            it.pct = 100;
        } catch (Exception e) {
            fail(it, "Sending stopped: " + e.getMessage());
        }
    }

    private JSONObject ask(JSONObject m) throws Exception {
        acks.clear();
        if (!out.send(m.toString())) return new JSONObject().put("ok", false).put("why", "Not linked to Keystone. Join Keystone Wi-Fi and try again");
        JSONObject a = acks.poll(m.optString("op").equals("end") ? 60 : 15, TimeUnit.SECONDS);
        return a != null ? a : new JSONObject().put("ok", false).put("why", "Keystone stopped answering. Try again");
    }

    private boolean ok(Item it, JSONObject a) {
        if (a.optBoolean("ok")) return true;
        fail(it, a.optString("why", "Keystone said no"));
        return false;
    }

    private void fail(Item it, String why) { it.state = "failed"; it.why = why; }

    private static int readFully(InputStream in, byte[] buf) throws Exception {
        int n = 0, r;
        while (n < buf.length && (r = in.read(buf, n, buf.length - n)) > 0) n += r;
        return n;
    }

    private void report() {
        try {
            JSONArray list = new JSONArray();
            synchronized (queue) {
                for (Item q : queue)
                    list.put(new JSONObject().put("name", q.name).put("size", q.size).put("state", q.state).put("pct", q.pct).put("why", q.why));
            }
            page.post(new JSONObject().put("busy", busy).put("files", list).toString());
        } catch (Exception ignored) { }
    }
}
