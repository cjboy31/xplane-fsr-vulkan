// SPDX-License-Identifier: MIT
// Windows update client for the independent Native AA plugin. No image controls.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
internal static class NativeAAUpdater {
    const string Repo="https://github.com/cjboy31/make-xp-anti-aliasing-great-again";
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer {MaxJsonLength=8000000};
    internal sealed class Release {internal string Tag,URL,Hash;internal long Bytes;}
    internal static Version Order(string tag){
        var m=Regex.Match(tag,@"^v?(\d{1,6}\.\d{1,6}\.\d{1,6})(?:-(alpha|beta|rc)(?:\.(\d{1,6}))?)?$");
        Require(m.Success,"Unrecognized installed version. Use the Native AA setup package manually.");
        var v=new Version(m.Groups[1].Value);int stage=m.Groups[2].Value=="alpha"?0:m.Groups[2].Value=="beta"?1:m.Groups[2].Value=="rc"?2:3;
        return new Version(v.Major,v.Minor,v.Build,stage*1000000+(m.Groups[3].Success?Int32.Parse(m.Groups[3].Value):0));
    }
    static void Require(bool ok,string message){if(!ok)throw new InvalidOperationException(message);}
    static string Hash(string file){using(var h=SHA256.Create())using(var f=File.OpenRead(file))return BitConverter.ToString(h.ComputeHash(f)).Replace("-","").ToLowerInvariant();}
    internal static string SafePath(string root,string name){
        Require(!String.IsNullOrWhiteSpace(name)&&!Path.IsPathRooted(name)&&!name.Contains(":")&&!name.Contains("\\")&&!name.Split('/').Contains(".."),"An update contains an unsafe path.");
        var full=Path.GetFullPath(Path.Combine(root,name));Require(full.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase),"An update path left its download folder.");return full;
    }
    static byte[] Fetch(string address,string destination,long expected){
        bool binary=destination!=null;ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;
        for(int redirects=0;redirects<6;++redirects){
            Uri u=new Uri(address);Require(u.Scheme=="https"&&u.Port==443&&u.UserInfo.Length==0&&u.Fragment.Length==0&&
                (binary?(u.Host=="github.com"||u.Host=="release-assets.githubusercontent.com"||u.Host=="objects.githubusercontent.com"):u.Host=="api.github.com"),"Update left the allowed GitHub HTTPS servers.");
            var request=(HttpWebRequest)WebRequest.Create(u);request.AllowAutoRedirect=false;request.Timeout=15000;request.ReadWriteTimeout=15000;request.UserAgent="XPlane-NativeAA-Updater";
            using(var response=(HttpWebResponse)request.GetResponse()){
                int status=(int)response.StatusCode;
                if(status==301||status==302||status==303||status==307||status==308){Require(binary,"Unexpected release-feed redirect.");address=new Uri(u,response.Headers["Location"]).AbsoluteUri;continue;}
                Require(status==200,"GitHub could not provide the update.");
                using(var input=response.GetResponseStream())using(Stream output=binary?(Stream)File.Create(destination):new MemoryStream()){
                    var buffer=new byte[65536];long count=0;int got;var watch=Stopwatch.StartNew();
                    while((got=input.Read(buffer,0,buffer.Length))>0){count+=got;Require(count<=(binary?expected:8000000)&&watch.Elapsed.TotalMinutes<10,"Update was too large or timed out.");output.Write(buffer,0,got);}
                    Require(!binary||count==expected,"Download was incomplete.");return binary?null:((MemoryStream)output).ToArray();
                }
            }
        }throw new InvalidOperationException("Too many update redirects.");
    }
    internal static Release Select(string feed){
        var releases=(object[])Json.DeserializeObject(feed);Release chosen=null;Version best=null;
        foreach(var entry in releases){var r=(Dictionary<string,object>)entry;if(Convert.ToBoolean(r["draft"]))continue;
            string tag=Convert.ToString(r["tag_name"]);var match=Regex.Match(tag,@"^v(\d{1,6}\.\d{1,6}\.\d{1,6})(?:-(alpha|beta|rc)(?:\.(\d{1,6}))?)?$");if(!match.Success)continue;
            var version=new Version(match.Groups[1].Value);int stage=match.Groups[2].Value=="alpha"?0:match.Groups[2].Value=="beta"?1:match.Groups[2].Value=="rc"?2:3;
            int rev=match.Groups[3].Success?Int32.Parse(match.Groups[3].Value):0;
            var order=new Version(version.Major,version.Minor,version.Build,stage*1000000+rev);
            if(best!=null&&order.CompareTo(best)<=0)continue;
            if(Convert.ToBoolean(r["prerelease"])!=(stage<3))continue;
            Require(Convert.ToString(r["html_url"])==Repo+"/releases/tag/"+tag,"Unexpected release repository.");
            string filename="Make-XP-AA-"+tag.Substring(1)+"-Windows-x64.zip";
            Release candidate=null;
            foreach(var a in (object[])r["assets"]){var asset=(Dictionary<string,object>)a;if(Convert.ToString(asset["name"])!=filename||Convert.ToString(asset["state"])!="uploaded")continue;
                Require(candidate==null,"Duplicate Windows update asset.");string url=Convert.ToString(asset["browser_download_url"]),digest=Convert.ToString(asset["digest"]);long size=Convert.ToInt64(asset["size"]);
                Require(url==Repo+"/releases/download/"+tag+"/"+filename&&Regex.IsMatch(digest,@"^sha256:[a-f0-9]{64}$")&&size>=1024&&size<=268435456,"The update has invalid size, link or verification data.");
                candidate=new Release{Tag=tag,URL=url,Hash=digest.Substring(7),Bytes=size};
            }
            if(candidate!=null){chosen=candidate;best=order;}
        }return chosen;
    }
    internal static string Extract(string zip,string folder){
        Require(!Directory.Exists(folder),"Download extraction folder already exists.");Directory.CreateDirectory(folder);
        using(var archive=ZipFile.OpenRead(zip)){
            Require(archive.Entries.Count>0&&archive.Entries.Count<=1000,"Unexpected number of package files.");
            long bytes=0;var paths=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var e in archive.Entries){string path=SafePath(folder,e.FullName);Require(paths.Add(path),"Duplicate package path.");bytes+=e.Length;
                int kind=(e.ExternalAttributes>>16)&0xf000;Require(kind!=0xa000&&bytes<=536870912&&e.Length<=268435456,"Unsafe link or oversized update package.");}
            foreach(var e in archive.Entries){string path=SafePath(folder,e.FullName);if(e.FullName.EndsWith("/")){Directory.CreateDirectory(path);continue;}Directory.CreateDirectory(Path.GetDirectoryName(path));e.ExtractToFile(path);}
        }
        var setups=Directory.GetFiles(folder,"NativeAA-Setup.exe",SearchOption.AllDirectories);Require(setups.Length==1,"A unique Native AA setup was not found.");
        Require(File.Exists(Path.Combine(Path.GetDirectoryName(setups[0]),"RELEASE-MANIFEST.json")),"Native AA package manifest is missing.");return setups[0];
    }
    [STAThread] static int Main(string[] args){
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        var form=new Form{Text="Native AA Plugin Updates",Width=650,Height=340,StartPosition=FormStartPosition.CenterScreen,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false};
        form.BackColor=System.Drawing.Color.FromArgb(12,16,23);form.ForeColor=System.Drawing.Color.FromArgb(241,238,229);form.Font=new System.Drawing.Font("Segoe UI",10);form.AutoScaleMode=AutoScaleMode.Dpi;
        var title=new Label{Left=22,Top=20,Width=590,Height=50,Text="Update Native AA without changing your AA choices.\nClose X-Plane before installing. A Windows update package is required."};
        var sim=new TextBox{Left=22,Top=82,Width=465};if(args.Length==2&&args[0]=="--simulator")sim.Text=args[1];
        var browse=new Button{Left=500,Top=80,Width=110,Text="Browse..."};var check=new Button{Left=22,Top=128,Width=270,Height=36,Text="Check for updates"};var install=new Button{Left=310,Top=128,Width=300,Height=36,Text="Download and install",Enabled=false};
        var status=new Label{Left=22,Top=184,Width=590,Height=90,Text="Choose your X-Plane folder. Downloads come from the official project GitHub releases and are checked before setup runs."};Release release=null;bool busy=false;
        browse.Click+=delegate{using(var picker=new FolderBrowserDialog{Description="Choose the X-Plane folder",ShowNewFolderButton=false})if(picker.ShowDialog(form)==DialogResult.OK)sim.Text=picker.SelectedPath;};
        check.Click+=async delegate{busy=true;check.Enabled=install.Enabled=browse.Enabled=sim.Enabled=false;status.Text="Checking GitHub...";try{release=await Task.Factory.StartNew(()=>Select(Encoding.UTF8.GetString(Fetch("https://api.github.com/repos/cjboy31/make-xp-anti-aliasing-great-again/releases?per_page=100",null,0))));
            string installed="Not installed";try{var inventory=(Dictionary<string,object>)Json.DeserializeObject(File.ReadAllText(Path.Combine(sim.Text,"Resources/plugins/NativeAA/installation.json")));installed=Convert.ToString(inventory["version"]);}catch{}
            bool newer=release!=null&&(installed=="Not installed"||Order(release.Tag).CompareTo(Order(installed))>0);
            status.Text="Installed: "+installed+"\n"+(release==null?"No Windows update package is available.":newer?"Available: "+release.Tag+". Your saved choices will be kept.":"You already have this version or a newer version. No downgrade is offered.");install.Enabled=newer;
        }catch(Exception e){status.Text=e.Message+" No changes made.";}finally{busy=false;check.Enabled=browse.Enabled=sim.Enabled=true;}};
        install.Click+=async delegate{busy=true;check.Enabled=install.Enabled=browse.Enabled=false;sim.Enabled=false;string stage=null;
            try{Require(Process.GetProcessesByName("X-Plane").Length==0,"Close X-Plane normally, then try again.");string selected=Path.GetFullPath(sim.Text);Require(File.Exists(Path.Combine(selected,"X-Plane.exe")),"Choose the folder containing X-Plane.exe.");var target=release;Require(target!=null,"Check for updates first.");status.Text="Downloading and verifying "+target.Tag+"...";
                stage=Path.Combine(Path.GetTempPath(),"NativeAA-update-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(stage);string zip=Path.Combine(stage,"update.zip");
                string setup=await Task.Factory.StartNew(()=>{Fetch(target.URL,zip,target.Bytes);Require(Hash(zip)==target.Hash,"Downloaded package failed verification. Setup will not run.");return Extract(zip,Path.Combine(stage,"package"));});
                Require(Process.GetProcessesByName("X-Plane").Length==0,"X-Plane started during the download. Close it before installing.");
                status.Text="Installing checked files...";var start=new ProcessStartInfo(setup,"--install \""+selected+"\""){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Path.GetDirectoryName(setup)};
                int result=await Task.Factory.StartNew(()=>{using(var p=Process.Start(start)){p.WaitForExit();return p.ExitCode;}});Require(result==0,"Native AA setup could not finish. Check the selected simulator and close all simulator sessions.");status.Text="Updated to "+target.Tag+". Launch X-Plane normally. Your AA choices are kept.";
            }catch(Exception e){status.Text=e.Message;}finally{busy=false;check.Enabled=browse.Enabled=sim.Enabled=true;if(stage!=null)try{Directory.Delete(stage,true);}catch{}}};
        form.FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy){e.Cancel=true;status.Text+="\nWait for this operation to finish before closing.";}};
        foreach(var b in new[]{browse,check,install}){b.FlatStyle=FlatStyle.Flat;b.BackColor=System.Drawing.Color.FromArgb(28,35,47);b.ForeColor=form.ForeColor;}
        sim.BackColor=System.Drawing.Color.FromArgb(28,35,47);sim.ForeColor=form.ForeColor;
        form.Controls.AddRange(new Control[]{title,sim,browse,check,install,status});Application.Run(form);return 0;
    }
}
