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
using System.Web.Script.Serialization;
using System.Collections.Generic;

static class Program {
    [STAThread]
    static void Main(string[] args) {
        if (Array.IndexOf(args, "--smoke-test") >= 0) {
            Application.EnableVisualStyles();
            using (RelayWindow window = new RelayWindow()) {
                window.UpdateStatus("검증 모드: 서비스 시작 안 함", "중지", "중지", "미확인", "캐시 정보 대기", false);
                window.Show(); Application.DoEvents();
                using (Bitmap capture = new Bitmap(window.Width, window.Height)) {
                    window.DrawToBitmap(capture, new Rectangle(Point.Empty, capture.Size));
                    capture.Save(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "gui-smoke.png"));
                }
                if (window.Controls.Count == 0) Environment.Exit(1);
            }
            return;
        }
        bool created;
        using (Mutex mutex = new Mutex(true, "Local\\M3TubeRelayLauncher", out created)) {
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
    public string Secret = "";
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
        lastStart = DateTime.Now;
        if (log == null) {
            if (System.IO.File.Exists(LogPath) && new FileInfo(LogPath).Length > 5 * 1024 * 1024) System.IO.File.Delete(LogPath);
            log = new StreamWriter(LogPath, true, new UTF8Encoding(false));
            log.AutoFlush = true;
        }
        ProcessStartInfo info = new ProcessStartInfo(File, Arguments);
        info.WorkingDirectory = directory; info.UseShellExecute = false; info.CreateNoWindow = true;
        info.RedirectStandardOutput = true; info.RedirectStandardError = true;
        info.StandardOutputEncoding = Encoding.UTF8; info.StandardErrorEncoding = Encoding.UTF8;
        if (process != null) process.Dispose();
        process = new Process(); process.StartInfo = info;
        process.OutputDataReceived += Write; process.ErrorDataReceived += Write;
        Write(null, "---- " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " start: " + File + " " + Arguments);
        process.Start();
        Job.Add(process);
        process.BeginOutputReadLine(); process.BeginErrorReadLine();
        lastStart = DateTime.Now;
    }
    void Write(object sender, DataReceivedEventArgs e) { if (e.Data != null) Write(sender, e.Data); }
    void Write(object sender, string line) { lock (this) { if (log != null) log.WriteLine(Secret.Length > 0 ? line.Replace(Secret, "[redacted]") : line); } }
    // Called periodically: restarts a crashed child, at most once per 10 seconds.
    public bool Supervise() {
        if (!Wanted || Running || (DateTime.Now - lastStart).TotalSeconds < 10) return false;
        Restarts++; Start(); return true;
    }
    public void Stop() {
        Wanted = false;
        try {
            if (Running) {
                ProcessStartInfo kill = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "taskkill.exe"), "/PID " + process.Id + " /T /F");
                kill.UseShellExecute = false; kill.CreateNoWindow = true;
                using (Process killer = Process.Start(kill)) { killer.WaitForExit(5000); }
                if (Running) process.Kill();
                process.WaitForExit(3000);
            }
        } catch (Exception) { try { if (Running) process.Kill(); } catch (Exception) { } }
    }
}

class RelayWindow : Form {
    public readonly Button StartButton = new Button(), StopButton = new Button(), RestartButton = new Button();
    public readonly Button FolderButton = new Button(), HealthButton = new Button(), ConfigButton = new Button(), QuitButton = new Button();
    public readonly CheckBox AutoStart = new CheckBox();
    readonly Label summary = new Label(), services = new Label(), tools = new Label(), cache = new Label();
    public readonly TextBox RelayLog = new TextBox(), TunnelLog = new TextBox();
    public bool Quitting;
    public RelayWindow() {
        Text = "M3 유튜브 중계 관리자"; Size = new Size(900, 670); MinimumSize = new Size(740, 520);
        StartPosition = FormStartPosition.CenterScreen; Font = new Font("맑은 고딕", 10);
        TableLayoutPanel layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(18), ColumnCount = 1, RowCount = 7 };
        foreach (int height in new int[] { 45, 35, 35, 35, 50, 35 }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); Controls.Add(layout);
        summary.Font = new Font(Font.FontFamily, 16, FontStyle.Bold);
        foreach (Label label in new Label[] { summary, services, tools, cache }) { label.AutoEllipsis = true; label.Dock = DockStyle.Fill; label.TextAlign = ContentAlignment.MiddleLeft; layout.Controls.Add(label); }
        FlowLayoutPanel actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        Button[] buttons = { StartButton, StopButton, RestartButton, HealthButton, FolderButton, ConfigButton, QuitButton };
        string[] names = { "시작", "중지", "재시작", "공개 상태", "폴더 열기", "설정 파일", "종료" };
        for (int i = 0; i < buttons.Length; i++) { buttons[i].Text = names[i]; buttons[i].Size = new Size(92, 35); actions.Controls.Add(buttons[i]); }
        layout.Controls.Add(actions);
        AutoStart.Text = "Windows 시작 시 자동 실행    |    창을 닫으면 트레이에서 계속 실행됩니다.";
        AutoStart.Dock = DockStyle.Fill; layout.Controls.Add(AutoStart);
        TabControl tabs = new TabControl { Dock = DockStyle.Fill };
        TextBox[] logs = { RelayLog, TunnelLog }; string[] titles = { "중계 서버 로그", "Cloudflare 터널 로그" };
        for (int i = 0; i < logs.Length; i++) {
            TabPage page = new TabPage(titles[i]); logs[i].Multiline = true; logs[i].ReadOnly = true;
            logs[i].ScrollBars = ScrollBars.Both; logs[i].WordWrap = false; logs[i].Dock = DockStyle.Fill;
            logs[i].Font = new Font("Consolas", 10); page.Controls.Add(logs[i]); tabs.TabPages.Add(page);
        }
        layout.Controls.Add(tabs);
        FormClosing += delegate(object sender, FormClosingEventArgs e) { if (!Quitting) { e.Cancel = true; Hide(); } };
    }
    public void UpdateStatus(string title, string server, string tunnel, string readiness, string storage, bool wanted) {
        summary.Text = title; services.Text = "중계 서버: " + server + "    |    터널: " + tunnel;
        tools.Text = readiness; cache.Text = storage;
        StartButton.Enabled = !wanted; StopButton.Enabled = wanted; RestartButton.Enabled = wanted;
    }
    public void UpdateLogs(string relay, string tunnel) {
        UpdateLog(RelayLog, relay); UpdateLog(TunnelLog, tunnel);
    }
    static void UpdateLog(TextBox box, string text) {
        if (box.Text == text) return;
        box.Text = text; box.SelectionStart = box.TextLength; box.ScrollToCaret();
    }
}

class RelayContext : ApplicationContext {
    readonly string root = AppDomain.CurrentDomain.BaseDirectory;
    readonly NotifyIcon tray = new NotifyIcon();
    readonly ToolStripMenuItem statusItem = new ToolStripMenuItem("시작 중...");
    readonly ToolStripMenuItem autostartItem = new ToolStripMenuItem("Windows 시작 시 자동 실행");
    readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
    Service server, tunnel;
    readonly RelayWindow window = new RelayWindow();
    string localUrl = "http://127.0.0.1:8790", token = "";
    string readiness = "도구 준비 상태 확인 중", storage = "캐시 정보 확인 중";
    bool quitting;
    string publicUrl = "https://tube.audreaming.com";
    int state = -1; // 0 stopped, 1 local only, 2 public ok
    bool checking;
    const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

    public RelayContext() {
        ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072; // TLS 1.2
        statusItem.Enabled = false;
        ContextMenuStrip menu = new ContextMenuStrip();
        menu.Items.Add("관리 창 열기", null, delegate { OpenWindow(); });
        menu.Items.Add(statusItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("공개 주소 상태 확인 (브라우저)", null, delegate { Open(publicUrl + "/healthz"); });
        menu.Items.Add("중계 서버 로그 보기", null, delegate { if (server != null) OpenLog(server.LogPath); });
        menu.Items.Add("터널 로그 보기", null, delegate { if (tunnel != null) OpenLog(tunnel.LogPath); });
        menu.Items.Add("중계 서버 폴더 열기", null, delegate { Open(root); });
        autostartItem.Checked = Autostart;
        autostartItem.Click += delegate { try { Autostart = !Autostart; autostartItem.Checked = Autostart; window.AutoStart.Checked = Autostart; } catch (Exception error) { Fail(error.Message); } };
        menu.Items.Add(autostartItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("시작", null, delegate { StartServices(); });
        menu.Items.Add("중지", null, delegate { StopServices(); });
        menu.Items.Add("다시 시작", null, delegate { Restart(); });
        menu.Items.Add("종료", null, delegate { Quit(); });
        tray.ContextMenuStrip = menu;
        tray.Text = "M3 유튜브 중계: 시작 중";
        tray.Icon = MakeIcon(Color.Gray);
        tray.Visible = true;
        tray.DoubleClick += delegate { OpenWindow(); };
        MainForm = window;
        window.StartButton.Click += delegate { StartServices(); };
        window.StopButton.Click += delegate { StopServices(); };
        window.RestartButton.Click += delegate { Restart(); };
        window.QuitButton.Click += delegate { Quit(); };
        window.HealthButton.Click += delegate { Open(publicUrl + "/healthz"); };
        window.FolderButton.Click += delegate { Open(root); };
        window.ConfigButton.Click += delegate { OpenLog(Path.Combine(root, "config.json")); };
        window.AutoStart.Checked = Autostart;
        window.AutoStart.CheckedChanged += delegate {
            try { Autostart = window.AutoStart.Checked; autostartItem.Checked = Autostart; }
            catch (Exception error) { Fail(error.Message); }
        };
        window.Show();
        bool prepared = false;
        try { prepared = Prepare(); } catch (Exception error) { Fail("설정을 읽지 못했습니다: " + error.Message); }
        if (!prepared) { window.UpdateStatus("설정 확인 필요: 설정 후 시작을 누르세요", "중지", "중지", readiness, storage, false); return; }
        server = new Service("relay", root, "node.exe", "server.mjs", "relay.log");
        tunnel = new Service("tunnel", root, "cloudflared.exe", "tunnel --no-autoupdate --config cloudflared.yml run", "cloudflared.log");
        server.Secret = tunnel.Secret = token;
        StartServices();
        timer.Interval = 5000;
        timer.Tick += delegate { Tick(); };
        timer.Start();
        Tick();
    }

    // Preserve existing credentials. Initial provisioning is explicit via the setup scripts.
    bool Prepare() {
        if (!File.Exists(Path.Combine(root, "server.mjs"))) {
            Fail("server.mjs 를 찾을 수 없습니다. M3TubeRelay.exe 를 tube-relay 폴더 안에서 실행하세요."); return false;
        }
        string bundled = Path.Combine(root, "runtime");
        if (Directory.Exists(bundled) && !(Environment.GetEnvironmentVariable("PATH") ?? "").StartsWith(bundled + ";"))
            Environment.SetEnvironmentVariable("PATH", bundled + ";" + Environment.GetEnvironmentVariable("PATH"));
        string missing = "";
        foreach (string tool in new string[] { "node.exe", "ffmpeg.exe", "cloudflared.exe", "python.exe" })
            if (FindOnPath(tool) == null) missing += "\n- " + tool;
        if (missing.Length > 0) { Fail("다음 프로그램을 찾을 수 없습니다 (PATH 확인):" + missing); return false; }
        if (!File.Exists(Path.Combine(root, "config.json"))) {
            Fail("config.json이 없습니다. 기존 PC의 설정을 복사하거나 소스 폴더에서 node setup.mjs를 먼저 실행하세요."); return false;
        }
        if (!File.Exists(Path.Combine(root, "cloudflared.yml"))) {
            Fail("cloudflared.yml이 없습니다. setup-tunnel.ps1로 터널을 먼저 설정하세요. 기존 PC에서는 기존 설정을 복사하세요."); return false;
        }
        Dictionary<string, object> config = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(root, "config.json")));
        if (config.ContainsKey("publicUrl")) publicUrl = Convert.ToString(config["publicUrl"]).TrimEnd('/');
        Uri address;
        if (!Uri.TryCreate(publicUrl, UriKind.Absolute, out address) || address.Scheme != "https") { Fail("publicUrl은 HTTPS 주소여야 합니다."); return false; }
        token = config.ContainsKey("token") ? Convert.ToString(config["token"]) : "";
        if (token.Length < 24) { Fail("config.json의 인증 토큰을 확인하세요."); return false; }
        int port = config.ContainsKey("port") ? Convert.ToInt32(config["port"]) : 8790;
        localUrl = "http://127.0.0.1:" + port;
        return true;
    }

    void Tick() {
        try {
        if (server.Supervise()) tray.ShowBalloonTip(3000, "M3 유튜브 중계", "중계 서버를 다시 시작했습니다.", ToolTipIcon.Warning);
        if (tunnel.Supervise()) tray.ShowBalloonTip(3000, "M3 유튜브 중계", "Cloudflare 터널을 다시 시작했습니다.", ToolTipIcon.Warning);
        } catch (Exception error) { readiness = "실행 오류: " + error.Message; }
        if (checking || quitting) return;
        checking = true;
        ThreadPool.QueueUserWorkItem(delegate {
            bool wanted = server.Wanted;
            bool local = wanted && Healthy(localUrl + "/healthz");
            string ready = "도구 준비 상태: 중지";
            if (local) {
                try {
                    Dictionary<string, object> status = ReadStatus(localUrl + "/api/status", token);
                    ready = "프로토콜: " + status["protocol"] + "    |    yt-dlp: " + (Convert.ToBoolean(status["ytdlp"]) ? "준비됨" : "확인 필요") + "    |    FFmpeg: " + (Convert.ToBoolean(status["ffmpeg"]) ? "준비됨" : "확인 필요");
                    if (Convert.ToInt32(status["protocol"]) != 2) ready += "    |    서버 업데이트 필요";
                } catch (Exception) { ready = "도구 준비 상태 확인 실패 (인증/서버 로그 확인)"; }
            }
            string cacheInfo = CacheInfo();
            string relayText = Tail(server.LogPath), tunnelText = Tail(tunnel.LogPath);
            bool online = local && Healthy(publicUrl + "/healthz");
            int next = online ? 2 : local ? 1 : 0;
            try { window.BeginInvoke((MethodInvoker)delegate {
                if (quitting) return;
                if (server.Wanted == wanted) { readiness = ready; storage = cacheInfo; Show(next); }
                window.UpdateLogs(relayText, tunnelText); checking = false;
            }); }
            catch (Exception) { checking = false; }
        });
    }
    void Show(int next) {
        string host = publicUrl.Replace("https://", "");
        string text = !server.Wanted ? "중지됨" : next == 2 ? "실행 중: " + host : next == 1 ? "터널 연결 대기 중 (로컬 서버 정상)" : "중계 서버 시작 대기 중";
        if (next == 2 && readiness.Contains("확인")) text = "서버 연결됨: 도구 준비 상태 확인 필요";
        statusItem.Text = text;
        window.UpdateStatus(text, ServiceText(server), ServiceText(tunnel), readiness, storage, server.Wanted);
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
                return response.StatusCode == HttpStatusCode.OK && reader.ReadToEnd().Contains("\"service\":\"m3-tube-relay\"");
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
    static void Open(string target) { try { Process.Start(target); } catch (Exception) { } }
    static void OpenLog(string path) { if (File.Exists(path)) Open("notepad.exe", "\"" + path + "\""); }
    static void Open(string file, string arguments) { try { Process.Start(file, arguments); } catch (Exception) { } }
    void Fail(string message) { MessageBox.Show(message, "M3 유튜브 중계", MessageBoxButtons.OK, MessageBoxIcon.Error); }
    void OpenWindow() { window.Show(); window.WindowState = FormWindowState.Normal; window.Activate(); }
    static string ServiceText(Service service) {
        return (service.Running ? "실행 중" : service.Wanted ? "시작 대기" : "중지") + " (자동 재시작 " + service.Restarts + "회)";
    }
    static Dictionary<string, object> ReadStatus(string url, string secret) {
        HttpWebRequest request = (HttpWebRequest)WebRequest.Create(url);
        request.Timeout = 4000; request.Headers["x-m3-token"] = secret;
        using (WebResponse response = request.GetResponse())
        using (StreamReader reader = new StreamReader(response.GetResponseStream()))
            return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(reader.ReadToEnd());
    }
    string Tail(string path) {
        try {
            using (FileStream file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)) {
                if (file.Length > 24000) file.Seek(-24000, SeekOrigin.End);
                using (StreamReader reader = new StreamReader(file, Encoding.UTF8)) {
                    string text = reader.ReadToEnd(); return token.Length > 0 ? text.Replace(token, "[redacted]") : text;
                }
            }
        } catch (IOException) { return "로그 대기 중"; }
    }
    string CacheInfo() {
        try {
            long bytes = 0; int count = 0;
            string directory = Path.Combine(root, "cache");
            if (Directory.Exists(directory)) foreach (string file in Directory.GetFiles(directory, "*", SearchOption.AllDirectories)) { bytes += new FileInfo(file).Length; count++; }
            return "캐시: " + count + "개 파일 / " + (bytes / 1048576.0).ToString("N1") + " MiB    |    " + publicUrl;
        } catch (IOException) { return "캐시 사용량 확인 대기"; }
    }
    void StartServices() {
        try {
            if (server != null && server.Running) return;
            if (!Prepare()) return;
            if (server == null) {
                server = new Service("relay", root, "node.exe", "server.mjs", "relay.log");
                tunnel = new Service("tunnel", root, "cloudflared.exe", "tunnel --no-autoupdate --config cloudflared.yml run", "cloudflared.log");
                server.Secret = tunnel.Secret = token;
                timer.Interval = 5000; timer.Tick += delegate { Tick(); }; timer.Start();
            }
            server.Secret = tunnel.Secret = token;
            if (!server.Running && Healthy(localUrl + "/healthz")) {
                Fail("이미 중계 서버가 실행 중입니다. 기존 start.ps1 또는 실행기를 종료한 뒤 시작하세요."); return;
            }
            server.Start(); tunnel.Start(); Show(0);
        } catch (Exception error) { StopServices(); Fail("프로그램을 시작하지 못했습니다: " + error.Message); }
    }
    void StopServices() {
        if (server == null) return;
        server.Stop(); tunnel.Stop(); readiness = "도구 준비 상태: 중지"; Show(0);
    }
    void Restart() { StopServices(); StartServices(); }
    void Quit() {
        quitting = true; timer.Stop();
        if (server != null) server.Stop();
        if (tunnel != null) tunnel.Stop();
        tray.Visible = false; tray.Dispose();
        window.Quitting = true; window.Close();
        Application.Exit();
    }
}
