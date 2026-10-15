package org.stormengine.port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Imports only user-selected local resources. No downloads or bundled game data. */
public final class GameLauncherActivity extends Activity {
    private static final int IMPORT = 1;
    private TextView status;
    private Button importButton, startButton;
    private File game;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        File root = getExternalFilesDir(null);
        if (root == null) root = getFilesDir();
        game = new File(root, "game");
        game.mkdirs();
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 24, 24, 24);
        status = new TextView(this);
        status.setTextSize(18);
        status.setText("Storm Android — experimental engine build\n\nImport your legally obtained TEHO game files as a ZIP with engine.ini at the archive root. No game data is included.\n\nGame files: " + game + "\nSaves/logs: " + new File(getFilesDir(), "storm") + "\n\nBack up saves before uninstalling. Device compatibility and gameplay are not yet verified.");
        layout.addView(status);
        importButton = new Button(this); importButton.setText("Import game ZIP");
        importButton.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE); intent.setType("application/zip");
            startActivityForResult(intent, IMPORT);
        });
        layout.addView(importButton);
        startButton = new Button(this); startButton.setText("Start engine");
        startButton.setOnClickListener(v -> {
            if (!new File(game,"engine.ini").isFile()) { status.setText("engine.ini is missing from " + game + ". Import the complete game files first."); return; }
            startActivity(new Intent(this, GameActivity.class));
        });
        layout.addView(startButton); setContentView(layout);
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request!=IMPORT || result!=RESULT_OK || data==null || data.getData()==null)return;
        importButton.setEnabled(false);startButton.setEnabled(false);status.setText("Importing selected archive… Keep this app open.");
        final android.net.Uri uri=data.getData();
        new Thread(() -> {
            String message;
            try(InputStream input=getContentResolver().openInputStream(uri);ZipInputStream zip=new ZipInputStream(input)) {
                String root=game.getCanonicalPath()+File.separator;
                byte[] buffer=new byte[65536];long total=0;int files=0;
                for(ZipEntry entry;(entry=zip.getNextEntry())!=null;) {
                    File output=new File(game,entry.getName().replace('\\','/'));
                    if(entry.isDirectory() && output.getCanonicalPath().equals(game.getCanonicalPath())) continue;
                    if(!output.getCanonicalPath().startsWith(root))throw new java.io.IOException("Archive contains an unsafe path");
                    if(entry.isDirectory()) {if(!output.isDirectory()&&!output.mkdirs())throw new java.io.IOException("Cannot create directory");continue;}
                    if(++files>200000)throw new java.io.IOException("Too many archive entries");
                    File parent=output.getParentFile();if(!parent.isDirectory()&&!parent.mkdirs())throw new java.io.IOException("Cannot create directory");
                    // Replace one file only after it has been fully extracted.
                    File temp=File.createTempFile("storm-import-", ".tmp",parent);
                    try {
                        try(FileOutputStream out=new FileOutputStream(temp)) {
                            int n;while((n=zip.read(buffer))!=-1) {
                                total+=n;if(total>32L*1024*1024*1024)throw new java.io.IOException("Archive exceeds 32 GiB import limit");
                                out.write(buffer,0,n);
                            }
                        }
                        if(!temp.renameTo(output))throw new java.io.IOException("Cannot replace " + output.getName());
                    } finally {temp.delete();}
                }
                message=new File(game,"engine.ini").isFile() ? "Import finished. Start engine to test. engine.ini alone does not verify that the game installation is complete." : "Import finished, but engine.ini is missing at the archive root. Check the folder layout.";
            } catch(Exception e) {message="Import failed: "+e.getMessage()+". Previously imported files remain; retry with a complete archive.";}
            final String report=message;
            runOnUiThread(() -> {status.setText(report);importButton.setEnabled(true);startButton.setEnabled(true);});
        },"storm-resource-import").start();
    }
}
