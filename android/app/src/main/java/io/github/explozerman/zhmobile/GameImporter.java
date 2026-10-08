package io.github.explozerman.zhmobile;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.StatFs;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Copies the player's own copy of Generals + Zero Hour from a folder they pick
 * (Storage Access Framework tree) into the app's GameData directory:
 *
 *   Zero Hour folder        -> GameData/
 *   base Generals folder    -> GameData/ZH_Generals/
 *   00RussianZH.big etc.    -> GameData/lang/ru/   (switchable language pack)
 *
 * Works with the retail layout (two sibling folders) and the Steam layout
 * (Zero Hour folder with a ZH_Generals subfolder). Windows-only files are skipped,
 * and files that are already present with the same size are not copied again, so an
 * interrupted import can simply be restarted.
 */
public final class GameImporter {

    public interface Listener {
        void onProgress(long doneBytes, long totalBytes, String currentFile);
    }

    /** Thrown with a launcher string key (see L10n) for user-facing failures. */
    public static final class ImportError extends Exception {
        public final String key;
        public final String detail;
        ImportError(String key, String detail) {
            super(key + (detail != null ? ": " + detail : ""));
            this.key = key;
            this.detail = detail;
        }
    }

    private static final class Doc {
        final String id;
        final String name;
        final boolean dir;
        final long size;
        Doc(String id, String name, boolean dir, long size) {
            this.id = id;
            this.name = name;
            this.dir = dir;
            this.size = size;
        }
    }

    private static final class CopyItem {
        final Doc doc;
        final File dest;
        CopyItem(Doc doc, File dest) {
            this.doc = doc;
            this.dest = dest;
        }
    }

    private final ContentResolver resolver;
    private final Uri tree;
    private volatile boolean cancelled;

    public GameImporter(ContentResolver resolver, Uri tree) {
        this.resolver = resolver;
        this.tree = tree;
    }

    public void cancel() {
        cancelled = true;
    }

    // ------------------------------------------------------------------ queries

    private List<Doc> children(String parentId) {
        List<Doc> out = new ArrayList<>();
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] cols = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
        };
        try (Cursor c = resolver.query(uri, cols, null, null, null)) {
            if (c == null) {
                return out;
            }
            while (c.moveToNext()) {
                String mime = c.getString(2);
                boolean isDir = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
                long size = c.isNull(3) ? 0 : c.getLong(3);
                out.add(new Doc(c.getString(0), c.getString(1), isDir, size));
            }
        }
        return out;
    }

    private static Doc find(List<Doc> docs, String name) {
        for (Doc d : docs) {
            if (d.name != null && d.name.equalsIgnoreCase(name)) {
                return d;
            }
        }
        return null;
    }

    private static boolean isZeroHourDir(List<Doc> kids) {
        return find(kids, "INIZH.big") != null;
    }

    private static boolean isBaseGeneralsDir(List<Doc> kids) {
        return find(kids, "INI.big") != null && find(kids, "W3D.big") != null && find(kids, "INIZH.big") == null;
    }

    private Doc zhDir;
    private Doc baseDir;

    private void locate(Doc dir, int depth) {
        if (depth > 3 || (zhDir != null && baseDir != null) || cancelled) {
            return;
        }
        List<Doc> kids = children(dir.id);
        if (zhDir == null && isZeroHourDir(kids)) {
            zhDir = dir;
        } else if (baseDir == null && isBaseGeneralsDir(kids)) {
            baseDir = dir;
        }
        for (Doc k : kids) {
            if (k.dir) {
                locate(k, depth + 1);
            }
        }
    }

    // ------------------------------------------------------------------ filters

    private static final String[] SKIP_EXT = {
        ".exe", ".dll", ".sys", ".ico", ".bmp", ".doc", ".rtf", ".pdf", ".lcf", ".txt", ".url", ".lnk",
    };
    private static final String[] SKIP_DIRS = {
        "mss", "manuals", "support", "steamapps", "_commonredist", "redistinstallers", "userdata",
        "__installer", "zh_generals",
    };

    private static boolean skipFile(String name, boolean atRoot) {
        String n = name.toLowerCase(Locale.ROOT);
        for (String e : SKIP_EXT) {
            if (n.endsWith(e)) {
                return true;
            }
        }
        // Root-level copy-protection / launcher data files
        return atRoot && (n.endsWith(".dat") || n.startsWith("00000000."));
    }

    private static boolean skipDir(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        for (String d : SKIP_DIRS) {
            if (n.equals(d)) {
                return true;
            }
        }
        return false;
    }

    /** Language pack archives such as 00RussianZH.big (loaded only when chosen). */
    static String languagePackCode(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        if (!n.endsWith(".big") || !(n.startsWith("00") || n.startsWith("0!"))) {
            return null;
        }
        if (n.contains("russian")) {
            return "ru";
        }
        return null;
    }

    private void collect(Doc dir, File dest, boolean root, boolean zeroHour, File gameData, List<CopyItem> out) {
        for (Doc d : children(dir.id)) {
            if (cancelled) {
                return;
            }
            if (d.dir) {
                if (!skipDir(d.name)) {
                    collect(d, new File(dest, d.name), false, zeroHour, gameData, out);
                }
                continue;
            }
            if (skipFile(d.name, root)) {
                continue;
            }
            String lang = root ? languagePackCode(d.name) : null;
            if (lang != null) {
                // Only Zero Hour's pack is used: ZH ships its own copy of every
                // translated string/menu, and the base game's pack would override
                // Zero Hour's English UI textures with base-game ones.
                if (zeroHour) {
                    out.add(new CopyItem(d, new File(new File(gameData, "lang/" + lang), d.name)));
                }
                continue;
            }
            out.add(new CopyItem(d, new File(dest, d.name)));
        }
    }

    // ------------------------------------------------------------------ import

    public void run(File gameData, Listener listener) throws ImportError, IOException {
        Doc root = new Doc(DocumentsContract.getTreeDocumentId(tree), "", true, 0);
        locate(root, 0);
        if (cancelled) {
            throw new ImportError("cancel", null);
        }
        if (zhDir == null) {
            throw new ImportError("import_no_zh", null);
        }
        if (baseDir == null) {
            throw new ImportError("import_no_base", null);
        }

        List<CopyItem> items = new ArrayList<>();
        collect(zhDir, gameData, true, true, gameData, items);
        collect(baseDir, new File(gameData, "ZH_Generals"), true, false, gameData, items);

        long total = 0;
        long needed = 0;
        for (CopyItem it : items) {
            total += it.doc.size;
            if (!(it.dest.exists() && it.dest.length() == it.doc.size)) {
                needed += it.doc.size;
            }
        }
        gameData.mkdirs();
        long free = new StatFs(gameData.getPath()).getAvailableBytes();
        if (needed > free) {
            throw new ImportError("import_space", formatGb(needed) + " / " + formatGb(free));
        }

        long done = 0;
        byte[] buf = new byte[1 << 20];
        for (CopyItem it : items) {
            if (cancelled) {
                throw new ImportError("cancel", null);
            }
            if (it.dest.exists() && it.dest.length() == it.doc.size) {
                done += it.doc.size;
                listener.onProgress(done, total, it.doc.name);
                continue;
            }
            File parent = it.dest.getParentFile();
            if (parent != null) {
                parent.mkdirs();
            }
            File tmp = new File(it.dest.getPath() + ".part");
            Uri src = DocumentsContract.buildDocumentUriUsingTree(tree, it.doc.id);
            try (InputStream in = resolver.openInputStream(src);
                 OutputStream out = new FileOutputStream(tmp)) {
                if (in == null) {
                    throw new IOException("cannot open " + it.doc.name);
                }
                int n;
                long lastReport = 0;
                while ((n = in.read(buf)) > 0) {
                    if (cancelled) {
                        throw new ImportError("cancel", null);
                    }
                    out.write(buf, 0, n);
                    done += n;
                    if (done - lastReport > (4 << 20)) {
                        lastReport = done;
                        listener.onProgress(done, total, it.doc.name);
                    }
                }
            }
            if (!tmp.renameTo(it.dest)) {
                throw new IOException("cannot write " + it.dest);
            }
            listener.onProgress(done, total, it.doc.name);
        }
    }

    static String formatGb(long bytes) {
        return String.format(Locale.ROOT, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}
