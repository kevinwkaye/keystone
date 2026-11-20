package uk.kevinkaye.keystone.watch;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.WindowManager;

/** The watch face for Keystone: ready, live, tuner. Tap the screen for the tuner. */
public final class WatchActivity extends Activity {
    private WatchView view;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new WatchView(this);
        view.setOnClickListener(v -> {
            WristService s = WristService.running;
            if (s != null && WristService.STATE.connected) s.toggleTuner();
        });
        setContentView(view);
        if (checkSelfPermission("android.permission.POST_NOTIFICATIONS") != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, 1);
        startForegroundService(new Intent(this, WristService.class));
    }

    @Override
    protected void onResume() {
        super.onResume();
        view.start();
    }

    @Override
    protected void onPause() {
        view.stop();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        // swiped away: the link stops too, so the watch isn't left streaming in a bag
        if (isFinishing()) stopService(new Intent(this, WristService.class));
        super.onDestroy();
    }
}
