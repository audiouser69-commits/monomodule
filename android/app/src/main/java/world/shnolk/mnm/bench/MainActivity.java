// Monomodule Bench: the Android load test (see bench.cpp). One screen: choose the Monomachine OS file, measure,
// listen with N tracks and MAKU.
package world.shnolk.mnm.bench;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends Activity {
    static { System.loadLibrary("mnmbench"); }

    private native String nativeLoad(String path);
    private native String nativeBench(int seconds);
    private native String nativeStart(int tracks, boolean maku);
    private native String nativeStatus();
    private native void nativeStop();

    private static final int PICK_OS = 1;
    private TextView log, status, tracksLabel;
    private SeekBar tracks;
    private CheckBox maku;
    private Button listen;
    private boolean playing = false;
    private final Handler ui = new Handler(Looper.getMainLooper());

    private File osFile() { return new File(getFilesDir(), "os.syx"); }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(40, 40, 40, 40);

        TextView title = new TextView(this);
        title.setText("Monomodule Bench");
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(title);
        TextView intro = new TextView(this);
        intro.setText("Misura quante tracce Monomachine emulate e MAKU regge questo telefono, prima del porting completo.");
        col.addView(intro);

        Button pick = new Button(this);
        pick.setText("1. Scegli il file OS (Elektron_SFX6-60_OS1.32B.syx)");
        pick.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, PICK_OS);
        });
        col.addView(pick);

        Button bench = new Button(this);
        bench.setText("2. Misura (circa un minuto)");
        bench.setOnClickListener(v -> run("Misura in corso...", () -> nativeBench(5)));
        col.addView(bench);

        tracksLabel = new TextView(this);
        col.addView(tracksLabel);
        tracks = new SeekBar(this);
        tracks.setMax(6);
        tracks.setProgress(6);
        tracks.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int p, boolean u) { tracksLabel.setText("Tracce da ascoltare: " + p); }
            public void onStartTrackingTouch(SeekBar s) {}
            public void onStopTrackingTouch(SeekBar s) {}
        });
        tracksLabel.setText("Tracce da ascoltare: 6");
        col.addView(tracks);
        maku = new CheckBox(this);
        maku.setText("Con MAKU");
        maku.setChecked(true);
        col.addView(maku);

        listen = new Button(this);
        listen.setText("3. Ascolta");
        listen.setOnClickListener(v -> {
            if (playing) { nativeStop(); playing = false; listen.setText("3. Ascolta"); status.setText(""); return; }
            final int n = tracks.getProgress();
            final boolean m = maku.isChecked();
            run("Avvio...", () -> nativeStart(n, m));
            playing = true;
            listen.setText("Stop");
        });
        col.addView(listen);

        status = new TextView(this);
        status.setTypeface(Typeface.MONOSPACE);
        col.addView(status);
        log = new TextView(this);
        log.setTypeface(Typeface.MONOSPACE);
        log.setTextIsSelectable(true);
        col.addView(log);

        ScrollView sv = new ScrollView(this);
        sv.addView(col, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(sv);

        add("Telefono: " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL + ", Android " + android.os.Build.VERSION.RELEASE
            + ", " + Runtime.getRuntime().availableProcessors() + " core");
        if (osFile().exists()) run("Carico il file OS salvato...", () -> nativeLoad(osFile().getAbsolutePath()));
        tick();
    }

    private void tick() {
        ui.postDelayed(() -> { if (playing) status.setText(nativeStatus()); tick(); }, 500);
    }

    private interface Job { String go(); }
    private void run(String what, Job job) {
        add(what);
        new Thread(() -> { final String r = job.go(); ui.post(() -> add(r)); }).start();
    }

    private void add(String s) { log.append(s + "\n\n"); }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != PICK_OS || res != RESULT_OK || data == null) return;
        final Uri uri = data.getData();
        run("Copio il file OS...", () -> {
            try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(osFile())) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            } catch (Exception e) {
                return "Errore nella copia: " + e.getMessage();
            }
            return nativeLoad(osFile().getAbsolutePath());
        });
    }

    @Override protected void onDestroy() { nativeStop(); super.onDestroy(); }
}
