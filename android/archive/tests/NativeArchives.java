package org.stormengine.port;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.zip.*;
import java.io.*;
public final class NativeArchives {
 static {System.loadLibrary("storm_archive");}
 static native String[] inspect(String input);
 static native String[] manifest(String input);
 static native String[] extract(String input,String output,long[] limits,Progress progress);
 interface Progress {boolean onProgress(String path,long bytes,long files);}
 static void require(boolean ok,String what){if(!ok)throw new AssertionError(what);}
 public static void main(String[] args)throws Exception {
  Path root=Files.createTempDirectory("storm-jni-🚢-");
  try {
   Path input=root.resolve("игра-🚢.zip"),out=root.resolve("output-🚢");Files.createDirectory(out);
   String name="RESOURCE/Корабль-🚢.txt";
   try(ZipOutputStream zip=new ZipOutputStream(Files.newOutputStream(input))){
    for(String file:new String[]{name,"PROGRAM/game.c","engine.ini"}){
     zip.putNextEntry(new ZipEntry("app/"+file));zip.write(file.getBytes(StandardCharsets.UTF_8));zip.closeEntry();
    }
   }
   String[] info=inspect(input.toString());require(info[0].equals("0"),"inspect: "+info[2]);
   info=manifest(input.toString());require(info[0].equals("0"),"manifest: "+info[2]);
   require(info[3].contains(name),"Manifest UTF-16 round trip");
   final boolean[] observed={false};
   info=extract(input.toString(),out.toString(),null,(path,bytes,files)->{if(path.equals(name))observed[0]=true;return false;});
   require(info[0].equals("0"),"extract: "+info[2]);require(observed[0],"Callback Unicode");
   require(Files.readString(out.resolve(name)).equals(name),"Output Unicode path and bytes");
   System.out.println("JNI Unicode paths, manifest, progress and extraction passed");
  } finally {
   try(var files=Files.walk(root)){files.sorted(java.util.Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new RuntimeException(e);}});}
  }
 }
}
