package com.nanxin.hdq.libgit2;

import android.content.Context;
import android.content.res.AssetManager;
import android.support.annotation.Keep;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Created by hdq on 16/10/26.
 */

public class Libgit2Utils {

    public Libgit2Utils(String zipFile,String folderPath,Context context){
        if (zipFile.length() < ".zip".length()){
            Log.e("Libgit2Utils","压缩文件名错误");
            return;
        }
        String filePath = folderPath+zipFile.substring(0,zipFile.length()-".zip".length())+"/.git";
        File f = new File(filePath);
        if (!f.exists()) {
            try {
                upZipFile(zipFile,folderPath,context);
            }catch (Exception e){
                Log.e("Libgit2Utils","解压文件失败");
            }
        }

        initLibgit2();

    }

    public void RecursionDeleteFile(File file){
        if(file.isFile()){
            file.delete();
            return;
        }
        if(file.isDirectory()){
            File[] childFile = file.listFiles();
            if(childFile == null || childFile.length == 0){
                file.delete();
                return;
            }
            for(File f : childFile){
                RecursionDeleteFile(f);
            }
            file.delete();
        }
    }

    public void updateGitRepo(String path){
        updateRepo(path,"origin",null,null);
    }

    private boolean copyAssetsToFilesystem(String assetsSrc, String des, Context context){
        Log.i("Libgit2Utils", "Copy "+assetsSrc+" to "+des);
        InputStream istream = null;
        OutputStream ostream = null;
        try{
            AssetManager am = context.getAssets();
            istream = am.open(assetsSrc);
            ostream = new FileOutputStream(des);
            byte[] buffer = new byte[1024];
            int length;
            while ((length = istream.read(buffer))>0){
                ostream.write(buffer, 0, length);
            }
            istream.close();
            ostream.close();
        }
        catch(Exception e){
            e.printStackTrace();
            try{
                if(istream!=null)
                    istream.close();
                if(ostream!=null)
                    ostream.close();
            }
            catch(Exception ee){
                ee.printStackTrace();
            }
            return false;
        }
        return true;
    }


    private void upZipFile(String zipFile, String folderPath,Context context){
        try {
            String dest = folderPath;
            if(!dest.endsWith("/")){
                dest = dest + "/";
            }
            dest = dest+zipFile;
            File dir = new File(folderPath);
            if(!dir.exists() && !dir.mkdirs()){
                Log.i("Libgit2Utils", "Create \""+folderPath+"\" fail!");
                return;
            }
            if (copyAssetsToFilesystem(zipFile,dest,context)){
                File file = new File(dest);
                upZipFile(file,folderPath);
            }
        }catch (Exception e){
            Log.e("Libgit2Utils","解压H5文件失败");
        }
    }

    /**
     * 解压缩功能.
     * 将zipFile文件解压到folderPath目录下.
     * @throws IOException
     */
    private int upZipFile(File zipFile, String folderPath)throws IOException {
        ZipFile zfile=new ZipFile(zipFile);
        Enumeration zList=zfile.entries();
        ZipEntry ze;
        byte[] buf=new byte[1024];
        while(zList.hasMoreElements()){
            ze=(ZipEntry)zList.nextElement();
            if(ze.isDirectory()){
                Log.d("upZipFile", "ze.getName() = "+ze.getName());
                String dirstr = folderPath + ze.getName();
                //dirstr.trim();
                dirstr = new String(dirstr.getBytes("8859_1"), "GB2312");
                Log.d("upZipFile", "str = "+dirstr);
                File f=new File(dirstr);
                f.mkdir();
                continue;
            }
            Log.d("upZipFile", "ze.getName() = "+ze.getName());
            OutputStream os=new BufferedOutputStream(new FileOutputStream(getRealFileName(folderPath, ze.getName())));
            InputStream is=new BufferedInputStream(zfile.getInputStream(ze));
            int readLen;
            while ((readLen=is.read(buf, 0, 1024))!=-1) {
                os.write(buf, 0, readLen);
            }
            is.close();
            os.close();
        }
        zfile.close();
        Log.d("upZipFile", "finishssssssssssssssssssss");
        return 0;
    }

    /**
     * 给定根目录，返回一个相对路径所对应的实际文件名.
     * @param baseDir 指定根目录
     * @param absFileName 相对路径名，来自于ZipEntry中的name
     * @return java.io.File 实际的文件
     */
    private static File getRealFileName(String baseDir, String absFileName){
        String[] dirs=absFileName.split("/");
        File ret=new File(baseDir);
        String substr;
        if(dirs.length>1){
            for (int i = 0; i < dirs.length-1;i++) {
                substr = dirs[i];
                try {
                    //substr.trim();
                    substr = new String(substr.getBytes("8859_1"), "GB2312");

                } catch (UnsupportedEncodingException e) {
                    // TODO Auto-generated catch block
                    e.printStackTrace();
                }
                ret=new File(ret, substr);

            }
            Log.d("upZipFile", "1ret = "+ret);
            if(!ret.exists())
                ret.mkdirs();
            substr = dirs[dirs.length-1];
            try {
                //substr.trim();
                substr = new String(substr.getBytes("8859_1"), "GB2312");
                Log.d("upZipFile", "substr = "+substr);
            } catch (UnsupportedEncodingException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }

            ret=new File(ret, substr);
            Log.d("upZipFile", "2ret = "+ret);
            return ret;
        }
        return ret;
    }


    @Keep
    private void updateStatus(String msg) {
        Log.e("Libgit2",msg);

    }

    static {
        System.loadLibrary("git2");
    }

    public native void updateRepo(String path,String remoteName,String user,String password);
    public native void initLibgit2();
}
