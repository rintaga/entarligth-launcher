package com.entarligth.launcher;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class MainActivity extends Activity {
    private static final String MAIN_CLASS = "net.minecraft.client.Main";
    private static final String HEAP = "-Xmx1024M"; // 32-bit: jangan terlalu besar

    private TextView status, out;
    private LinearLayout buttons;
    private PS5Controller controller;
    private File jre, game;
    private volatile boolean started;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        jre = new File(new File(getFilesDir(), ".plugin"), "jre"); // folder tersembunyi
        game = new File(getFilesDir(), "game");
        game.mkdirs();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 32, 32, 32);

        status = new TextView(this);
        status.setTextSize(16);

        buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.VERTICAL);
        buttons.setVisibility(View.GONE);
        Button login = new Button(this);
        login.setText("Login Microsoft");
        login.setOnClickListener(v -> { buttons.setVisibility(View.GONE); new Thread(this::login).start(); });
        Button offline = new Button(this);
        offline.setText("Main offline");
        offline.setOnClickListener(v -> {
            buttons.setVisibility(View.GONE);
            MsAuth.Account a = new MsAuth.Account();
            a.name = "Player"; a.uuid = "00000000000000000000000000000000"; a.token = "0";
            new Thread(() -> launch(a)).start();
        });
        buttons.addView(login);
        buttons.addView(offline);

        out = new TextView(this);
        out.setTextIsSelectable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(out);

        root.addView(status);
        root.addView(buttons);
        root.addView(sv, new LinearLayout.LayoutParams(-1, 0, 1f));
        FrameLayout frame = new FrameLayout(this);
        frame.addView(root);
        controller = new PS5Controller(this);
        controller.setVisibility(View.GONE);
        frame.addView(controller, new FrameLayout.LayoutParams(-1, -1));
        setContentView(frame);

        new Thread(this::prepare, "lumina-prepare").start(); // langsung loading saat dibuka
    }

    private void prepare() {
        try {
            extractRuntime();
            extractGame();
            downloadAssets();
            setStatus("Memeriksa akun...");
            MsAuth.Account a = MsAuth.restore(this);
            if (a != null) launch(a);
            else runOnUiThread(() -> { setStatus("Login untuk bermain online"); buttons.setVisibility(View.VISIBLE); });
        } catch (FileNotFoundException e) {
            log("File bawaan APK tidak ada (" + e.getMessage() + "). Periksa build GitHub (JRE_ARM_URL, client.jar).");
        } catch (Throwable t) {
            log("Error: " + t);
        }
    }

    private void login() {
        try {
            MsAuth.DeviceCode d = MsAuth.startDeviceLogin();
            runOnUiThread(() -> {
                setStatus("Buka " + d.uri + " lalu masukkan kode:\n" + d.userCode);
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(d.uri)));
            });
            MsAuth.Account a = MsAuth.finishDeviceLogin(this, d);
            launch(a);
        } catch (Throwable t) {
            log("Login gagal: " + t);
            runOnUiThread(() -> buttons.setVisibility(View.VISIBLE));
        }
    }

    // ---- runtime & game files ----

    private void extractRuntime() throws IOException {
        File ok = new File(jre, ".ok");
        if (ok.exists()) return;
        setStatus("Mengekstrak runtime Java...");
        try (InputStream in = getAssets().open("jre-arm.zip")) { unzip(in, jre); }
        ok.createNewFile();
    }

    private void extractGame() throws IOException {
        for (String n : new String[]{"client.jar", "server.jar"}) {
            File dst = new File(game, n);
            long len = getAssets().openFd("game/" + n).getLength();
            if (dst.exists() && dst.length() == len) continue;
            setStatus("Menyalin " + n + "...");
            try (InputStream in = getAssets().open("game/" + n);
                 OutputStream os = new FileOutputStream(dst)) { copy(in, os); }
        }
        File libs = new File(game, "libraries");
        File ok = new File(libs, ".ok");
        if (!ok.exists()) {
            setStatus("Mengekstrak library...");
            try (InputStream in = getAssets().open("game/libraries.zip")) { unzip(in, libs); }
            ok.createNewFile();
        }
    }

    private void downloadAssets() throws Exception {
        String id = asset("game/assetindex.id").trim();
        String idx = asset("game/assetindex.json");
        File idxf = new File(game, "assets/indexes/" + id + ".json");
        idxf.getParentFile().mkdirs();
        try (OutputStream os = new FileOutputStream(idxf)) { os.write(idx.getBytes("UTF-8")); }

        JSONObject objs = new JSONObject(idx).getJSONObject("objects");
        List<String> todo = new ArrayList<>();
        for (Iterator<String> it = objs.keys(); it.hasNext(); ) {
            String h = objs.getJSONObject(it.next()).getString("hash");
            if (!new File(game, "assets/objects/" + h.substring(0, 2) + "/" + h).exists()) todo.add(h);
        }
        if (todo.isEmpty()) return;

        final int total = todo.size();
        AtomicInteger done = new AtomicInteger();
        ExecutorService ex = Executors.newFixedThreadPool(8);
        List<Future<?>> fs = new ArrayList<>();
        for (String h : todo) {
            fs.add(ex.submit(() -> {
                File f = new File(game, "assets/objects/" + h.substring(0, 2) + "/" + h);
                download("https://resources.download.minecraft.net/" + h.substring(0, 2) + "/" + h, f);
                int d = done.incrementAndGet();
                if (d % 50 == 0 || d == total) setStatus("Mengunduh assets " + d + "/" + total);
                return null;
            }));
        }
        try { for (Future<?> f : fs) f.get(); } finally { ex.shutdown(); }
    }

    private static void download(String url, File dst) throws IOException {
        dst.getParentFile().mkdirs();
        File tmp = new File(dst.getPath() + ".part");
        IOException last = null;
        for (int i = 0; i < 3; i++) {
            try {
                HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
                c.setConnectTimeout(20000);
                c.setReadTimeout(30000);
                try (InputStream in = c.getInputStream(); OutputStream os = new FileOutputStream(tmp)) { copy(in, os); }
                if (!tmp.renameTo(dst)) throw new IOException("rename gagal");
                return;
            } catch (IOException e) { last = e; }
        }
        throw last;
    }

    // ---- launch ----

    private void launch(MsAuth.Account acc) {
        if (started) return;
        started = true;
        try {
            setStatus("Memuat Java...");
            runOnUiThread(() -> controller.setVisibility(View.VISIBLE));
            String version = asset("game/version.id").trim();
            String assetId = asset("game/assetindex.id").trim();

            List<String> cp = new ArrayList<>();
            cp.add(new File(game, "client.jar").getPath());
            collect(new File(game, "libraries"), cp);

            List<String> a = new ArrayList<>();
            a.add("-Xms256M");
            a.add(HEAP);
            a.add("-Djava.home=" + jre);
            a.add("-Djava.io.tmpdir=" + getCacheDir());
            a.add("-Duser.home=" + game);
            a.add("-Dos.name=Linux");
            a.add("-cp");
            a.add(String.join(":", cp));
            a.add(MAIN_CLASS);
            a.addAll(Arrays.asList(
                    "--username", acc.name, "--uuid", acc.uuid, "--accessToken", acc.token,
                    "--userType", "msa", "--version", version, "--versionType", "release",
                    "--gameDir", game.getPath(),
                    "--assetsDir", new File(game, "assets").getPath(),
                    "--assetIndex", assetId));

            File logf = new File(game, "latestlog.txt");
            int rc = NativeBridge.launch(jre.getPath(), game.getPath(), logf.getPath(),
                    a.toArray(new String[0]));
            log("exit code " + rc + "\n" + tail(logf));
        } catch (Throwable t) {
            log("Error: " + t);
        } finally {
            started = false;
        }
    }

    // ---- util ----

    private static void collect(File dir, List<String> cp) {
        File[] fs = dir.listFiles();
        if (fs == null) return;
        for (File f : fs) {
            if (f.isDirectory()) collect(f, cp);
            else if (f.getName().endsWith(".jar")) cp.add(f.getPath());
        }
    }

    private String asset(String name) throws IOException {
        try (InputStream in = getAssets().open(name)) {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            copy(in, bo);
            return bo.toString("UTF-8");
        }
    }

    private static void unzip(InputStream in, File dest) throws IOException {
        dest.mkdirs();
        String base = dest.getCanonicalPath() + File.separator;
        try (ZipInputStream z = new ZipInputStream(new BufferedInputStream(in))) {
            ZipEntry e;
            while ((e = z.getNextEntry()) != null) {
                File f = new File(dest, e.getName());
                if (!f.getCanonicalPath().startsWith(base)) continue; // zip-slip
                if (e.isDirectory()) { f.mkdirs(); continue; }
                f.getParentFile().mkdirs();
                try (OutputStream os = new FileOutputStream(f)) { copy(z, os); }
            }
        }
    }

    private static void copy(InputStream in, OutputStream os) throws IOException {
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
    }

    private static String tail(File f) {
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            long len = r.length(), from = Math.max(0, len - 8000);
            byte[] b = new byte[(int) (len - from)];
            r.seek(from);
            r.readFully(b);
            return new String(b);
        } catch (IOException e) { return "(log kosong)"; }
    }

    private void setStatus(String s) { runOnUiThread(() -> status.setText(s)); }
    private void log(String s) { runOnUiThread(() -> out.append(s + "\n")); }
}
