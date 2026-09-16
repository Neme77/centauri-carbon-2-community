using System;
using System.Net;
using System.Net.Sockets;
using System.Net.NetworkInformation;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

public class ElegooWebStatus {
    public bool HttpReady;
    public bool PrinterReachable;
    public string LanIP;
}
public sealed class PrinterDiscoveryResult {
    public string SerialNumber;
    public string MachineModel;
    public string Hostname;
}
internal sealed class SystemInfoEnvelope {
    public int error_code { get; set; }
    public SystemInfoData system_info { get; set; }
}
internal sealed class SystemInfoData {
    public string sn { get; set; }
    public string machine_model { get; set; }
    public string hostname { get; set; }
}
public static class ElegooWebAsync {
    public static Task<PrinterDiscoveryResult> DiscoverPrinter(string printerIP, string accessCode, bool english) {
        return Task.Run(() => {
            string url="http://"+printerIP+"/system/info?X-Token="+Uri.EscapeDataString(accessCode);
            try {
                var request=(HttpWebRequest)WebRequest.Create(url);
                request.Method="GET";
                request.Proxy=null;
                request.Timeout=3000;
                request.ReadWriteTimeout=3000;
                request.AllowAutoRedirect=false;
                string body;
                using(var response=(HttpWebResponse)request.GetResponse())
                using(var stream=response.GetResponseStream())
                using(var reader=new StreamReader(stream,Encoding.UTF8)) body=reader.ReadToEnd();
                var envelope=new JavaScriptSerializer().Deserialize<SystemInfoEnvelope>(body);
                if(envelope==null||envelope.error_code!=0||envelope.system_info==null)
                    throw new InvalidDataException(english?"Invalid response from the printer.":"Risposta non valida dalla stampante.");
                string sn=(envelope.system_info.sn??"").Trim();
                if(String.IsNullOrWhiteSpace(sn)) throw new InvalidDataException(english?"The printer did not return its serial number.":"La stampante non ha restituito il seriale.");
                return new PrinterDiscoveryResult {
                    SerialNumber=sn,
                    MachineModel=String.IsNullOrWhiteSpace(envelope.system_info.machine_model)?(english?"Model unavailable":"Modello non disponibile"):envelope.system_info.machine_model.Trim(),
                    Hostname=String.IsNullOrWhiteSpace(envelope.system_info.hostname)?printerIP:envelope.system_info.hostname.Trim()
                };
            } catch(WebException e) {
                var response=e.Response as HttpWebResponse;
                if(response!=null && response.StatusCode==HttpStatusCode.Unauthorized)
                    throw new InvalidOperationException(english?"Invalid Access Code.":"Access Code non valido.");
                if(e.Status==WebExceptionStatus.Timeout)
                    throw new TimeoutException(english?"The printer did not respond within 3 seconds.":"La stampante non ha risposto entro 3 secondi.");
                throw new InvalidOperationException((english?"Unable to detect the printer: ":"Impossibile rilevare la stampante: ")+e.Message);
            }
        });
    }
    public static Task<ElegooWebStatus> Probe(string printerIP) {
        return Task.Run(() => {
            var result = new ElegooWebStatus();
            IPAddress address;
            if (IPAddress.TryParse(printerIP, out address) && address.AddressFamily == AddressFamily.InterNetwork) {
                // UDP connect selects the route without sending application traffic.
                try {
                    using (var route = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp)) {
                        route.Connect(new IPEndPoint(address, 9001));
                        var local = ((IPEndPoint)route.LocalEndPoint).Address;
                        if (!IPAddress.IsLoopback(local) && !local.Equals(IPAddress.Any)) result.LanIP = local.ToString();
                    }
                } catch {}
                try {
                    using (var tcp = new TcpClient()) {
                        var connection = tcp.BeginConnect(address, 9001, null, null);
                        using (var wait = connection.AsyncWaitHandle) {
                            if (wait.WaitOne(350)) { tcp.EndConnect(connection); result.PrinterReachable = tcp.Connected; }
                        }
                    }
                } catch {}
            }
            if (String.IsNullOrEmpty(result.LanIP)) {
                try {
                    foreach (var network in NetworkInterface.GetAllNetworkInterfaces()) {
                        if (network.OperationalStatus != OperationalStatus.Up || network.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                        var properties = network.GetIPProperties();
                        if (properties.GatewayAddresses.Count == 0) continue;
                        foreach (var entry in properties.UnicastAddresses) {
                            if (entry.Address.AddressFamily == AddressFamily.InterNetwork && !entry.Address.ToString().StartsWith("169.254.")) {
                                result.LanIP = entry.Address.ToString(); break;
                            }
                        }
                        if (!String.IsNullOrEmpty(result.LanIP)) break;
                    }
                } catch {}
            }
            try {
                var request = (HttpWebRequest)WebRequest.Create("http://127.0.0.1:8888/");
                request.Method = "HEAD";
                request.Proxy = null;
                request.Timeout = 700;
                request.ReadWriteTimeout = 700;
                request.AllowAutoRedirect = false;
                using (var response = (HttpWebResponse)request.GetResponse()) {
                    result.HttpReady = (int)response.StatusCode >= 200 && (int)response.StatusCode < 400;
                }
            } catch {}
            return result;
        });
    }
}
