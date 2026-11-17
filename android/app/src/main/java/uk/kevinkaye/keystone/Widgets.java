package uk.kevinkaye.keystone;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.widget.RemoteViews;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

/**
 * The home screen widgets. Keystone's own messages (state, val, meter) pass through here on
 * their way to the page, and the widgets redraw at most five times a second. They only know
 * what the phone app hears, so they show "Not linked" when the app isn't connected.
 */
final class Widgets {
    private Widgets() { }

    // ---- what the widgets show ----
    private static boolean linked;
    private static int rig, bank, current, droneLayers, footMode;
    private static String[] songs = new String[0];
    private static boolean[] parts = new boolean[0];
    private static final Map<String, Double> values = new HashMap<>();
    private static double inDb = -100;
    private static boolean tunerOn, tunerValid;
    private static String tunerNote = "";
    private static double tunerCents;

    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static long lastDraw;
    private static boolean drawQueued;

    static void onLine(Context ctx, String line) {
        try {
            JSONObject m = new JSONObject(line);
            String t = m.optString("t");
            synchronized (Widgets.class) {
                if ("state".equals(t)) {
                    rig = m.optInt("rig", 0);
                    bank = m.optInt("bank", 0);
                    current = m.optInt("current", 0);
                    footMode = m.optInt("footMode", 0);
                    JSONArray s = m.optJSONArray("songs"), p = m.optJSONArray("parts");
                    songs = new String[s == null ? 0 : s.length()];
                    parts = new boolean[songs.length];
                    for (int i = 0; i < songs.length; i++) {
                        songs[i] = s.optString(i);
                        parts[i] = p != null && p.optBoolean(i, false);
                    }
                    JSONObject v = m.optJSONObject("values");
                    if (v != null) for (java.util.Iterator<String> it = v.keys(); it.hasNext(); ) { String k = it.next(); values.put(k, v.optDouble(k)); }
                } else if ("val".equals(t)) {
                    values.put(m.optString("id"), m.optDouble("v"));
                } else if ("meter".equals(t)) {
                    inDb = m.optDouble("in", -100);
                    if (m.has("rig")) rig = m.optInt("rig");
                    if (m.has("bank")) bank = m.optInt("bank");
                    droneLayers = m.optInt("drone", 0);
                    JSONObject tu = m.optJSONObject("tuner");
                    tunerOn = tu != null && tu.optBoolean("on");
                    tunerValid = tu != null && tu.optBoolean("valid");
                    tunerNote = tu == null ? "" : tu.optString("note");
                    tunerCents = tu == null ? 0 : tu.optDouble("cents");
                } else return;
            }
            queue(ctx);
        } catch (Exception ignored) { }
    }

    static void setLinked(Context ctx, boolean on) {
        synchronized (Widgets.class) { linked = on; if (!on) { inDb = -100; tunerOn = false; } }
        queue(ctx);
    }

    private static void queue(Context ctx) {
        final Context app = ctx.getApplicationContext();
        synchronized (Widgets.class) {
            if (drawQueued) return;
            drawQueued = true;
        }
        long wait = Math.max(0, 200 - (SystemClock.uptimeMillis() - lastDraw));
        ui.postDelayed(() -> {
            synchronized (Widgets.class) { drawQueued = false; }
            lastDraw = SystemClock.uptimeMillis();
            draw(app);
        }, wait);
    }

    // ---- drawing ----
    private static boolean on(String id) { Double v = values.get(id); return v != null && v > 0.5; }

    private static final int[] CHIPS = { R.id.chip0, R.id.chip1, R.id.chip2, R.id.chip3, R.id.chip4, R.id.chip5, R.id.chip6, R.id.chip7 };
    private static final int[] WSEG = { R.id.ws0, R.id.ws1, R.id.ws2, R.id.ws3, R.id.ws4, R.id.ws5, R.id.ws6, R.id.ws7, R.id.ws8, R.id.ws9 };
    private static final int[] ISEG = { R.id.is0, R.id.is1, R.id.is2, R.id.is3, R.id.is4, R.id.is5, R.id.is6, R.id.is7, R.id.is8 };
    private static final String[] BANKS = { "GRIT", "GROOVE", "GLOW" };

    static synchronized void draw(Context ctx) {
        AppWidgetManager mgr = AppWidgetManager.getInstance(ctx);
        PendingIntent open = PendingIntent.getActivity(ctx, 0, new Intent(ctx, MainActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP), PendingIntent.FLAG_IMMUTABLE);
        final boolean k = rig == 1;
        final int dimText = k ? 0xFF8A6A50 : 0xFF8A847A;

        // --- what's on ---
        int[] ids = mgr.getAppWidgetIds(new ComponentName(ctx, WhatsOnWidget.class));
        if (ids.length > 0) {
            RemoteViews r = new RemoteViews(ctx.getPackageName(), R.layout.widget_whats_on);
            r.setOnClickPendingIntent(R.id.root, open);
            r.setInt(R.id.root, "setBackgroundResource", k ? R.drawable.widget_bg_kuddle : R.drawable.widget_bg);
            r.setInt(R.id.num, "setBackgroundResource", k ? R.drawable.flip_orange : R.drawable.flip_blue);
            r.setTextColor(R.id.num, k ? 0xFF1A110B : 0xFFFFFFFF);
            int head = current, number = 0;
            while (head > 0 && head < parts.length && parts[head]) head--;
            for (int i = 0; i <= current && i < parts.length; i++) if (!parts[i]) number++;
            String name = head < songs.length ? songs[head] : (k ? "Kuddle" : "Keystone");
            if (current < songs.length && current < parts.length && parts[current]) name += ", " + songs[current];
            r.setTextViewText(R.id.num, number > 0 ? String.format(java.util.Locale.UK, "%02d", number) : "--");
            r.setTextViewText(R.id.song, name);
            r.setTextColor(R.id.song, k ? 0xFFF6EBDD : 0xFFF2EDE4);
            r.setTextViewText(R.id.sub, k ? "KUDDLE" : "KEYSTONE  ·  " + (footMode == 1 ? "SONG MODE" : BANKS[Math.max(0, Math.min(2, bank))]));
            r.setTextViewText(R.id.linked, linked ? "● LINKED" : "NOT LINKED");
            r.setTextColor(R.id.linked, linked ? (k ? 0xFFF28C28 : 0xFF5DCAA5) : dimText);
            String[] names; String[] keys; int[] lit;
            if (k) {
                names = new String[] { "MUTE", "WAH", "OPTO", "TAPE", "TONE", "ROOM" };
                keys = new String[] { "kuddle.muteOn", "kuddle.wahOn", "kuddle.optoOn", "kuddle.tapeOn", "kuddle.toneOn", "kuddle.roomOn" };
                lit = new int[] { R.drawable.chip_kuddle, R.drawable.chip_kuddle, R.drawable.chip_kuddle, R.drawable.chip_kuddle, R.drawable.chip_kuddle, R.drawable.chip_kuddle };
            } else {
                names = new String[] { "DRIVE", "BITROT", "FILTER", "WAH", "SPECKLE", "DELAY", "REVERB", "DRONE" };
                keys = new String[] { "drive.on", "bitrot.on", "filter.on", "wah.on", "grains.on", "delay.on", "reverb.on", null };
                lit = new int[] { R.drawable.chip_grit, R.drawable.chip_grit, R.drawable.chip_groove, R.drawable.chip_groove,
                                  R.drawable.chip_glow, R.drawable.chip_glow, R.drawable.chip_glow, R.drawable.chip_green };
            }
            for (int i = 0; i < CHIPS.length; i++) {
                if (i >= names.length) { r.setViewVisibility(CHIPS[i], android.view.View.INVISIBLE); continue; }
                r.setViewVisibility(CHIPS[i], android.view.View.VISIBLE);
                boolean isOn = linked && (keys[i] == null ? droneLayers > 0 : on(keys[i]));
                r.setTextViewText(CHIPS[i], names[i]);
                r.setInt(CHIPS[i], "setBackgroundResource", isOn ? lit[i] : (k ? R.drawable.chip_off_kuddle : R.drawable.chip_off));
                r.setTextColor(CHIPS[i], isOn ? 0xFF0E0E10 : dimText);
            }
            levelBar(r, WSEG, k);
            r.setTextViewText(R.id.inDb, linked && inDb > -99 ? Math.round(inDb) + " dB" : "-- dB");
            mgr.updateAppWidget(ids, r);
        }

        // --- input, or the tuner while tuning ---
        ids = mgr.getAppWidgetIds(new ComponentName(ctx, InputWidget.class));
        if (ids.length > 0) {
            RemoteViews r = new RemoteViews(ctx.getPackageName(), R.layout.widget_input);
            r.setOnClickPendingIntent(R.id.root, open);
            r.setInt(R.id.root, "setBackgroundResource", k ? R.drawable.widget_bg_kuddle : R.drawable.widget_bg);
            r.setTextColor(R.id.big, k ? 0xFFF6EBDD : 0xFFF2EDE4);
            if (linked && tunerOn) {
                r.setTextViewText(R.id.label, "TUNER");
                r.setTextViewText(R.id.big, tunerValid ? tunerNote : "--");
                boolean inTune = tunerValid && Math.abs(tunerCents) < 3;
                // nine segments: the middle one lit green when in tune, otherwise the one nearest the cents
                int pos = tunerValid ? (int) Math.round(Math.max(-4, Math.min(4, tunerCents / 10.0))) + 4 : -1;
                for (int i = 0; i < ISEG.length; i++) {
                    int res = k ? R.drawable.seg_off_kuddle : R.drawable.seg_off;
                    if (i == pos) res = inTune ? R.drawable.seg_green : R.drawable.seg_orange;
                    r.setInt(ISEG[i], "setBackgroundResource", res);
                }
                String st = !tunerValid ? "Play a note" : inTune ? "In tune" : (tunerCents > 0 ? "+" : "") + Math.round(tunerCents) + " cents, " + (tunerCents > 0 ? "sharp" : "flat");
                r.setTextViewText(R.id.status, st);
                r.setTextColor(R.id.status, inTune ? 0xFF5DCAA5 : tunerValid ? 0xFFF0997B : dimText);
            } else {
                r.setTextViewText(R.id.label, k ? "KUDDLE INPUT" : "INPUT");
                r.setTextViewText(R.id.big, linked && inDb > -99 ? Math.round(inDb) + " dB" : "--");
                levelBar(r, ISEG, k);
                String st; int col;
                if (!linked) { st = "Not linked"; col = dimText; }
                else if (inDb < -45) { st = "No signal"; col = dimText; }
                else if (inDb < -30) { st = "Quiet"; col = 0xFFF0997B; }
                else if (inDb > -1) { st = "Clipping"; col = 0xFFE5534B; }
                else if (inDb > -6) { st = "Hot"; col = 0xFFF0997B; }
                else { st = "Good level"; col = 0xFF5DCAA5; }
                r.setTextViewText(R.id.status, st);
                r.setTextColor(R.id.status, col);
            }
            mgr.updateAppWidget(ids, r);
        }
    }

    // segments across -40 to 0 dB, green, then orange in the top 6 dB
    private static void levelBar(RemoteViews r, int[] seg, boolean k) {
        int n = seg.length;
        double f = linked ? (inDb + 40) / 40.0 : 0;
        int litCount = (int) Math.round(Math.max(0, Math.min(1, f)) * n);
        for (int i = 0; i < n; i++) {
            double segTopDb = -40 + 40.0 * (i + 1) / n;
            int res = i < litCount ? (segTopDb > -6 ? R.drawable.seg_orange : (k ? R.drawable.seg_kuddle : R.drawable.seg_green))
                                   : (k ? R.drawable.seg_off_kuddle : R.drawable.seg_off);
            r.setInt(seg[i], "setBackgroundResource", res);
        }
    }

    // ---- the two providers ----
    public static class WhatsOnWidget extends AppWidgetProvider {
        @Override public void onUpdate(Context ctx, AppWidgetManager mgr, int[] ids) { draw(ctx); }
    }

    public static class InputWidget extends AppWidgetProvider {
        @Override public void onUpdate(Context ctx, AppWidgetManager mgr, int[] ids) { draw(ctx); }
    }
}
