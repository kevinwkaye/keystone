package uk.kevinkaye.keystone;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;
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
    private ConnectivityManager.NetworkCallback wifiCallback;

    @SuppressLint({ "SetJavaScriptEnabled", "AddJavascriptInterface" })
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

        preferLocalWifi();

        link = new Link(this, new Link.Listener() {
            @Override
            public void onLine(String line) {
                synchronized (inbox) {
                    inbox.add(line);
                    if (flushQueued) return;
                    flushQueued = true;
                }
                ui.postDelayed(MainActivity.this::flush, 12);
            }

            @Override
            public void onStatus(String status, String detail) {
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

    // If the phone joins a Wi-Fi network with no internet instead of hosting the hotspot,
    // Android would send traffic over mobile data. Pin it to the Wi-Fi network.
    private void preferLocalWifi() {
        final ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) return;
        NetworkRequest req = new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).build();
        wifiCallback = new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network n) { cm.bindProcessToNetwork(n); }
            @Override public void onLost(Network n) { cm.bindProcessToNetwork(null); }
        };
        try { cm.registerNetworkCallback(req, wifiCallback); } catch (Exception ignored) { }
    }

    @Override
    public void onBackPressed() {
        web.evaluateJavascript("KS.back()", value -> {
            if (!"true".equals(value)) moveTaskToBack(true);
        });
    }

    @Override
    protected void onDestroy() {
        link.stop();
        ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm != null && wifiCallback != null) {
            try { cm.unregisterNetworkCallback(wifiCallback); } catch (Exception ignored) { }
            cm.bindProcessToNetwork(null);
        }
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
    }
}
