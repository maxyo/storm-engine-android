package org.stormengine.port;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.zip.*;
import java.io.*;
/** Host JVM check of the JNI bridge: UTF-16 paths, results, staged progress and cancellation. */
public final class NativeArchives {
 static {System.loadLibrary("storm_archive");}
 interface Progress {boolean onProgress(int stage,String path,long bytes,long total,long files);}
 static native String[] inspect(String input);
 static native String[] manifest(String input);
 static native String[] discover(String source,String scratch,String password,long[] limits,Progress progress);
 static native String[] extractRoot(String source,String scratch,String password,String id,String staging,long[] limits,Progress progress);
 static final long[] LIMITS={64L<<30,4L<<30,400000,3600,32L<<30,3,100,1<<20};
 static void require(boolean ok,String what){if(!ok)throw new AssertionError(what);}
 static String field(String json,String name){int at=json.indexOf("\""+name+"\":\"")+name.length()+4;return json.substring(at,json.indexOf('"',at));}
 public static void main(String[] args)throws Exception {
  Path root=Files.createTempDirectory("storm-jni-🚢-");
  try {
   Path source=root.resolve("источник-🚢"),scratch=root.resolve("scratch"),out=root.resolve("output-🚢");
   Files.createDirectory(source);Files.createDirectory(scratch);Files.createDirectory(out);
   Path input=source.resolve("игра-🚢.zip");
   String name="RESOURCE/Корабль-🚢.txt";
   try(ZipOutputStream zip=new ZipOutputStream(Files.newOutputStream(input))){
    for(String file:new String[]{name,"PROGRAM/game.c","engine.ini"}){
     zip.putNextEntry(new ZipEntry("Корсары/"+file));zip.write(file.getBytes(StandardCharsets.UTF_8));zip.closeEntry();
    }
   }
   String[] info=inspect(input.toString());require(info[0].equals("0"),"inspect: "+info[2]);
   info=manifest(input.toString());require(info[0].equals("0"),"manifest: "+info[2]);
   require(info[3].contains(name),"Manifest UTF-16 round trip");
   info=discover(source.toString(),scratch.toString(),null,new long[]{1,2,3},null);
   require(info[0].equals("6")&&info.length==4,"Invalid limits are explicit: "+info[0]);
   final boolean[] reading={false};
   info=discover(source.toString(),scratch.toString(),null,LIMITS,(stage,path,bytes,total,files)->{reading[0]|=stage==1;return false;});
   require(info[0].equals("0"),"discover: "+info[2]);require(reading[0],"Reading stage reported");
   require(info[3].contains("\"root\":\"Корсары/\"")&&info[3].contains("игра-🚢.zip"),"Discovery UTF-16 round trip: "+info[3]);
   String id=field(info[3],"id");
   info=extractRoot(source.toString(),scratch.toString(),null,id,out.toString(),LIMITS,(stage,path,bytes,total,files)->files>=1);
   require(info[0].equals("7"),"Callback true cancels: "+info[0]);
   try(var files=Files.walk(out)){files.sorted(java.util.Comparator.reverseOrder()).filter(p->!p.equals(out)).forEach(p->p.toFile().delete());}
   final boolean[] observed={false};
   info=extractRoot(source.toString(),scratch.toString(),null,id,out.toString(),LIMITS,(stage,path,bytes,total,files)->{if(stage==3&&path.equals(name))observed[0]=true;return false;});
   require(info[0].equals("0"),"extractRoot: "+info[2]);require(observed[0],"Callback Unicode");
   require(Files.readString(out.resolve(name)).equals(name),"Output Unicode path and bytes");
   System.out.println("JNI Unicode paths, manifest, discovery, staged progress, cancellation and root extraction passed");
  } finally {
   try(var files=Files.walk(root)){files.sorted(java.util.Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new RuntimeException(e);}});}
  }
 }
}
