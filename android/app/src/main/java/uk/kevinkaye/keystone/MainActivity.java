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

        room = new RoomRecorder(this, new RoomRecorder.Sender() {
            @Override public boolean send(String line) { if (!linked) return false; link.send(line); return true; }
            @Override public boolean linked() { return linked; }
        });

        link = new Link(this, new Link.Listener() {
            @Override
            public void onLine(String line) {
                if (line.contains("\"t\":\"roomAck\"")) {
                    try { room.onAck(new JSONObject(line)); } catch (Exception ignored) { }
                    return;
                }
                Widgets.onLine(MainActivity.this, line);
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
        if (pageReady) web.evaluateJavascript("KS.refresh()", null);
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
        public void askMic() { ui.post(() -> requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, 7)); }

        @JavascriptInterface
        public boolean roomStart(String take, double at) { return micAllowed() && room.start(MainActivity.this, take, at); }

        @JavascriptInterface
        public void roomStop() { room.stop(); }

        @JavascriptInterface
        public String roomStatus() { return room.statusJson(); }
    }
}
