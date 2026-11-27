package uk.kevinkaye.keystone;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Hands a Keystone report to whichever app it's shared with (Drive, Gmail, WhatsApp), read only.
 * Serves the zips in the app's cache/reports folder and nothing else.
 */
public final class ReportProvider extends ContentProvider {
    static File dir(Context c) {
        File d = new File(c.getCacheDir(), "reports");
        //noinspection ResultOfMethodCallIgnored
        d.mkdirs();
        return d;
    }

    static Uri uriFor(Context c, File f) {
        return Uri.parse("content://" + c.getPackageName() + ".reports/" + Uri.encode(f.getName()));
    }

    private File fileFor(Uri u) {
        String n = u.getLastPathSegment();
        if (n == null || n.contains("/") || n.contains("..") || !n.startsWith("keystone-report-") || !n.endsWith(".zip")) return null;
        File f = new File(dir(getContext()), n);
        return f.isFile() ? f : null;
    }

    @Override public boolean onCreate() { return true; }

    @Override public String getType(Uri uri) { return "application/zip"; }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new SecurityException("Reports are read only");
        File f = fileFor(uri);
        if (f == null) throw new FileNotFoundException("No such report");
        return ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
        File f = fileFor(uri);
        if (f == null) return null;
        String[] cols = projection != null ? projection : new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE };
        MatrixCursor c = new MatrixCursor(cols, 1);
        Object[] row = new Object[cols.length];
        for (int i = 0; i < cols.length; i++) {
            if (OpenableColumns.DISPLAY_NAME.equals(cols[i])) row[i] = f.getName();
            else if (OpenableColumns.SIZE.equals(cols[i])) row[i] = f.length();
        }
        c.addRow(row);
        return c;
    }

    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] args) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) { return 0; }
}
