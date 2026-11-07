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
        config.Save();

        NativeBridge.InitTimerResolution();

        string exeName = gameKey switch
        {
            "u1" => "eboot.bin",
            "u2" => "big2-ps4_Shipping.elf",
            "u3" => "big3-ps4_Shipping.elf",
            _ => "big3-ps4_Shipping.elf"
        };

        string pythonExe = "python.exe";
        string runScript = Path.Combine(workspaceRoot, "Project", "tools", "probe_uncharted.py");

        var psi = new ProcessStartInfo
        {
            FileName = pythonExe,
            Arguments = $"-u \"{runScript}\" --boot --seconds 0 --executable \"{exeName}\" --game \"{config.GameDir}\"",
            WorkingDirectory = Path.Combine(workspaceRoot, "Project"),
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };

        // Strict environment enforcement
        string engineConfigPath = Path.Combine(workspaceRoot, "Project", "EngineConfig.ini");
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
