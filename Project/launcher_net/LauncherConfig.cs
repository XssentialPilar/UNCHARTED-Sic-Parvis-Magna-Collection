using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace UnchartedLauncher;

public class LauncherConfig
{
    public string GameDir { get; set; } = @"D:\D'Arby\Uncharted Game\CUSA02344";
    public string SelectedGame { get; set; } = "u1";
    public string GraphicsApi { get; set; } = "dx9"; // dx11, dx12, vulkan, dx9
    public string VramBudgetMb { get; set; } = "0"; // 820, 1720, 3200, 6144, 0
    public string FramePacingMode { get; set; } = "hybrid"; // hybrid, waitable, sleep, off
    public string TextureQuality { get; set; } = "medium"; // low, medium, high, ultra
    public string ShadowQuality { get; set; } = "medium"; // low, medium, high, ultra
    public string SsaoQuality { get; set; } = "full"; // off, half, full
    public string Anisotropy { get; set; } = "16"; // 1, 2, 4, 8, 16
    public string FpsLimit { get; set; } = "60"; // 30, 60, 90, 120, 0
    public string VSync { get; set; } = "1"; // 0, 1
    public string Resolution { get; set; } = "1920x1080";
    public string OutputResolution { get; set; } = "";
    public string Upscaler { get; set; } = "off"; // off, fsr1, fsr3, fsr4, dlaa, dlss
    public bool FullResTextures { get; set; } = false;
    public bool HiresShadows { get; set; } = false;
    public bool MotionBlur { get; set; } = false;
    public bool DepthOfField { get; set; } = false;
    public bool Diagnostics { get; set; } = false;
    public double MouseSensitivity { get; set; } = 3.0;
    public string GpuName { get; set; } = "";
    public string GpuId { get; set; } = "-1";

    private static string ConfigPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "Uncharted Launcher",
        "launcher.json");

    public static LauncherConfig Load()
    {
        try
        {
            if (File.Exists(ConfigPath))
            {
                string json = File.ReadAllText(ConfigPath);
                var cfg = JsonSerializer.Deserialize<LauncherConfig>(json);
                if (cfg != null) return cfg;
            }
        }
        catch { }
        return new LauncherConfig();
    }

    public void Save()
    {
        try
        {
            string dir = Path.GetDirectoryName(ConfigPath)!;
            Directory.CreateDirectory(dir);
            string json = JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(ConfigPath, json);
        }
        catch { }
    }

    public void SyncToIniFiles(string workspaceRoot)
    {
        try
        {
            string userDir = Path.Combine(workspaceRoot, "Project", "analysis", "uncharted-user");
            Directory.CreateDirectory(userDir);

            string[] games = { "u1", "u2", "u3", "probe" };
            foreach (var g in games)
            {
                string iniPath = Path.Combine(userDir, g == "probe" ? "probe.ini" : $"probe-{g}.ini");
                var lines = new List<string>();
                if (File.Exists(iniPath))
                {
                    lines.AddRange(File.ReadAllLines(iniPath));
                }

                void SetVal(string key, string val)
                {
                    int idx = lines.FindIndex(l => l.Trim().StartsWith(key + "=", StringComparison.OrdinalIgnoreCase));
                    if (idx >= 0) lines[idx] = $"{key}={val}";
                    else lines.Add($"{key}={val}");
                }

                SetVal("graphics_api", GraphicsApi);
                SetVal("vram_budget_mb", VramBudgetMb);
                SetVal("frame_pacer", FramePacingMode);
                SetVal("fps_limit", FpsLimit);
                SetVal("vsync", VSync);
                SetVal("texture_quality", TextureQuality);
                SetVal("shadow_quality", ShadowQuality);
                SetVal("ssao_quality", SsaoQuality);
                SetVal("anisotropy", Anisotropy);
                SetVal("upscaler", Upscaler);

                File.WriteAllLines(iniPath, lines);
            }

            string[] enginePaths = {
                Path.Combine(workspaceRoot, "Project", "EngineConfig.ini"),
                Path.Combine(workspaceRoot, "Project", "gow3.ini"),
                Path.Combine(workspaceRoot, "EngineConfig.ini"),
                Path.Combine(userDir, "EngineConfig.ini")
            };
            foreach (var ep in enginePaths)
            {
                var epLines = File.Exists(ep) ? new List<string>(File.ReadAllLines(ep)) : new List<string>();
                void SetEpVal(string key, string val)
                {
                    int idx = epLines.FindIndex(l => l.Trim().StartsWith(key + "=", StringComparison.OrdinalIgnoreCase));
                    if (idx >= 0) epLines[idx] = $"{key}={val}";
                    else epLines.Add($"{key}={val}");
                }
                SetEpVal("graphics_api", GraphicsApi);
                SetEpVal("vram_budget_mb", VramBudgetMb);
                SetEpVal("frame_pacer", FramePacingMode);
                SetEpVal("fps_limit", FpsLimit);
                SetEpVal("vsync", VSync);
                SetEpVal("texture_quality", TextureQuality);
                SetEpVal("shadow_quality", ShadowQuality);
                SetEpVal("ssao_quality", SsaoQuality);
                SetEpVal("anisotropy", Anisotropy);
                SetEpVal("upscaler", Upscaler);
                SetEpVal("deferred_readback", "1");
                SetEpVal("stale_readback", "0");
                if (!string.IsNullOrEmpty(GpuName)) SetEpVal("gpu_name", GpuName);
                if (!string.IsNullOrEmpty(GpuId)) SetEpVal("gpu_id", GpuId);

                var dir = Path.GetDirectoryName(ep);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) Directory.CreateDirectory(dir);
                File.WriteAllLines(ep, epLines);
            }
        }
        catch { }
    }
}
