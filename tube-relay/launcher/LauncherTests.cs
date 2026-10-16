using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;

static class LauncherTests {
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    [STAThread] static int Main(string[] args) {
        string root = Path.GetFullPath(args[0]);
        Directory.CreateDirectory(root);
        Service service = new Service("test", root, "node.exe", "-e \"setInterval(()=>{},1000)\"", "service.log");
        try {
            service.Start(); Check(service.Wanted && service.Running, "start failed");
            service.Start(); Check(service.Running, "idempotent start failed");
            service.Stop(); Check(!service.Wanted && !service.Running, "stop failed");
            Check(!service.Supervise(), "stopped service restarted");
            service.Start(); Check(service.Running, "restart failed");
            Process child = (Process)typeof(Service).GetField("process", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(service);
            child.Kill(); child.WaitForExit();
            typeof(Service).GetField("lastStart", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(service, DateTime.Now.AddSeconds(-20));
            Check(service.Supervise() && service.Running && service.Restarts == 1, "crash recovery failed");
            service.Stop();
            using (RelayWindow window = new RelayWindow()) {
                window.UpdateStatus("중지", "중지", "중지", "미확인", "캐시", false);
                Check(window.StartButton.Enabled && !window.StopButton.Enabled, "stopped buttons");
                window.UpdateStatus("실행", "실행", "실행", "준비됨", "캐시", true);
                Check(!window.StartButton.Enabled && window.StopButton.Enabled && window.RestartButton.Enabled, "running buttons");
                window.Show(); Application.DoEvents();
                int calls = 0; window.StopButton.Click += delegate { calls++; }; window.StopButton.PerformClick();
                Check(calls == 1, "stop action binding");
                window.UpdateLogs("relay fixture", "tunnel fixture"); Check(window.RelayLog.Text == "relay fixture", "log display");
                window.Show(); window.Close(); Check(!window.Visible && !window.IsDisposed, "close should hide to tray");
                window.Quitting = true; window.Close();
            }
            Console.WriteLine("PASS: start, stop, restart, crash recovery, stopped supervision, GUI actions/logs and close-to-tray.");
            return 0;
        } catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        finally { service.Stop(); }
    }
}
