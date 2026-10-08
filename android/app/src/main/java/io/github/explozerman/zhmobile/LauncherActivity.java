package io.github.explozerman.zhmobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Launcher: shows whether the game data is installed, imports it from the player's
 * own copy, lets them pick the language (Russian / English) and render resolution,
 * and starts {@link GameActivity}.
 */
public class LauncherActivity extends Activity {
    private static final int REQ_PICK_FOLDER = 1;
    private static final int[] SCALES = { 100, 75, 50 };

    private static final int COLOR_BG = 0xFF14171A;
    private static final int COLOR_PANEL = 0xFF1F2429;
    private static final int COLOR_ACCENT = 0xFFE0A526;
    private static final int COLOR_TEXT = 0xFFE8E6E1;
    private static final int COLOR_MUTED = 0xFF9AA1A8;

    // An import survives activity recreation (language / resolution switch); only one
    // may run at a time. Progress is reported to whichever launcher instance is current.
    private static GameImporter s_runningImport;
    private static LauncherActivity s_current;
    private static String s_progressLine = "";
    private static int s_progress;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private LinearLayout root;
    private TextView statusView;
    private Button playButton;
    private Button importButton;
    private ProgressBar progressBar;
    private TextView progressText;

    private String t(String key) {
        return L10n.t(this, key);
    }

    private File gameDataDir() {
        return new File(getExternalFilesDir(null), "GameData");
    }

    private File logsDir() {
        return new File(getExternalFilesDir(null), "logs");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(COLOR_BG);
        getWindow().setNavigationBarColor(COLOR_BG);
        s_current = this;
        installBundledGameData();
        buildUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshStatus();
    }

    // ------------------------------------------------------------------ UI

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(String s, float sp, int color, boolean bold) {
        TextView tv = new TextView(this);
        tv.setText(s);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        tv.setTextColor(color);
        if (bold) {
            tv.setTypeface(Typeface.DEFAULT_BOLD);
        }
        return tv;
    }

    private Button button(String label, boolean primary, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, primary ? 20 : 15);
        b.setTextColor(primary ? Color.BLACK : COLOR_TEXT);
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(10));
        bg.setColor(primary ? COLOR_ACCENT : COLOR_PANEL);
        if (!primary) {
            bg.setStroke(dp(1), 0xFF3A424A);
        }
        b.setBackground(bg);
        b.setPadding(dp(16), dp(primary ? 14 : 10), dp(16), dp(primary ? 14 : 10));
        b.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    private LinearLayout row() {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        return r;
    }

    private Button choice(String label, boolean selected, View.OnClickListener onClick) {
        Button b = button(label, false, onClick);
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(10));
        bg.setColor(selected ? 0xFF3B3320 : COLOR_PANEL);
        bg.setStroke(dp(selected ? 2 : 1), selected ? COLOR_ACCENT : 0xFF3A424A);
        b.setBackground(bg);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        lp.topMargin = dp(6);
        lp.rightMargin = dp(6);
        b.setLayoutParams(lp);
        return b;
    }

    private void buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(COLOR_BG);
        scroll.setFillViewport(true);

        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);
        columns.setPadding(dp(24), dp(16), dp(24), dp(16));
        scroll.addView(columns);

        // Left column: title, status, main actions
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        columns.addView(root, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.1f));

        TextView title = text("ZH Mobile", 30, COLOR_ACCENT, true);
        title.setLetterSpacing(0.04f);
        root.addView(title);
        root.addView(text(t("subtitle"), 14, COLOR_MUTED, false));

        statusView = text("", 14, COLOR_TEXT, false);
        statusView.setPadding(0, dp(14), 0, dp(6));
        statusView.setLineSpacing(dp(3), 1f);
        root.addView(statusView);

        playButton = button(t("play"), true, v -> startGame());
        root.addView(playButton);
        importButton = button(t("import"), false, v -> askImport());
        root.addView(importButton);

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(1000);
        progressBar.setVisibility(View.GONE);
        root.addView(progressBar);
        progressText = text("", 12, COLOR_MUTED, false);
        progressText.setVisibility(View.GONE);
        root.addView(progressText);

        // Right column: settings + help
        LinearLayout side = new LinearLayout(this);
        side.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams sideLp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        sideLp.leftMargin = dp(28);
        columns.addView(side, sideLp);

        side.addView(text(t("language"), 13, COLOR_MUTED, true));
        LinearLayout langRow = row();
        String lang = L10n.lang(this);
        langRow.addView(choice("Русский", L10n.RU.equals(lang), v -> switchLanguage(L10n.RU)));
        langRow.addView(choice("English", L10n.EN.equals(lang), v -> switchLanguage(L10n.EN)));
        side.addView(langRow);

        TextView resLabel = text(t("resolution"), 13, COLOR_MUTED, true);
        resLabel.setPadding(0, dp(14), 0, 0);
        side.addView(resLabel);
        LinearLayout resRow = row();
        int scale = L10n.prefs(this).getInt("scale", 100);
        for (int s : SCALES) {
            final int value = s;
            resRow.addView(choice(s + "%", s == scale, v -> {
                L10n.prefs(this).edit().putInt("scale", value).apply();
                recreate();
            }));
        }
        side.addView(resRow);
        side.addView(text(t("res_hint"), 12, COLOR_MUTED, false));

        side.addView(button(t("help"), false, v -> showText(t("help"), t("help_text"))));
        side.addView(button(t("save_log"), false, v -> saveLog()));
        side.addView(button(t("about"), false, v -> showText(t("about"), t("about_text") + versionLine())));

        setContentView(scroll);
        if (s_runningImport != null) {
            setImporting(true);
            progressBar.setProgress(s_progress);
            progressText.setText(s_progressLine);
        }
    }

    private String versionLine() {
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            return "\n\nv" + pi.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "";
        }
    }

    private void showText(String title, String body) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle(title)
            .setMessage(body)
            .setPositiveButton(t("ok"), null)
            .show();
    }

    private void switchLanguage(String lang) {
        L10n.setLang(this, lang);
        recreate();
    }

    // ------------------------------------------------------------------ status

    private boolean exists(String rel) {
        return new File(gameDataDir(), rel).isFile();
    }

    private boolean hasZeroHour() {
        return exists("INIZH.big");
    }

    private boolean hasBaseGame() {
        return exists("ZH_Generals/INI.big");
    }

    private boolean hasRussianPack() {
        File[] files = new File(gameDataDir(), "lang/ru").listFiles();
        return files != null && files.length > 0;
    }

    private boolean hasVulkan11() {
        return getPackageManager().hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION, 0x401000);
    }

    private void refreshStatus() {
        StringBuilder sb = new StringBuilder();
        boolean ready = hasZeroHour() && hasBaseGame();
        if (!hasVulkan11()) {
            sb.append(t("no_vulkan")).append('\n');
        }
        if (!hasZeroHour() && !hasBaseGame()) {
            sb.append(t("status_missing"));
        } else {
            if (ready) {
                sb.append(t("status_ok"));
            } else if (!hasZeroHour()) {
                sb.append(t("status_zh_missing"));
            } else {
                sb.append(t("status_base_missing"));
            }
            sb.append('\n');
            sb.append(hasRussianPack() ? t("status_ru_ok") : t("status_ru_missing"));
        }
        statusView.setText(sb.toString());
        playButton.setEnabled(ready && s_runningImport == null);
        playButton.setAlpha(playButton.isEnabled() ? 1f : 0.4f);
    }

    // ------------------------------------------------------------------ game

    private void startGame() {
        installBundledGameData();
        int scale = L10n.prefs(this).getInt("scale", 100);
        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra(GameActivity.EXTRA_ARGS, new String[] {
            "-gxlang", L10n.lang(this),
            "-gxscale", Integer.toString(scale),
        });
        startActivity(intent);
    }

    /**
     * Copies assets/gamedata (fonts, dxvk.conf, DefaultOptions.ini) into GameData.
     * Re-run after every app update so config fixes reach existing installs.
     */
    private void installBundledGameData() {
        long versionCode;
        try {
            versionCode = getPackageManager().getPackageInfo(getPackageName(), 0).getLongVersionCode();
        } catch (PackageManager.NameNotFoundException e) {
            versionCode = -1;
        }
        File marker = new File(gameDataDir(), ".zhmobile-assets");
        if (marker.isFile() && Long.toString(versionCode).equals(readSmall(marker))) {
            return;
        }
        try {
            copyAssetTree(getAssets(), "gamedata", gameDataDir());
            writeSmall(marker, Long.toString(versionCode));
        } catch (IOException e) {
            Toast.makeText(this, "assets: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private static void copyAssetTree(AssetManager am, String path, File dest) throws IOException {
        String[] list = am.list(path);
        if (list == null || list.length == 0) {
            dest.getParentFile().mkdirs();
            try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(dest)) {
                pipe(in, out);
            }
            return;
        }
        dest.mkdirs();
        for (String child : list) {
            copyAssetTree(am, path + "/" + child, new File(dest, child));
        }
    }

    private static void pipe(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[64 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
        }
    }

    private static String readSmall(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[64];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n).trim() : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeSmall(File f, String s) throws IOException {
        f.getParentFile().mkdirs();
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(s.getBytes());
        }
    }

    // ------------------------------------------------------------------ import

    private void askImport() {
        if (s_runningImport != null) {
            Toast.makeText(this, t("import_busy"), Toast.LENGTH_SHORT).show();
            return;
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle(t("pick_title"))
            .setMessage(t("pick_hint"))
            .setNegativeButton(t("cancel"), null)
            .setPositiveButton(t("continue"), (d, w) -> {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivityForResult(i, REQ_PICK_FOLDER);
            })
            .show();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_FOLDER && resultCode == RESULT_OK && data != null && data.getData() != null) {
            startImport(data.getData());
        }
    }

    private void setImporting(boolean on) {
        progressBar.setVisibility(on ? View.VISIBLE : View.GONE);
        progressText.setVisibility(on ? View.VISIBLE : View.GONE);
        importButton.setEnabled(!on);
        importButton.setAlpha(on ? 0.4f : 1f);
        if (on) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            progressText.setText(t("importing"));
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
        refreshStatus();
    }

    private void startImport(Uri tree) {
        final GameImporter importer = new GameImporter(getContentResolver(), tree);
        s_runningImport = importer;
        setImporting(true);
        final File dest = gameDataDir();
        new Thread(() -> {
            String message;
            try {
                importer.run(dest, (done, total, file) -> ui.post(() -> {
                    LauncherActivity a = s_current;
                    if (a == null) {
                        return;
                    }
                    s_progress = total > 0 ? (int) (done * 1000 / total) : 0;
                    s_progressLine = String.format(Locale.ROOT, "%s  %s / %s  ·  %s", a.t("importing"),
                        GameImporter.formatGb(done), GameImporter.formatGb(total), file);
                    a.progressBar.setProgress(s_progress);
                    a.progressText.setText(s_progressLine);
                }));
                message = t("import_done");
            } catch (GameImporter.ImportError e) {
                message = "cancel".equals(e.key) ? null
                    : (e.key.equals("import_space") ? t("import_space") + e.detail : t(e.key));
            } catch (Exception e) {
                message = t("import_failed") + e.getMessage();
            }
            final String result = message;
            ui.post(() -> {
                s_runningImport = null;
                s_progress = 0;
                s_progressLine = "";
                LauncherActivity a = s_current;
                if (a != null) {
                    a.setImporting(false);
                    if (result != null) {
                        a.showText("ZH Mobile", result);
                    }
                }
            });
        }, "zh-import").start();
    }

    // ------------------------------------------------------------------ logs

    private void saveLog() {
        File log = new File(logsDir(), "zh-stderr.log");
        File prev = new File(logsDir(), "zh-stderr-prev.log");
        if (!log.isFile() && !prev.isFile()) {
            Toast.makeText(this, t("log_missing"), Toast.LENGTH_LONG).show();
            return;
        }
        String name = "zh-mobile-log-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date()) + ".txt";
        ContentValues cv = new ContentValues();
        cv.put(MediaStore.Downloads.DISPLAY_NAME, name);
        cv.put(MediaStore.Downloads.MIME_TYPE, "text/plain");
        Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
        if (uri == null) {
            Toast.makeText(this, "MediaStore error", Toast.LENGTH_LONG).show();
            return;
        }
        try (OutputStream out = getContentResolver().openOutputStream(uri)) {
            String header = "ZH Mobile" + versionLine().trim() + " | " + android.os.Build.MANUFACTURER + " "
                + android.os.Build.MODEL + " | Android " + android.os.Build.VERSION.RELEASE + "\n";
            out.write(header.getBytes());
            for (File f : new File[] { prev, log }) {
                if (f.isFile()) {
                    out.write(("\n===== " + f.getName() + " =====\n").getBytes());
                    try (InputStream in = new FileInputStream(f)) {
                        pipe(in, out);
                    }
                }
            }
            Toast.makeText(this, t("log_saved") + name, Toast.LENGTH_LONG).show();
        } catch (IOException e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onDestroy() {
        // A running import keeps going; the next launcher instance re-attaches to it.
        if (s_current == this) {
            s_current = null;
        }
        super.onDestroy();
    }
}
