using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

[assembly: AssemblyTitle("ELEGOO Web")]
[assembly: AssemblyDescription("Local printer web interface controller")]
[assembly: AssemblyVersion("4.4.0.0")]
[assembly: AssemblyFileVersion("4.4.0.0")]

public sealed class Config {
    public string printer_ip { get; set; }
    public string username { get; set; }
    public string access_code { get; set; }
    public string serial { get; set; }
    public string language { get; set; }
    public int port { get; set; }
    public Config() { printer_ip=""; username="elegoo"; access_code=""; serial=""; language="it"; port=8888; }
    public bool Complete() {
        IPAddress a;
        return IPAddress.TryParse(printer_ip, out a) && a.AddressFamily == AddressFamily.InterNetwork
            && !String.IsNullOrWhiteSpace(username) && !String.IsNullOrWhiteSpace(access_code)
            && !String.IsNullOrWhiteSpace(serial);
    }
}
public static class ConfigStore {
    public static Config Load(string path) {
        if (!File.Exists(path)) return new Config();
        Config c = new JavaScriptSerializer().Deserialize<Config>(File.ReadAllText(path, Encoding.UTF8));
        if (c == null) throw new InvalidDataException("Configurazione vuota / Empty configuration.");
        c.port=8888; return c;
    }
    public static void Save(string path, Config c) {
        if (!c.Complete()) throw new InvalidDataException("Configurazione incompleta / Incomplete configuration.");
        string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {
            File.WriteAllText(temp,new JavaScriptSerializer().Serialize(c),new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temp,path,null); else File.Move(temp,path);
        } finally { if(File.Exists(temp)) File.Delete(temp); }
    }
}
public sealed class MainWindow : Form {
    readonly string root=AppDomain.CurrentDomain.BaseDirectory;
    string ConfigPath { get { return Path.Combine(root,"config.json"); } }
    string ServerPath { get { return Path.Combine(root,"ElegooWebServer.exe"); } }
    Config config;
    TextBox ip,user,code,serial,mobile;
    ComboBox language;
    Label status,printer,qrHint,mobileLabel,ipLabel,userLabel,codeLabel,serialLabel,languageLabel,footerInfo,footerSecurity;
    Button toggle,save,open,detect,copy;
    CheckBox showCode;
    PictureBox qr;
    readonly Timer timer=new Timer();
    bool probing,busy,closing;
    string lastQr="";
    public MainWindow() {
        Text="ELEGOO Web 4.4.0"; ClientSize=new Size(710,590);
        Font=new Font("Segoe UI",10); BackColor=Color.FromArgb(242,244,247);
        StartPosition=FormStartPosition.CenterScreen; FormBorderStyle=FormBorderStyle.FixedSingle;
        MaximizeBox=false; AutoScaleMode=AutoScaleMode.Font;
        if(File.Exists(Path.Combine(root,"favicon.ico"))) Icon=new Icon(Path.Combine(root,"favicon.ico"));
        bool configWarning=false;
        try { config=ConfigStore.Load(ConfigPath); } catch { config=new Config(); configWarning=true; }
        Panel header=new Panel {Bounds=new Rectangle(0,0,710,85), BackColor=Color.FromArgb(31,35,42)};
        Controls.Add(header);
        header.Controls.Add(new Label {Text="ELEGOO WEB",Bounds=new Rectangle(25,12,470,38),ForeColor=Color.White,Font=new Font("Segoe UI",22,FontStyle.Bold)});
        header.Controls.Add(new Label {Text="Centauri Carbon 2  |  Local Web Control  |  v4.4.0",Bounds=new Rectangle(28,53,650,24),ForeColor=Color.LightGray});
        status=LabelAt("",25,105,480,28);
        printer=LabelAt("",25,141,480,28);
        mobileLabel=LabelAt("",25,185,420,25);
        mobile=Field("",25,211,370); mobile.ReadOnly=true;
        copy=ButtonAt("",405,209,100,32);
        copy.Click+=delegate { try {if(mobile.Text.StartsWith("http://")) Clipboard.SetText(mobile.Text);}catch(Exception e){Error(e.Message);} };
        qr=new PictureBox {Bounds=new Rectangle(545,104,135,135),SizeMode=PictureBoxSizeMode.Zoom,BackColor=Color.White}; Controls.Add(qr);
        qrHint=LabelAt("",535,244,160,25);
        open=ButtonAt("",25,281,210,43);
        toggle=ButtonAt("",250,281,210,43);
        detect=ButtonAt("",475,281,210,43);
        detect.Click+=async delegate {await ActionAsync(async delegate {
            string currentIP=ip.Text.Trim();
            string currentCode=code.Text;
            IPAddress parsed;
            if(!IPAddress.TryParse(currentIP,out parsed)||parsed.AddressFamily!=AddressFamily.InterNetwork)
                throw new InvalidOperationException(L("Inserisci un indirizzo IPv4 valido.","Enter a valid IPv4 address."));
            if(String.IsNullOrWhiteSpace(currentCode))
                throw new InvalidOperationException(L("Inserisci l'Access Code della stampante.","Enter the printer Access Code."));
            PrinterDiscoveryResult found=await ElegooWebAsync.DiscoverPrinter(currentIP,currentCode,IsEnglish);
            serial.Text=found.SerialNumber;
            printer.Text=found.Hostname+"  |  "+found.MachineModel+"  |  SN: "+found.SerialNumber;
            printer.ForeColor=Color.ForestGreen;
            MessageBox.Show(this,L("Stampante rilevata.\n\nModello: ","Printer detected.\n\nModel: ")+found.MachineModel+L("\nSeriale: ","\nSerial number: ")+found.SerialNumber,
                "ELEGOO Web",MessageBoxButtons.OK,MessageBoxIcon.Information);
        });};
        ipLabel=LabelAt("",25,349,145,24); ip=Field(config.printer_ip,25,377,145);
        userLabel=LabelAt("",185,349,130,24); user=Field(config.username,185,377,130);
        codeLabel=LabelAt("",330,349,155,24); code=Field(config.access_code,330,377,155); code.UseSystemPasswordChar=true;
        serialLabel=LabelAt("",500,349,185,24); serial=Field(config.serial,500,377,185);
        languageLabel=LabelAt("",25,426,210,24);
        language=new ComboBox {Bounds=new Rectangle(25,454,160,30),DropDownStyle=ComboBoxStyle.DropDownList};
        language.Items.AddRange(new object[]{"Italiano","English"}); language.SelectedIndex=config.language=="en"?1:0; Controls.Add(language);
        showCode=new CheckBox {Bounds=new Rectangle(215,452,235,30)}; Controls.Add(showCode);
        showCode.CheckedChanged+=delegate {code.UseSystemPasswordChar=!showCode.Checked;};
        save=ButtonAt("",490,445,195,44);
        footerInfo=LabelAt("",25,516,660,24);
        footerSecurity=LabelAt("",25,544,660,24);
        language.SelectedIndexChanged+=delegate {ApplyLanguage();};
        ApplyLanguage();
        open.Click+=async delegate { await ActionAsync(async delegate {
            bool running=await Task.Run(()=>OwnServerRunning());
            if(running) Process.Start(new ProcessStartInfo("http://localhost:8888/"){UseShellExecute=true});
            else await Task.Run(()=>StartServer());
        }); };
        toggle.Click+=async delegate {await ActionAsync(()=>Task.Run(()=>{if(OwnServerRunning()) StopServer();else StartServer();}));};
        save.Click+=async delegate {await ActionAsync(async delegate {
            Config next=new Config {printer_ip=ip.Text.Trim(),username=user.Text.Trim(),access_code=code.Text,serial=serial.Text.Trim(),language=language.SelectedIndex==1?"en":"it",port=8888};
            if(!next.Complete()) throw new InvalidOperationException(L("Completa IP IPv4, Username, Access Code e Serial Number.","Complete IPv4 address, Username, Access Code and Serial Number."));
            ConfigStore.Save(ConfigPath,next); config=next;
            await Task.Run(()=>{StopServer();StartServer();});
        });};
        timer.Interval=5000; timer.Tick+=async delegate {await Probe();};
        Shown+=async delegate {if(configWarning) Error(L("Configurazione non leggibile. Reinserisci i dati e salva.","Configuration could not be read. Enter the details again and save."));timer.Start();await Probe();};
        FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy){e.Cancel=true;return;} closing=true;timer.Stop();};
        FormClosed+=delegate {timer.Dispose();if(qr.Image!=null) qr.Image.Dispose();};
    }
    Label LabelAt(string text,int x,int y,int w,int h) {Label l=new Label {Text=text,Bounds=new Rectangle(x,y,w,h)}; Controls.Add(l);return l;}
    TextBox Field(string text,int x,int y,int w) {TextBox t=new TextBox {Text=text??"",Bounds=new Rectangle(x,y,w,28)};Controls.Add(t);return t;}
    Button ButtonAt(string text,int x,int y,int w,int h) {Button b=new Button {Text=text,Bounds=new Rectangle(x,y,w,h)};Controls.Add(b);return b;}
    bool IsEnglish { get { return language!=null && language.SelectedIndex==1; } }
    string L(string italian,string english){return IsEnglish?english:italian;}
    void ApplyLanguage(){
        Text="ELEGOO Web 4.4.0";
        mobileLabel.Text=L("ACCESSO MOBILE","MOBILE ACCESS");
        copy.Text=L("COPIA","COPY");
        open.Text=L("APRI WEB UI","OPEN WEB UI");
        detect.Text=L("RILEVA STAMPANTE","DETECT PRINTER");
        ipLabel.Text=L("IP stampante","Printer IP");
        userLabel.Text="Username";
        codeLabel.Text="Access Code";
        serialLabel.Text="Serial Number";
        languageLabel.Text=L("Lingua interfaccia","Interface language");
        showCode.Text=L("Mostra Access Code","Show Access Code");
        save.Text=L("SALVA E AVVIA","SAVE AND START");
        footerInfo.Text=L("Chiudendo la finestra, il server resta disponibile ai dispositivi mobili.","Closing this window leaves the server available to mobile devices.");
        footerSecurity.Text=L("Impostazioni salvate sul PC. Access Code nel file config.json in chiaro.","Settings are stored on this PC. Access Code is plain text in config.json.");
        status.Text=L("SERVER: verifica in corso","SERVER: checking");
        printer.Text=L("STAMPANTE: verifica in corso","PRINTER: checking");
        qrHint.Text=L("QR mobile","Mobile QR");
        try { using(Process p=OwnServer()) { toggle.Text=p!=null?L("ARRESTA SERVER","STOP SERVER"):L("AVVIA SERVER","START SERVER"); } } catch { toggle.Text=L("AVVIA SERVER","START SERVER"); }
    }
    void Error(string text){if(!closing) MessageBox.Show(this,text,"ELEGOO Web",MessageBoxButtons.OK,MessageBoxIcon.Warning);}
    Process OwnServer() {
        Process found=null;
        foreach(Process p in Process.GetProcessesByName("ElegooWebServer")) {
            try {if(found==null && String.Equals(Path.GetFullPath(p.MainModule.FileName),ServerPath,StringComparison.OrdinalIgnoreCase)){found=p;continue;}}
            catch(System.ComponentModel.Win32Exception){} catch(InvalidOperationException){}
            p.Dispose();
        }
        return found;
    }
    bool OwnServerRunning(){using(Process p=OwnServer()) return p!=null;}
    void StartServer() {
        if(OwnServerRunning()) return;
        if(!File.Exists(ServerPath)) throw new FileNotFoundException(L("Manca ElegooWebServer.exe. Estrai tutto il pacchetto.","ElegooWebServer.exe is missing. Extract the complete package."));
        if(!ConfigStore.Load(ConfigPath).Complete()) throw new InvalidOperationException(L("Configura la stampante e premi SALVA E AVVIA.","Configure the printer and select SAVE AND START."));
        TcpListener listener=new TcpListener(IPAddress.Any,8888);
        try{listener.Start();}catch(SocketException){throw new InvalidOperationException(L("Porta 8888 occupata. Arresta il server della vecchia copia.","Port 8888 is in use. Stop the server from the previous copy."));}finally{listener.Stop();}
        using(Process p=Process.Start(new ProcessStartInfo(ServerPath){WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true})) {}
    }
    void StopServer() {
        using(Process p=OwnServer()) {
            if(p==null) return;
            try{p.Kill();if(!p.WaitForExit(5000))throw new TimeoutException(L("Il server non si e' arrestato.","The server did not stop."));}
            catch(InvalidOperationException){if(!p.HasExited)throw;}
        }
    }
    async Task ActionAsync(Func<Task> action) {
        if(busy)return; busy=true;save.Enabled=toggle.Enabled=open.Enabled=detect.Enabled=false;
        try { await action(); }
        catch(Exception e) { Error(e.Message); }
        finally {
            busy=false;
            if(!closing) save.Enabled=toggle.Enabled=open.Enabled=detect.Enabled=true;
        }
        // C# 5: await is not supported inside catch/finally.
        if(!closing) await Probe();
    }
    async Task Probe() {
        if(probing||closing||busy)return; probing=true;
        try{
            string currentIP=config.printer_ip;
            ElegooWebStatus result=await ElegooWebAsync.Probe(currentIP);
            bool running=await Task.Run(()=>OwnServerRunning());
            if(closing||busy||currentIP!=config.printer_ip)return;
            status.Text=running?(result.HttpReady?L("SERVER ATTIVO","SERVER RUNNING"):L("SERVER IN AVVIO / NON RISPONDE","SERVER STARTING / NOT RESPONDING")):L("SERVER ARRESTATO","SERVER STOPPED");
            status.ForeColor=running&&result.HttpReady?Color.ForestGreen:Color.DarkOrange;
            printer.Text=(String.IsNullOrEmpty(currentIP)?L("Non configurata","Not configured"):currentIP)+"  |  "+(result.PrinterReachable?"PORT 9001 OK":L("NON RAGGIUNGIBILE","UNREACHABLE"));
            toggle.Text=running?L("ARRESTA SERVER","STOP SERVER"):L("AVVIA SERVER","START SERVER");
            string url=String.IsNullOrEmpty(result.LanIP)?"":"http://"+result.LanIP+":8888/";
            mobile.Text=url;
            if(url.Length==0){lastQr="";if(qr.Image!=null){qr.Image.Dispose();qr.Image=null;}}
            else if(url!=lastQr) await UpdateQr(url);
        }catch(Exception e){if(!closing) status.Text=L("Verifica non riuscita: ","Check failed: ")+e.Message;}finally{probing=false;}
    }
    async Task UpdateQr(string url) {
        string helper=Path.Combine(root,"QRHelper.exe");
        if(!File.Exists(helper)){qrHint.Text=L("QRHelper.exe assente","QRHelper.exe missing");return;}
        string temp=Path.Combine(Path.GetTempPath(),"ElegooWeb_"+Guid.NewGuid().ToString("N")+".png");
        try{
            byte[] bytes=await Task.Run(()=>{
                // Both arguments are generated locally; no user-provided command or shell.
                using(Process p=Process.Start(new ProcessStartInfo(helper,"\""+url+"\" \""+temp+"\""){WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true})) {
                    if(!p.WaitForExit(5000)){p.Kill();p.WaitForExit(1000);throw new TimeoutException(L("QR: tempo scaduto","QR: timed out"));}
                    if(p.ExitCode!=0)throw new InvalidOperationException(L("QR non disponibile","QR unavailable"));
                }
                return File.ReadAllBytes(temp);
            });
            if(closing)return;
            using(MemoryStream s=new MemoryStream(bytes)) using(Image img=Image.FromStream(s)) {
                Bitmap b=new Bitmap(img);Image old=qr.Image;qr.Image=b;if(old!=null)old.Dispose();
            }
            lastQr=url;qrHint.Text=L("Scansiona dal telefono","Scan with your phone");
        }catch(Exception){if(!closing)qrHint.Text=L("QR non disponibile","QR unavailable");}finally{try{if(File.Exists(temp))File.Delete(temp);}catch(IOException){}}
    }
}
public static class Program {
    [STAThread] public static void Main() {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        try{Application.Run(new MainWindow());}
        catch(Exception e){MessageBox.Show(e.Message,"ELEGOO Web - startup error / errore avvio",MessageBoxButtons.OK,MessageBoxIcon.Error);}
    }
}
