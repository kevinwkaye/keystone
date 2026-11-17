package uk.kevinkaye.keystone;

import android.content.Context;
import android.media.MediaRecorder;
import android.os.Build;
import android.util.Base64;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.util.Arrays;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * The room track for a rehearsal take: the phone's own mic records while Keystone records,
 * then the file goes to the laptop after the take, a piece at a time, each piece waiting
 * for Keystone's answer so footswitch and phone controls never queue behind it.
 *
 * Files wait in the phone's own storage until they arrive, so a dropped link just means
 * they go across next time the phone finds Keystone.
 */
final class RoomRecorder {
    interface Sender { boolean send(String line); boolean linked(); }

    private static final int PIECE = 48 * 1024;   // 64 KB a line once in base64
    private final File dir;
    private final Context app;
    private volatile Sender out = new Sender() {
        @Override public boolean send(String line) { return false; }
        @Override public boolean linked() { return false; }
    };
    private static RoomRecorder instance;
    private String failedTake = "";

    /** One for the whole app, so the background service and the screens share it. */
    static synchronized RoomRecorder get(Context c) {
        if (instance == null) instance = new RoomRecorder(c.getApplicationContext());
        return instance;
    }

    void setSender(Sender s) { out = s; }

    // the room track setting, remembered so it works with the app out of sight
    boolean wanted() { return app.getSharedPreferences("room", 0).getBoolean("on", true); }
    void setWanted(boolean on) { app.getSharedPreferences("room", 0).edit().putBoolean("on", on).apply(); }

    /**
     * Every meter message from Keystone: start the room with a take, stop it after. Runs on
     * the link's thread, so it works with the screen off.
     */
    void sync(JSONObject rec) {
        boolean on = rec != null && rec.optBoolean("on") && !rec.optString("take").isEmpty();
        String t = on ? rec.optString("take") : "";
        boolean allowed = app.checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) == android.content.pm.PackageManager.PERMISSION_GRANTED;
        boolean want = on && wanted() && allowed;
        if (recording() && (!want || !t.equals(take()))) { stop(); RoomService.say(app, "Ready to record the room with your takes"); }
        if (want && !recording() && !t.equals(failedTake)) {
            if (start(app, t, rec.optDouble("s", 0))) RoomService.say(app, "Recording the room");
            else failedTake = t;
        }
    }
    private MediaRecorder rec;
    private String take = "";
    private double startsAt;
    private volatile String status = "";
    private volatile int sentPct = -1;
    private final LinkedBlockingQueue<JSONObject> acks = new LinkedBlockingQueue<>();
    private Thread uploader;

    private RoomRecorder(Context c) {
        this.app = c;
        this.dir = new File(c.getFilesDir(), "room");
        dir.mkdirs();
    }

    synchronized boolean recording() { return rec != null; }
    synchronized String take() { return take; }

    /** Start recording the room for this take; startsAt is how far into the take it is now. */
    synchronized boolean start(Context c, String takeName, double at) {
        if (rec != null) return takeName.equals(take);
        if (takeName.isEmpty() || takeName.contains("/") || takeName.contains("..")) return false;
        try {
            MediaRecorder m = Build.VERSION.SDK_INT >= 31 ? new MediaRecorder(c) : new MediaRecorder();
            m.setAudioSource(MediaRecorder.AudioSource.UNPROCESSED);   // no phone-call processing on a loud band
            m.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
            m.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
            m.setAudioChannels(1);
            m.setAudioSamplingRate(48000);
            m.setAudioEncodingBitRate(128000);
            m.setOutputFile(new File(dir, takeName + ".part").getAbsolutePath());
            m.prepare();
            m.start();
            rec = m;
            take = takeName;
            startsAt = at;
            status = "Recording the room";
            return true;
        } catch (Exception e) {
            // some phones refuse UNPROCESSED: fall back to the plain mic
            try {
                MediaRecorder m = Build.VERSION.SDK_INT >= 31 ? new MediaRecorder(c) : new MediaRecorder();
                m.setAudioSource(MediaRecorder.AudioSource.MIC);
                m.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
                m.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
                m.setAudioChannels(1);
                m.setAudioSamplingRate(48000);
                m.setAudioEncodingBitRate(128000);
                m.setOutputFile(new File(dir, takeName + ".part").getAbsolutePath());
                m.prepare();
                m.start();
                rec = m;
                take = takeName;
                startsAt = at;
                status = "Recording the room";
                return true;
            } catch (Exception e2) {
                status = "The phone's mic wouldn't start";
                return false;
            }
        }
    }

    /** Stop recording and send the file across. */
    synchronized void stop() {
        if (rec == null) return;
        try { rec.stop(); } catch (Exception ignored) { }
        rec.release();
        rec = null;
        File part = new File(dir, take + ".part");
        File done = new File(dir, take + "@" + Math.round(startsAt * 10) + ".m4a");
        if (part.length() > 0) part.renameTo(done); else part.delete();
        take = "";
        status = "";
        kick();
    }

    /** Called with every roomAck line from Keystone. */
    void onAck(JSONObject ack) { acks.offer(ack); }

    /** Send whatever is waiting, if not already sending. */
    synchronized void kick() {
        if (uploader != null && uploader.isAlive()) return;
        uploader = new Thread(this::uploadAll, "room upload");
        uploader.start();
    }

    String statusJson() {
        try {
            File[] waiting = dir.listFiles((d, n) -> n.endsWith(".m4a"));
            return new JSONObject()
                    .put("recording", recording())
                    .put("take", take())
                    .put("waiting", waiting == null ? 0 : waiting.length)
                    .put("pct", sentPct)
                    .put("status", status)
                    .toString();
        } catch (Exception e) { return "{}"; }
    }

    private void uploadAll() {
        while (out.linked()) {
            File[] files = dir.listFiles((d, n) -> n.endsWith(".m4a"));
            if (files == null || files.length == 0) break;
            Arrays.sort(files);
            if (!upload(files[0])) break;   // try again when the link comes back
        }
        sentPct = -1;
        if (!recording()) status = "";
    }

    private boolean upload(File f) {
        String n = f.getName();
        int at = n.lastIndexOf('@');
        String takeName = at > 0 ? n.substring(0, at) : n.substring(0, n.length() - 4);
        double starts = at > 0 ? Double.parseDouble(n.substring(at + 1, n.length() - 4)) / 10.0 : -1;
        long total = f.length();
        long sent = 0;
        int seq = 0;
        status = "Sending the room track";
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[PIECE];
            while (true) {
                int got = 0;
                while (got < PIECE) {
                    int r = in.read(buf, got, PIECE - got);
                    if (r < 0) break;
                    got += r;
                }
                sent += got;
                boolean last = sent >= total;
                JSONObject m = new JSONObject()
                        .put("t", "room").put("take", takeName).put("seq", seq).put("last", last).put("ext", "m4a")
                        .put("data", Base64.encodeToString(buf, 0, got, Base64.NO_WRAP));
                if (last && starts >= 0) m.put("at", starts);
                acks.clear();
                if (!out.send(m.toString())) return false;
                JSONObject ack = acks.poll(15, TimeUnit.SECONDS);
                if (ack == null) { status = "Room track waiting for Keystone"; return false; }
                if (!ack.optBoolean("ok")) {
                    // Keystone has no such take (deleted, or a different laptop): nothing to send it to
                    f.delete();
                    status = "Keystone didn't take the room track";
                    return true;
                }
                sentPct = (int) (100 * sent / Math.max(1, total));
                seq++;
                if (last) break;
            }
        } catch (Exception e) {
            return false;
        }
        f.delete();
        status = "Room track sent";
        return true;
    }
}
