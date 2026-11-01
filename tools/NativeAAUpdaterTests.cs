// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.IO.Compression;
using System.Text;
using System.Web.Script.Serialization;
internal static class NativeAAUpdaterTests {
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
    static string Feed(string tag,string hash,string filename=null){
        string repo="https://github.com/cjboy31/make-xp-anti-aliasing-great-again";
        filename=filename??"Make-XP-AA-"+tag.Substring(1)+"-Windows-x64.zip";
        return new JavaScriptSerializer().Serialize(new[]{new {draft=false,prerelease=true,tag_name=tag,html_url=repo+"/releases/tag/"+tag,name="AA test",assets=new[]{new {name=filename,state="uploaded",digest=hash,size=2048,browser_download_url=repo+"/releases/download/"+tag+"/"+filename}}}});
    }
    static void Rejected(Action f,string why){bool refused=false;try{f();}catch{refused=true;}Check(refused,why);}
    static void Zip(string file,string path,bool linked=false){using(var z=ZipFile.Open(file,ZipArchiveMode.Create)){var e=z.CreateEntry(path);if(linked)e.ExternalAttributes=unchecked((int)0xa0000000);using(var s=e.Open()){var b=Encoding.UTF8.GetBytes("fixture");s.Write(b,0,b.Length);}}}
    public static int Main(string[] args){
        try{Check(args.Length==1,"fresh fixture path required");string root=Path.GetFullPath(args[0]);Check(!Directory.Exists(root),"fixture already exists");Directory.CreateDirectory(root);
            var hash="sha256:"+new string('a',64);var good=NativeAAUpdater.Select(Feed("v0.3.1-alpha",hash));Check(good!=null&&good.Tag=="v0.3.1-alpha"&&good.Bytes==2048,"valid release selection");
            Check(NativeAAUpdater.Order("0.4.0-alpha").CompareTo(NativeAAUpdater.Order("v0.3.1-alpha"))>0,"newer installed version would be downgraded");
            Check(NativeAAUpdater.Order("0.3.1-alpha")==NativeAAUpdater.Order("v0.3.1-alpha"),"equal versions disagree");
            Check(NativeAAUpdater.Select(Feed("v0.3.1-linux-beta.1",hash))==null,"Linux package selected on Windows");
            // v2 channels: beta.windows.N is Betas-only; Release Candidates never see it.
            Check(NativeAAUpdater.Select(Feed("v2.0.0-beta.windows.1",hash))==null,"Beta offered to Release Candidates");
            var beta=NativeAAUpdater.Select(Feed("v2.0.0-beta.windows.1",hash),true);Check(beta!=null&&beta.Tag=="v2.0.0-beta.windows.1","Beta not offered to Betas");
            Check(NativeAAUpdater.Select(Feed("v0.3.1-alpha",hash),true)!=null,"Betas lost ordinary releases");
            Check(NativeAAUpdater.Order("2.0.0-beta.windows.2").CompareTo(NativeAAUpdater.Order("v2.0.0-beta.windows.1"))>0&&
                  NativeAAUpdater.Order("2.0.0").CompareTo(NativeAAUpdater.Order("2.0.0-beta.windows.9"))>0,"Namespaced beta ordering");
            Check(NativeAAUpdater.Select(Feed("v2.0.0-rc.windows.1",hash),true)==null,"Namespaced non-beta accepted");
            string ini=Path.Combine(root,"settings.ini");File.WriteAllText(ini,"[Launcher]\nChannel=beta\n[Updates]\nChannel=beta\n");Check(NativeAAUpdater.BetasSelected(ini),"Betas channel not read");
            File.WriteAllText(ini,"[Updates]\nChannel=release\n");Check(!NativeAAUpdater.BetasSelected(ini),"Release channel read as Betas");
            File.WriteAllText(ini,"[Updates]\nChannel=experimental\n");Check(NativeAAUpdater.BetasSelected(ini),"Legacy Experimental not mapped to Betas");
            Check(!NativeAAUpdater.BetasSelected(Path.Combine(root,"missing.ini")),"Missing settings must default to Release Candidates");
            Check(NativeAAUpdater.Select(Feed("v0.3.1-alpha",hash,"other.zip"))==null,"unexpected asset selected");
            Rejected(()=>NativeAAUpdater.Select(Feed("v0.3.1-alpha","sha256:bad")),"invalid digest accepted");
            foreach(var name in new[]{"../escape.exe","/absolute.exe","C:/escape.exe","a\\b.exe","file:stream"})Rejected(()=>NativeAAUpdater.SafePath(root,name),"unsafe archive path accepted");
            string traversal=Path.Combine(root,"traversal.zip");Zip(traversal,"../escape.exe");Rejected(()=>NativeAAUpdater.Extract(traversal,Path.Combine(root,"bad")),"archive traversal accepted");Check(!File.Exists(Path.Combine(root,"escape.exe")),"archive wrote outside its folder");
            string link=Path.Combine(root,"link.zip");Zip(link,"link",true);Rejected(()=>NativeAAUpdater.Extract(link,Path.Combine(root,"links")),"archive symlink accepted");
            string valid=Path.Combine(root,"valid.zip");using(var z=ZipFile.Open(valid,ZipArchiveMode.Create)){z.CreateEntry("package/NativeAA-Setup.exe");z.CreateEntry("package/RELEASE-MANIFEST.json");}
            Check(NativeAAUpdater.Extract(valid,Path.Combine(root,"valid")).EndsWith("NativeAA-Setup.exe"),"valid package extraction failed");
            Console.WriteLine("PASS: Windows-only feed, Release Candidates/Betas channels, exact assets/digests, bounded paths, archive traversal/link refusal and fresh package extraction; no network or installer execution.");return 0;
        }catch(Exception e){Console.Error.WriteLine("FAIL: "+e.Message);return 1;}
    }
}
