package com.entarligth.launcher;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.*;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class MainActivity extends Activity {
    private static final int REQ_JAR = 1;
    private static final String MAIN_CLASS = "net.minecraft.client.main.Main";
    private static final String HEAP = "-Xmx1024M";

    private TextView out;
    private File jre, game;
    private volatile boolean started;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        File plugin = new File(getFilesDir(), ".plugin"); // folder tersembunyi
        jre = new File(plugin, "jre");
        game = new File(getFilesDir(), "game");
        game.mkdirs();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 32, 32, 32);

        Button imp = new Button(this);
        imp.setText("Import client.jar");
        imp.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_JAR);
        });

        out = new TextView(this);
        out.setTextIsSelectable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(out);

        root.addView(imp);
        root.addView(sv, new LinearLayout.LayoutParams(-1, 0, 1f));
        setContentView(root);

        begin(); // langsung loading saat pertama dibuka
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        if (req != REQ_JAR || res != RESULT_OK || data == null) return;
        Uri u = data.getData();
        new Thread(() -> {
            try (InputStream in = getContentResolver().openInputStream(u);
                 OutputStream os = new FileOutputStream(new File(game, "client.jar"))) {
                copy(in, os);
                log("client.jar diimpor.");
                begin();
            } catch (Throwable t) { log("Import gagal: " + t); }
        }).start();
    }

    private void begin() {
        if (started) return;
        started = true;
        new Thread(this::start, "lumina-launch").start();
    }

    private void start() {
        try {
            String abi = Build.SUPPORTED_64_BIT_ABIS.length > 0 ? "arm64" : "arm";
            File ok = new File(jre, ".ok");
            if (!ok.exists()) {
                log("Mengekstrak runtime (" + abi + ")...");
                try (InputStream in = getAssets().open("jre-" + abi + ".zip")) { unzip(in, jre); }
                ok.createNewFile();
            }

            File jar = new File(game, "client.jar");
            List<String> a = new ArrayList<>();
            a.add("-Xms256M");
            a.add(HEAP);
            a.add("-Djava.home=" + jre);
            a.add("-Djava.io.tmpdir=" + getCacheDir());
            a.add("-Duser.home=" + game);
            a.add("-Dos.name=Linux");
            if (jar.exists()) {
                a.add("-cp");
                a.add(classpath(jar));
                a.add(MAIN_CLASS);
                a.addAll(Arrays.asList("--username", "Player", "--version", "entarligth",
                        "--gameDir", game.getPath(),
                        "--assetsDir", new File(game, "assets").getPath(),
                        "--accessToken", "0"));
            } else {
                a.add("-version"); // uji JVM
            }

            File logf = new File(game, "latestlog.txt");
            int rc = NativeBridge.launch(jre.getPath(), game.getPath(), logf.getPath(),
                    a.toArray(new String[0]));
            log("exit code " + rc + "\n" + tail(logf));
        } catch (FileNotFoundException e) {
            log("Runtime tidak ada di APK. Isi variabel JRE_ARM64_URL / JRE_ARM_URL di GitHub, lalu build ulang.");
        } catch (Throwable t) {
            log("Error: " + t);
        } finally {
            started = false;
        }
    }

    private String classpath(File jar) {
        List<String> cp = new ArrayList<>();
        cp.add(jar.getPath());
        collect(new File(game, "libraries"), cp);
        return String.join(":", cp);
    }

    private static void collect(File dir, List<String> cp) {
        File[] fs = dir.listFiles();
        if (fs == null) return;
        for (File f : fs) {
            if (f.isDirectory()) collect(f, cp);
            else if (f.getName().endsWith(".jar")) cp.add(f.getPath());
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

    private void log(String s) {
        runOnUiThread(() -> out.append(s + "\n"));
    }
}
