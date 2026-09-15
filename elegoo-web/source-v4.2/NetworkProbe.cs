using System;
using System.Net;
using System.Net.Sockets;
using System.Net.NetworkInformation;
using System.Threading.Tasks;

public class ElegooWebStatus {
    public bool HttpReady;
    public bool PrinterReachable;
    public string LanIP;
}
public static class ElegooWebAsync {
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
