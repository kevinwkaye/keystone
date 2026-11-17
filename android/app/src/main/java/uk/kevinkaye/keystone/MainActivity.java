package uk.kevinkaye.keystone;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/**
 * Keystone's phone controller: the Flip Fella screens (bundled, so nothing loads from the
 * internet), kept awake, linked to Keystone on the hotspot.
 */
public class MainActivity extends Activity {
    private WebView web;
    private Link link;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final List<String> inbox = new ArrayList<>();
    private boolean flushQueued;
    private boolean pageReady;
    private String pendingStatus;
    private volatile boolean linked;
    private RoomRecorder room;
    private Updater updater;

    @SuppressLint({ "SetJavaScriptEnabled", "AddJavascriptInterface" })
    // No network pinning: the phone is often on home Wi-Fi and hosting the hotspot at once,
    // and the laptop sits on the hotspot. The system routes each address to the right one.
    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(Color.parseColor("#0E0E10"));
        getWindow().setNavigationBarColor(Color.parseColor("#0E0E10"));

        web = new WebView(this);
        web.setBackgroundColor(Color.parseColor("#0E0E10"));
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);
        s.setTextZoom(100);
        s.setMediaPlaybackRequiresUserGesture(false);   // the listening pack plays fetched files one after another
        web.setWebChromeClient(new WebChromeClient());
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView view, String url) {
                pageReady = true;
                if (pendingStatus != null) deliverStatus(pendingStatus);
            }
        });
        web.addJavascriptInterface(new Bridge(), "KeystoneNative");
        setContentView(web);
        web.loadUrl("file:///android_asset/index.html");

        room = RoomRecorder.get(this);
        room.setSender(new RoomRecorder.Sender() {
            @Override public boolean send(String line) { if (!linked) return false; link.send(line); return true; }
            @Override public boolean linked() { return linked; }
        });

        updater = new Updater(this,
                json -> ui.post(() -> { if (pageReady) web.evaluateJavascript("KS.onUpdate(" + json + ")", null); }),
                line -> { if (!linked) return false; link.send(line); return true; });

        link = new Link(this, new Link.Listener() {
            @Override
            public void onLine(String line) {
                if (line.contains("\"t\":\"fwAck\"")) {
                    try { updater.onAck(new JSONObject(line)); } catch (Exception ignored) { }
                    return;
                }
                if (line.contains("\"t\":\"roomAck\"")) {
                    try { room.onAck(new JSONObject(line)); } catch (Exception ignored) { }
                    return;
                }
                if (line.contains("\"t\":\"meter\"") && line.contains("\"rec\"")) {
                    // the room track follows Keystone's takes here, so it works with the screen off
                    try { room.sync(new JSONObject(line).optJSONObject("rec")); } catch (Exception ignored) { }
                }
                if (!line.contains("\"t\":\"listenChunk\"")) Widgets.onLine(MainActivity.this, line);   // audio pieces are only for the page
                synchronized (inbox) {
                    inbox.add(line);
                    if (flushQueued) return;
                    flushQueued = true;
                }
                ui.postDelayed(MainActivity.this::flush, 12);
            }

            @Override
            public void onStatus(String status, String detail) {
                linked = "connected".equals(status);
                Widgets.setLinked(MainActivity.this, linked);
                if (linked) room.kick();   // anything left over from last time
                ui.post(() -> deliverStatus(JSONObject.quote(status) + "," + JSONObject.quote(detail)));
            }
        });
        link.start();
    }

    // Lines arrive in bursts (meters, state); hand them to the page in one go.
    private void flush() {
        List<String> lines;
        synchronized (inbox) {
            lines = new ArrayList<>(inbox);
            inbox.clear();
            flushQueued = false;
        }
        if (!pageReady || lines.isEmpty()) return;
        StringBuilder js = new StringBuilder();
        for (String l : lines) js.append("KS.onMessage(").append(JSONObject.quote(l)).append(");");
        web.evaluateJavascript(js.toString(), null);
    }

    private void deliverStatus(String args) {
        if (!pageReady) { pendingStatus = args; return; }
        pendingStatus = null;
        web.evaluateJavascript("KS.onLink(" + args + ")", null);
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        roomService();
        if (pageReady) web.evaluateJavascript("KS.refresh()", null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        roomService();
    }

    // the background mic needs its notification started while the app is open
    private void roomService() {
        boolean allowed = checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
        if (room.wanted() && allowed) RoomService.start(this);
        else if (!room.wanted()) RoomService.stop(this);
    }

    @Override
    public void onBackPressed() {
        web.evaluateJavascript("KS.back()", value -> {
            if (!"true".equals(value)) moveTaskToBack(true);
        });
    }

    @Override
    protected void onDestroy() {
        room.stop();
        RoomService.stop(this);
        link.stop();
        Widgets.setLinked(this, false);
        web.destroy();
        super.onDestroy();
    }

    final class Bridge {
        @JavascriptInterface
        public void send(String line) { link.send(line); }

        @JavascriptInterface
        public void connectTo(String host) { link.connectTo(host); }

        @JavascriptInterface
        public String version() { return BuildConfig.VERSION_NAME; }

        // the room track for rehearsal takes
        @JavascriptInterface
        public boolean micAllowed() { return checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED; }

        @JavascriptInterface
        public void askMic() {
            // the notification that lets the room record with the screen off, too (Android 13 on)
            String[] ask = android.os.Build.VERSION.SDK_INT >= 33
                    ? new String[] { Manifest.permission.RECORD_AUDIO, Manifest.permission.POST_NOTIFICATIONS }
                    : new String[] { Manifest.permission.RECORD_AUDIO };
            ui.post(() -> requestPermissions(ask, 7));
        }

        // the room track setting: the recording itself follows Keystone's takes natively
        @JavascriptInterface
        public void roomSetting(boolean on) {
            if (on == room.wanted()) return;
            room.setWanted(on);
            if (!on) room.stop();
            ui.post(MainActivity.this::roomService);
        }

        @JavascriptInterface
        public boolean roomStart(String take, double at) { return false; }   // older pages

        @JavascriptInterface
        public void roomStop() { }

        @JavascriptInterface
        public String roomStatus() { return room.statusJson(); }

        // Keystone's own Wi-Fi: save it to this phone, so it joins by itself from then on
        @JavascriptInterface
        public void wifiAdd(String ssid, String pass) {
            ui.post(() -> {
                try {
                    if (android.os.Build.VERSION.SDK_INT >= 30) {
                        android.net.wifi.WifiNetworkSuggestion n = new android.net.wifi.WifiNetworkSuggestion.Builder()
                                .setSsid(ssid).setWpa2Passphrase(pass).build();
                        android.os.Bundle b = new android.os.Bundle();
                        b.putParcelableArrayList(android.provider.Settings.EXTRA_WIFI_NETWORK_LIST, new ArrayList<>(java.util.Collections.singletonList(n)));
                        startActivity(new android.content.Intent(android.provider.Settings.ACTION_WIFI_ADD_NETWORKS).putExtras(b));
                    } else {
                        startActivity(new android.content.Intent(android.provider.Settings.ACTION_WIFI_SETTINGS));
                    }
                } catch (Exception e) {
                    startActivity(new android.content.Intent(android.provider.Settings.ACTION_WIFI_SETTINGS));
                }
            });
        }

        // Update firmware and the phone app
        @JavascriptInterface
        public void updateCheck() { updater.check(); }

        @JavascriptInterface
        public void updateFirmware(String url, String name, double size, String digest, String version) { updater.firmware(url, name, (long) size, digest, version); }

        @JavascriptInterface
        public void updateApp(String url, double size) { updater.app(url, (long) size); }
    }
}
