package uk.kevinkaye.keystone;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * Lets the phone record the room with the app in the background or the screen off.
 * Android only lets an app use the mic out of sight while it shows a notification, and the
 * notification has to start while the app is open, so this starts whenever you open Keystone
 * with the room track on, and stays until you switch the room track off or close the app.
 * It records nothing itself: the recorder starts and stops with Keystone's takes.
 */
public class RoomService extends Service {
    private static final String CHANNEL = "room";
    private static final int ID = 47;
    static volatile boolean running;

    static void start(Context c) {
        if (running) return;
        try {
            Intent i = new Intent(c, RoomService.class);
            if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i);
        } catch (Exception ignored) { }   // not allowed from the background: it starts next time the app opens
    }

    static void stop(Context c) { c.stopService(new Intent(c, RoomService.class)); }

    /** The notification's line: ready, or recording a take. */
    static void say(Context c, String text) {
        if (!running) return;
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        if (nm != null) nm.notify(ID, build(c, text));
    }

    private static Notification build(Context c, String text) {
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        if (Build.VERSION.SDK_INT >= 26 && nm != null && nm.getNotificationChannel(CHANNEL) == null) {
            NotificationChannel ch = new NotificationChannel(CHANNEL, "Room track", NotificationManager.IMPORTANCE_LOW);
            ch.setDescription("Shows while Keystone can record the room for your takes");
            nm.createNotificationChannel(ch);
        }
        PendingIntent open = PendingIntent.getActivity(c, 0, new Intent(c, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(c, CHANNEL) : new Notification.Builder(c);
        return b.setSmallIcon(R.drawable.ic_room)
                .setContentTitle("Keystone room track")
                .setContentText(text)
                .setContentIntent(open)
                .setOngoing(true)
                .setShowWhen(false)
                .build();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Notification n = build(this, RoomRecorder.get(this).recording() ? "Recording the room" : "Ready to record the room with your takes");
        try {
            if (Build.VERSION.SDK_INT >= 29) startForeground(ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
            else startForeground(ID, n);
            running = true;
        } catch (Exception e) {
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }
}
