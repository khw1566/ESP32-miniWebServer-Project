// M3 YouTube relay launcher: a tray app that keeps server.mjs and the Cloudflare
// tunnel running in the background. Build with build-exe.ps1 (.NET Framework 4, C# 5).
using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

static class Program {
    [STAThread]
    static void Main() {
        bool created;
        using (Mutex mutex = new Mutex(true, "M3TubeRelayLauncher", out created)) {
            if (!created) {
                MessageBox.Show("M3 유튜브 중계 서버가 이미 실행 중입니다. 작업 표시줄 알림 영역의 아이콘을 확인하세요.",
                    "M3 유튜브 중계", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            Application.EnableVisualStyles();
            Application.Run(new RelayContext());
        }
    }
}

// Children join a job object that is closed with this process, so a crash or
// "End task" of the launcher never leaves node or cloudflared running.
static class Job {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint length);
    [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [StructLayout(LayoutKind.Sequential)] struct BasicLimits {
        public long PerProcessUserTimeLimit, PerJobUserTimeLimit; public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize; public uint ActiveProcessLimit;
        public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)] struct IoCounters { public ulong a, b, c, d, e, f; }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {
        public BasicLimits Basic; public IoCounters Io;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }
    static readonly IntPtr handle = Create();
    static IntPtr Create() {
        IntPtr job = CreateJobObject(IntPtr.Zero, null);
        ExtendedLimits limits = new ExtendedLimits();
        limits.Basic.LimitFlags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        int size = Marshal.SizeOf(typeof(ExtendedLimits));
        IntPtr buffer = Marshal.AllocHGlobal(size);
        Marshal.StructureToPtr(limits, buffer, false);
        SetInformationJobObject(job, 9, buffer, (uint)size); // JobObjectExtendedLimitInformation
        Marshal.FreeHGlobal(buffer);
        return job;
    }
    public static void Add(Process process) { AssignProcessToJobObject(handle, process.Handle); }
}

// One supervised child process with its own log file and automatic restart.
class Service {
    public readonly string Name, File, Arguments, LogPath;
    readonly string directory;
    Process process;
    StreamWriter log;
    DateTime lastStart = DateTime.MinValue;
    public bool Wanted;
    public int Restarts;
    public Service(string name, string directory, string file, string arguments, string logName) {
        Name = name; this.directory = directory; File = file; Arguments = arguments;
        LogPath = Path.Combine(directory, logName);
    }
    public bool Running { get { return process != null && !process.HasExited; } }
    public void Start() {
        Wanted = true;
        if (Running) return;
        if (log == null) {
            if (System.IO.File.Exists(LogPath) && new FileInfo(LogPath).Length > 5 * 1024 * 1024) System.IO.File.Delete(LogPath);
            log = new StreamWriter(LogPath, true, new UTF8Encoding(false));
            log.AutoFlush = true;
        }
        ProcessStartInfo info = new ProcessStartInfo(File, Arguments);
        info.WorkingDirectory = directory; info.UseShellExecute = false; info.CreateNoWindow = true;
        info.RedirectStandardOutput = true; info.RedirectStandardError = true;
        info.StandardOutputEncoding = Encoding.UTF8; info.StandardErrorEncoding = Encoding.UTF8;
        process = new Process(); process.StartInfo = info;
        process.OutputDataReceived += Write; process.ErrorDataReceived += Write;
        Write(null, "---- " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " start: " + File + " " + Arguments);
        process.Start();
        Job.Add(process);
        process.BeginOutputReadLine(); process.BeginErrorReadLine();
        lastStart = DateTime.Now;
    }
    void Write(object sender, DataReceivedEventArgs e) { if (e.Data != null) Write(sender, e.Data); }
    void Write(object sender, string line) { lock (this) { if (log != null) log.WriteLine(line); } }
    // Called periodically: restarts a crashed child, at most once per 10 seconds.
    public bool Supervise() {
        if (!Wanted || Running || (DateTime.Now - lastStart).TotalSeconds < 10) return false;
        Restarts++; Start(); return true;
    }
    public void Stop() {
        Wanted = false;
        try { if (Running) { process.Kill(); process.WaitForExit(3000); } } catch (Exception) { }
    }
}

class RelayContext : ApplicationContext {
    readonly string root = AppDomain.CurrentDomain.BaseDirectory;
    readonly NotifyIcon tray = new NotifyIcon();
    readonly ToolStripMenuItem statusItem = new ToolStripMenuItem("시작 중...");
    readonly ToolStripMenuItem autostartItem = new ToolStripMenuItem("Windows 시작 시 자동 실행");
    readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
    Service server, tunnel;
    string publicUrl = "https://tube.audreaming.com";
    int state = -1; // 0 stopped, 1 local only, 2 public ok
    bool checking;
    const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

    public RelayContext() {
        ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072; // TLS 1.2
        statusItem.Enabled = false;
        ContextMenuStrip menu = new ContextMenuStrip();
        menu.Items.Add(statusItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("공개 주소 상태 확인 (브라우저)", null, delegate { Open(publicUrl + "/healthz"); });
        menu.Items.Add("중계 서버 로그 보기", null, delegate { OpenLog(server.LogPath); });
        menu.Items.Add("터널 로그 보기", null, delegate { OpenLog(tunnel.LogPath); });
        menu.Items.Add("중계 서버 폴더 열기", null, delegate { Open(root); });
        autostartItem.Checked = Autostart;
        autostartItem.Click += delegate { Autostart = !Autostart; autostartItem.Checked = Autostart; };
        menu.Items.Add(autostartItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("다시 시작", null, delegate { Restart(); });
        menu.Items.Add("종료", null, delegate { Quit(); });
        tray.ContextMenuStrip = menu;
        tray.Text = "M3 유튜브 중계: 시작 중";
        tray.Icon = MakeIcon(Color.Gray);
        tray.Visible = true;
        tray.DoubleClick += delegate { Open(publicUrl + "/healthz"); };
        if (!Prepare()) { Quit(); return; }
        server = new Service("relay", root, "node.exe", "server.mjs", "relay.log");
        tunnel = new Service("tunnel", root, "cloudflared.exe", "tunnel --no-autoupdate --config cloudflared.yml run", "cloudflared.log");
        try { server.Start(); tunnel.Start(); }
        catch (Exception error) { Fail("프로그램을 시작하지 못했습니다: " + error.Message); Quit(); return; }
        timer.Interval = 5000;
        timer.Tick += delegate { Tick(); };
        timer.Start();
        Tick();
    }

    // First run: create the token/config and the tunnel config the same way setup.mjs / setup-tunnel.ps1 do.
    bool Prepare() {
        if (!File.Exists(Path.Combine(root, "server.mjs"))) {
            Fail("server.mjs 를 찾을 수 없습니다. M3TubeRelay.exe 를 tube-relay 폴더 안에서 실행하세요."); return false;
        }
        string missing = "";
        foreach (string tool in new string[] { "node.exe", "ffmpeg.exe", "cloudflared.exe", "python.exe" })
            if (FindOnPath(tool) == null) missing += "\n- " + tool;
        if (missing.Length > 0) { Fail("다음 프로그램을 찾을 수 없습니다 (PATH 확인):" + missing); return false; }
        if (!File.Exists(Path.Combine(root, "config.json")) && RunAndWait("node.exe", "setup.mjs") != 0) {
            Fail("setup.mjs 실행에 실패했습니다."); return false;
        }
        if (!File.Exists(Path.Combine(root, "cloudflared.yml")) &&
            RunAndWait("powershell.exe", "-NoProfile -ExecutionPolicy Bypass -File setup-tunnel.ps1") != 0 &&
            !File.Exists(Path.Combine(root, "cloudflared.yml"))) {
            Fail("Cloudflare 터널 설정에 실패했습니다. 'cloudflared tunnel login' 후 다시 실행하세요."); return false;
        }
        Match url = Regex.Match(File.ReadAllText(Path.Combine(root, "config.json")), "\"publicUrl\"\\s*:\\s*\"([^\"]+)\"");
        if (url.Success) publicUrl = url.Groups[1].Value.TrimEnd('/');
        return true;
    }

    void Tick() {
        if (server.Supervise()) tray.ShowBalloonTip(3000, "M3 유튜브 중계", "중계 서버를 다시 시작했습니다.", ToolTipIcon.Warning);
        if (tunnel.Supervise()) tray.ShowBalloonTip(3000, "M3 유튜브 중계", "Cloudflare 터널을 다시 시작했습니다.", ToolTipIcon.Warning);
        if (checking) return;
        checking = true;
        ThreadPool.QueueUserWorkItem(delegate {
            bool local = Healthy("http://127.0.0.1:8790/healthz");
            bool online = local && Healthy(publicUrl + "/healthz");
            int next = online ? 2 : local ? 1 : 0;
            try { tray.ContextMenuStrip.BeginInvoke((MethodInvoker)delegate { Show(next); checking = false; }); }
            catch (Exception) { checking = false; }
        });
    }
    void Show(int next) {
        string host = publicUrl.Replace("https://", "");
        string text = next == 2 ? "실행 중: " + host : next == 1 ? "터널 연결 대기 중 (로컬 서버 정상)" : "중계 서버 시작 대기 중";
        statusItem.Text = text;
        tray.Text = ("M3 유튜브 중계: " + text).Length > 63 ? ("M3 유튜브 중계: " + text).Substring(0, 63) : "M3 유튜브 중계: " + text;
        if (next == state) return;
        if (state >= 0 && next == 2) tray.ShowBalloonTip(3000, "M3 유튜브 중계", "보드에서 유튜브를 사용할 수 있습니다.\n" + host, ToolTipIcon.Info);
        state = next;
        Icon old = tray.Icon;
        tray.Icon = MakeIcon(next == 2 ? Color.FromArgb(52, 211, 153) : next == 1 ? Color.FromArgb(251, 191, 36) : Color.FromArgb(248, 113, 113));
        if (old != null) old.Dispose();
    }
    static bool Healthy(string url) {
        try {
            HttpWebRequest request = (HttpWebRequest)WebRequest.Create(url);
            request.Timeout = 4000; request.UserAgent = "M3TubeRelayLauncher";
            using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
            using (StreamReader reader = new StreamReader(response.GetResponseStream()))
                return response.StatusCode == HttpStatusCode.OK && reader.ReadToEnd().Contains("\"ok\":true");
        } catch (Exception) { return false; }
    }

    // Red rounded tile with a play triangle; the dot shows the relay state.
    static Icon MakeIcon(Color status) {
        using (Bitmap bitmap = new Bitmap(32, 32))
        using (Graphics g = Graphics.FromImage(bitmap)) {
            g.SmoothingMode = SmoothingMode.AntiAlias; g.Clear(Color.Transparent);
            using (GraphicsPath tile = Rounded(new Rectangle(1, 4, 28, 22), 7))
            using (SolidBrush red = new SolidBrush(Color.FromArgb(220, 38, 38))) g.FillPath(red, tile);
            g.FillPolygon(Brushes.White, new Point[] { new Point(11, 10), new Point(11, 20), new Point(20, 15) });
            using (SolidBrush dot = new SolidBrush(status)) g.FillEllipse(dot, 20, 18, 12, 12);
            g.DrawEllipse(new Pen(Color.FromArgb(20, 20, 20), 1.5f), 20, 18, 12, 12);
            return Icon.FromHandle(bitmap.GetHicon());
        }
    }
    static GraphicsPath Rounded(Rectangle r, int radius) {
        GraphicsPath path = new GraphicsPath(); int d = radius * 2;
        path.AddArc(r.X, r.Y, d, d, 180, 90); path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        path.CloseFigure(); return path;
    }

    static bool Autostart {
        get { using (RegistryKey key = Registry.CurrentUser.OpenSubKey(RunKey)) return key != null && key.GetValue("M3TubeRelay") != null; }
        set {
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(RunKey)) {
                if (value) key.SetValue("M3TubeRelay", "\"" + Application.ExecutablePath + "\"");
                else key.DeleteValue("M3TubeRelay", false);
            }
        }
    }
    static string FindOnPath(string file) {
        foreach (string dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(';')) {
            try { if (dir.Length > 0 && File.Exists(Path.Combine(dir.Trim('"'), file))) return Path.Combine(dir.Trim('"'), file); }
            catch (ArgumentException) { }
        }
        return null;
    }
    int RunAndWait(string file, string arguments) {
        ProcessStartInfo info = new ProcessStartInfo(file, arguments);
        info.WorkingDirectory = root; info.UseShellExecute = false; info.CreateNoWindow = true;
        using (Process process = Process.Start(info)) { process.WaitForExit(120000); return process.HasExited ? process.ExitCode : -1; }
    }
    static void Open(string target) { try { Process.Start(target); } catch (Exception) { } }
    static void OpenLog(string path) { if (File.Exists(path)) Open("notepad.exe", "\"" + path + "\""); }
    static void Open(string file, string arguments) { try { Process.Start(file, arguments); } catch (Exception) { } }
    void Fail(string message) { MessageBox.Show(message, "M3 유튜브 중계", MessageBoxButtons.OK, MessageBoxIcon.Error); }
    void Restart() {
        server.Stop(); tunnel.Stop();
        state = -1; server.Restarts = tunnel.Restarts = 0;
        server.Start(); tunnel.Start(); Tick();
    }
    void Quit() {
        timer.Stop();
        if (server != null) server.Stop();
        if (tunnel != null) tunnel.Stop();
        tray.Visible = false;
        Application.Exit();
        Environment.Exit(0);
    }
}
