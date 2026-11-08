using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;

namespace UnchartedLauncher;

public class GameRunner
{
    private Process? _process;

    public event Action<string>? LogReceived;
    public event Action<string>? StatusChanged;
    public event Action<int>? GameExited;

    public bool IsRunning => _process != null && !_process.HasExited;

    public async Task<bool> LaunchAsync(LauncherConfig config, string workspaceRoot, string gameKey)
    {
        if (IsRunning) return false;

        config.SyncToIniFiles(workspaceRoot);
        config.Save(workspaceRoot);

        NativeBridge.InitTimerResolution();

        string exeName = gameKey switch
        {
            "u1" => "eboot.bin",
            "u2" => "big2-ps4_Shipping.elf",
            "u3" => "big3-ps4_Shipping.elf",
            _ => "big3-ps4_Shipping.elf"
        };

        string[] scriptCandidates = {
            Path.Combine(workspaceRoot, "core", "tools", "probe_uncharted.py"),
            Path.Combine(workspaceRoot, "tools", "probe_uncharted.py"),
            Path.Combine(workspaceRoot, "Project", "tools", "probe_uncharted.py"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "core", "tools", "probe_uncharted.py"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tools", "probe_uncharted.py")
        };
        string runScript = "";
        foreach (var sc in scriptCandidates)
        {
            if (File.Exists(sc))
            {
                runScript = sc;
                break;
            }
        }
        if (string.IsNullOrEmpty(runScript))
        {
            StatusChanged?.Invoke("Error: No se encontró probe_uncharted.py en la instalación.");
            return false;
        }

        string pythonExe = ResolvePython(workspaceRoot);

        string workingDir = Path.GetDirectoryName(Path.GetDirectoryName(runScript)) ?? workspaceRoot;
        if (!Directory.Exists(workingDir) || workingDir == "C:\\" || workingDir == "C:\\Users")
        {
            if (Directory.Exists(Path.Combine(workspaceRoot, "core"))) workingDir = Path.Combine(workspaceRoot, "core");
            else if (Directory.Exists(Path.Combine(workspaceRoot, "Project"))) workingDir = Path.Combine(workspaceRoot, "Project");
            else workingDir = workspaceRoot;
        }

        var psi = new ProcessStartInfo
        {
            FileName = pythonExe,
            Arguments = $"-u \"{runScript}\" --boot --seconds 0 --executable \"{exeName}\" --game \"{config.GameDir}\"",
            WorkingDirectory = workingDir,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };

        // Append core and python directories to PATH
        string coreDir = Directory.Exists(Path.Combine(workspaceRoot, "core")) ? Path.Combine(workspaceRoot, "core") : workingDir;
        string currentPath = Environment.GetEnvironmentVariable("PATH") ?? "";
        psi.Environment["PATH"] = $"{coreDir};{Path.Combine(coreDir, "python")};{currentPath}";

        // Strict environment enforcement
        string[] iniCandidates = {
            Path.Combine(workspaceRoot, "core", "EngineConfig.ini"),
            Path.Combine(workspaceRoot, "EngineConfig.ini"),
            Path.Combine(workspaceRoot, "Project", "EngineConfig.ini"),
            Path.Combine(workingDir, "EngineConfig.ini")
        };
        string engineConfigPath = iniCandidates[0];
        foreach (var cand in iniCandidates)
        {
            if (File.Exists(cand)) { engineConfigPath = cand; break; }
        }
        psi.Environment["GOW3_CONFIG"] = engineConfigPath;
        psi.Environment["UNCHARTED_CONFIG_FILE"] = engineConfigPath;
        psi.Environment["GOW3_GRAPHICS_API"] = config.GraphicsApi;
        psi.Environment["GOW3_VRAM_BUDGET_MB"] = config.VramBudgetMb;
        psi.Environment["GOW3_FRAME_PACER"] = config.FramePacingMode;
        if (!string.IsNullOrEmpty(config.GpuId) && config.GpuId != "-1")
        {
            psi.Environment["GOW3_GPU_ID"] = config.GpuId;
        }
        if (!string.IsNullOrEmpty(config.GpuName))
        {
            psi.Environment["GOW3_GPU_NAME"] = config.GpuName;
            psi.Environment["GOW3_GPU"] = config.GpuName;
        }
        psi.Environment["GOW3_FPS_LIMIT"] = config.FpsLimit;
        psi.Environment["GOW3_VBLANK_HZ"] = (config.FpsLimit == "60") ? "60" : "0";
        psi.Environment["GOW3_VSYNC"] = config.VSync;
        psi.Environment["GOW3_TEXTURE_QUALITY"] = config.TextureQuality;
        psi.Environment["GOW3_SHADOW_QUALITY"] = config.ShadowQuality;
        psi.Environment["GOW3_SSAO_QUALITY"] = config.SsaoQuality;
        psi.Environment["GOW3_DYNAMIC_SHADOWS"] = config.DynamicShadows;
        psi.Environment["GOW3_VIEW_DISTANCE"] = config.ViewDistance;
        if (config.DynamicShadows == "0") {
            psi.Environment["GOW3_SUN_SHADOW_DISTANCE"] = "0";
            psi.Environment["GOW3_LIGHT_SHADOW_DISTANCE"] = "0";
        } else if (config.ShadowQuality == "low") {
            psi.Environment["GOW3_SUN_SHADOW_DISTANCE"] = "50";
            psi.Environment["GOW3_LIGHT_SHADOW_DISTANCE"] = "50";
        }
        if (config.ViewDistance == "very_low") psi.Environment["GOW3_FADE_DISTANCE"] = "50";
        else if (config.ViewDistance == "low") psi.Environment["GOW3_FADE_DISTANCE"] = "75";
        else if (config.ViewDistance == "medium") psi.Environment["GOW3_FADE_DISTANCE"] = "100";
        else if (config.ViewDistance == "high") psi.Environment["GOW3_FADE_DISTANCE"] = "125";
        else if (config.ViewDistance == "ultra") psi.Environment["GOW3_FADE_DISTANCE"] = "150";
        else if (config.ViewDistance == "auto") psi.Environment["GOW3_FADE_DISTANCE"] = "75";
        if (!string.IsNullOrEmpty(config.GpuId) && config.GpuId != "-1") {
            psi.Environment["VK_DEVICE_INDEX"] = config.GpuId;
        }
        psi.Environment["UNCHARTED_ANISOTROPY"] = config.Anisotropy;
        psi.Environment["UNCHARTED_RESOLUTION"] = config.Resolution;
        if (!string.IsNullOrWhiteSpace(config.OutputResolution))
        {
            psi.Environment["UNCHARTED_OUTPUT_RES"] = config.OutputResolution;
        }

        int vramMb = 0;
        int.TryParse(config.VramBudgetMb, out vramMb);
        if (vramMb > 0 && vramMb <= 2048)
        {
            psi.Environment["GOW3_LOW_VRAM"] = "1";
        }

        psi.Environment["UNCHARTED_FULL_RES_TEXTURES"] = config.FullResTextures ? "1" : "0";
        psi.Environment["UNCHARTED_HIRES_SHADOWS"] = config.HiresShadows ? "1" : "0";
        psi.Environment["UNCHARTED_NO_MOTION_BLUR"] = config.MotionBlur ? "0" : "1";
        psi.Environment["UNCHARTED_NO_DOF"] = config.DepthOfField ? "0" : "1";
        psi.Environment["UNCHARTED_DIAGNOSTICS"] = config.Diagnostics ? "1" : "0";

        // Controls & Input variables
        psi.Environment["GOW3_MOUSE"] = "1";
        psi.Environment["GOW3_MOUSE_SENS"] = config.MouseSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);
        psi.Environment["GOW3_MOUSE_AIM_SENS"] = config.MouseAimSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);
        psi.Environment["GOW3_MOUSE_VERTICAL"] = config.MouseVerticalSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);
        psi.Environment["GOW3_MOUSE_INVERT"] = config.MouseInvertY ? "1" : "0";
        psi.Environment["GOW3_MOUSE_INVERT_X"] = config.MouseInvertX ? "1" : "0";
        psi.Environment["GOW3_STICK_SPEED"] = config.StickSpeed.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);
        psi.Environment["GOW3_VIBRATION"] = config.Vibration ? "1" : "0";
        psi.Environment["GOW3_PROMPTS"] = config.Prompts;
        if (!string.IsNullOrEmpty(config.KeyMap)) psi.Environment["GOW3_KEY_MAP"] = config.KeyMap;
        if (!string.IsNullOrEmpty(config.PadMap)) psi.Environment["GOW3_PAD_MAP"] = config.PadMap;
        if (!string.IsNullOrEmpty(config.MouseMap)) psi.Environment["GOW3_MOUSE_MAP"] = config.MouseMap;

        try
        {
            _process = new Process { StartInfo = psi, EnableRaisingEvents = true };

            _process.OutputDataReceived += (s, e) =>
            {
                if (!string.IsNullOrEmpty(e.Data))
                {
                    LogReceived?.Invoke(e.Data);
                }
            };
            _process.ErrorDataReceived += (s, e) =>
            {
                if (!string.IsNullOrEmpty(e.Data))
                {
                    LogReceived?.Invoke("[ERR] " + e.Data);
                }
            };

            _process.Exited += (s, e) =>
            {
                int code = _process?.ExitCode ?? 0;
                StatusChanged?.Invoke($"Juego finalizado (Código {code})");
                GameExited?.Invoke(code);
                _process = null;
            };

            bool started = _process.Start();
            if (!started) return false;

            _process.BeginOutputReadLine();
            _process.BeginErrorReadLine();

            NativeBridge.AllowSetForeground((uint)_process.Id);
            StatusChanged?.Invoke($"Ejecutando Uncharted ({config.GraphicsApi.ToUpper()} - {config.VramBudgetMb} MB Cap)");

            return true;
        }
        catch (Exception ex)
        {
            StatusChanged?.Invoke($"Error al iniciar: {ex.Message}");
            return false;
        }
    }

    public static string ResolvePython(string workspaceRoot)
    {
        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string[] embeddedCandidates = {
            Path.Combine(workspaceRoot, "core", "python", "python.exe"),
            Path.Combine(baseDir, "core", "python", "python.exe"),
            Path.Combine(workspaceRoot, "python", "python.exe"),
            Path.Combine(baseDir, "python", "python.exe")
        };
        foreach (var cand in embeddedCandidates)
        {
            if (File.Exists(cand)) return cand;
        }

        string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        string programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        string[] commonPaths = {
            Path.Combine(localAppData, @"Programs\Python\Python312\python.exe"),
            Path.Combine(localAppData, @"Programs\Python\Python311\python.exe"),
            Path.Combine(localAppData, @"Programs\Python\Python310\python.exe"),
            Path.Combine(localAppData, @"Programs\Python\Python39\python.exe"),
            Path.Combine(programFiles, @"Python312\python.exe"),
            Path.Combine(programFiles, @"Python311\python.exe"),
            Path.Combine(programFiles, @"Python310\python.exe"),
            Path.Combine(localAppData, @"Microsoft\WindowsApps\python.exe")
        };
        foreach (var path in commonPaths)
        {
            if (File.Exists(path)) return path;
        }

        return "python.exe";
    }

    public void Stop()
    {
        if (_process != null && !_process.HasExited)
        {
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "taskkill",
                    Arguments = $"/PID {_process.Id} /T /F",
                    CreateNoWindow = true,
                    UseShellExecute = false
                })?.WaitForExit(3000);
            }
            catch { }
            _process = null;
            StatusChanged?.Invoke("Juego detenido.");
        }
    }
}
