package uk.kevinkaye.keystone.watch;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.View;

/**
 * Drawn by hand in the Flip Fella look: near black, flip cards with a hinge line, Roboto
 * Condensed, the bank's colour. Redraws 30 times a second while the screen is on.
 */
final class WatchView extends View {
    private static final int BG = 0xFF0E0E10, FLIP = 0xFF262626, HINGE = 0xFF0A0A0A, BTN = 0xFF1C1C20;
    private static final int TEXT = 0xFFF2EDE4, MUTED = 0xFFB9B2A6, DIM = 0xFF8A847A, BLUE = 0xFF4B57FF, GREEN = 0xFF5DCAA5;
    private static final int[] BANK_COL = { 0xFFF0997B, 0xFFB38CF0, 0xFF6FA8FF };
    private static final String[] BANK = { "GRIT", "GROOVE", "GLOW" };

    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint tp = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Typeface face, bold;
    private final RectF r = new RectF();
    private boolean ticking;
    private final Runnable tick = new Runnable() {
        @Override public void run() { invalidate(); if (ticking) postDelayed(this, 33); }
    };

    WatchView(Context c) {
        super(c);
        Typeface f;
        try { f = Typeface.createFromAsset(c.getAssets(), "RobotoCondensed.ttf"); } catch (Exception e) { f = Typeface.DEFAULT; }
        face = f;
        bold = Typeface.create(f, Typeface.BOLD);
        setClickable(true);
    }

    void start() { ticking = true; removeCallbacks(tick); post(tick); }
    void stop() { ticking = false; removeCallbacks(tick); }

    @Override
    protected void onDraw(Canvas c) {
        final float w = getWidth(), h = getHeight(), cx = w / 2, cy = h / 2, u = Math.min(w, h) / 100f;
        c.drawColor(BG);
        WristService.State s = WristService.STATE;
        if (WristService.running == null || !s.connected) { drawSearching(c, cx, cy, u, s.status); return; }
        if (s.tunerOn) { drawTuner(c, cx, cy, u, s); return; }

        final int bank = Math.max(0, Math.min(2, s.bank));
        final int col = BANK_COL[bank];
        final boolean live = s.live;
        final float v = s.jobv;

        // the sweep arc: grey track, filled in the bank's colour when live, dim blue when not
        float rad = 46 * u;
        r.set(cx - rad, cy - rad, cx + rad, cy + rad);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setStrokeWidth(4.5f * u);
        p.setColor(FLIP);
        c.drawArc(r, 135, 270, false, p);
        p.setColor(live ? col : BLUE);
        if (v > 0.002f) c.drawArc(r, 135, 270 * Math.min(1f, v), false, p);
        p.setStyle(Paint.Style.FILL);

        text(c, live ? "LIVE" : BANK[bank], cx, cy - 25 * u, 7 * u, live ? col : MUTED, false, 0.12f);
        flipPair(c, cx, cy - 4 * u, u, Math.min(99, Math.round(v * 100)), FLIP);
        text(c, s.jobName.isEmpty() ? "Pedal" : s.jobName, cx, cy + 20 * u, 8 * u, TEXT, true, 0);
        String hint = bank != 0 ? "The watch works in Grit" : live ? "Let go of C to glide back" : "Hold C to go live";
        text(c, hint, cx, cy + 30 * u, 6 * u, DIM, false, 0);
        text(c, "Tap for the tuner", cx, cy + 38 * u, 5.5f * u, DIM, false, 0);
    }

    private void drawSearching(Canvas c, float cx, float cy, float u, String status) {
        text(c, "KEYSTONE", cx, cy - 26 * u, 7 * u, MUTED, false, 0.12f);
        p.setColor(BTN);
        c.drawCircle(cx, cy - 4 * u, 12 * u, p);
        long t = System.currentTimeMillis() / 400 % 3;
        for (int i = 0; i < 3; i++) {
            p.setColor(i == t ? 0xFFF0997B : FLIP);
            c.drawCircle(cx + (i - 1) * 7 * u, cy - 4 * u, 2.2f * u, p);
        }
        tp.setTypeface(face);
        tp.setTextSize(6 * u);
        tp.setColor(DIM);
        StaticLayout sl = StaticLayout.Builder.obtain(status == null ? "" : status, 0, status == null ? 0 : status.length(), tp, (int) (64 * u))
                .setAlignment(Layout.Alignment.ALIGN_CENTER).build();
        c.save();
        c.translate(cx - 32 * u, cy + 12 * u);
        sl.draw(c);
        c.restore();
    }

    private void drawTuner(Canvas c, float cx, float cy, float u, WristService.State s) {
        text(c, "TUNER", cx, cy - 28 * u, 7 * u, GREEN, false, 0.12f);
        String note = s.tunerValid && !s.note.isEmpty() ? s.note : "-";
        boolean inTune = s.tunerValid && Math.abs(s.cents) < 3;
        // the note on one big flip card, blue when in tune
        float cw = 30 * u, ch = 30 * u;
        r.set(cx - cw / 2, cy - 18 * u, cx + cw / 2, cy - 18 * u + ch);
        p.setColor(inTune ? BLUE : FLIP);
        c.drawRoundRect(r, 2 * u, 2 * u, p);
        text(c, note, cx, r.centerY() + 6.5f * u, 19 * u, 0xFFFFFFFF, true, 0);
        p.setColor(HINGE);
        c.drawRect(r.left, r.centerY() - 0.4f * u, r.right, r.centerY() + 0.4f * u, p);
        // cents: a bar from flat to sharp with the needle
        float bw = 56 * u, by = cy + 22 * u;
        p.setColor(FLIP);
        c.drawRoundRect(cx - bw / 2, by - 1.2f * u, cx + bw / 2, by + 1.2f * u, u, u, p);
        p.setColor(MUTED);
        c.drawRect(cx - 0.4f * u, by - 3 * u, cx + 0.4f * u, by + 3 * u, p);
        if (s.tunerValid) {
            float x = cx + Math.max(-1f, Math.min(1f, s.cents / 50f)) * bw / 2;
            p.setColor(inTune ? GREEN : 0xFFF0997B);
            c.drawCircle(x, by, 3 * u, p);
            text(c, (s.cents > 0 ? "+" : "") + Math.round(s.cents) + " cents", cx, by + 11 * u, 6 * u, MUTED, false, 0);
        }
        text(c, "Tap to leave", cx, cy + 41 * u, 5.5f * u, DIM, false, 0);
    }

    // two flip cards with the hinge, like the phone's
    private void flipPair(Canvas c, float cx, float top, float u, int value, int col) {
        String d = String.format(java.util.Locale.ROOT, "%02d", value);
        float cw = 14 * u, ch = 19 * u, gap = 1.5f * u;
        for (int i = 0; i < 2; i++) {
            float left = cx - cw - gap / 2 + i * (cw + gap);
            r.set(left, top - ch / 2, left + cw, top + ch / 2);
            p.setColor(col);
            c.drawRoundRect(r, 1.6f * u, 1.6f * u, p);
            text(c, d.substring(i, i + 1), r.centerX(), r.centerY() + 5 * u, 14 * u, 0xFFFFFFFF, true, 0);
            p.setColor(HINGE);
            c.drawRect(r.left, r.centerY() - 0.35f * u, r.right, r.centerY() + 0.35f * u, p);
        }
    }

    private void text(Canvas c, String s, float x, float y, float size, int color, boolean strong, float spacing) {
        p.setTypeface(strong ? bold : face);
        p.setTextSize(size);
        p.setColor(color);
        p.setLetterSpacing(spacing);
        p.setTextAlign(Paint.Align.CENTER);
        c.drawText(s, x, y, p);
        p.setLetterSpacing(0);
    }
}
