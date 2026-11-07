using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace UnchartedLauncher;

public class LauncherConfig
{
    public string GameDir { get; set; } = @"D:\D'Arby\Uncharted Game\CUSA02344";
    public string SelectedGame { get; set; } = "u1";
    public string GraphicsApi { get; set; } = "dx11"; // dx11, dx12, vulkan
    public string VramBudgetMb { get; set; } = "1720"; // 820, 1720, 3200, 6144, 0
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

            string engineConfig = $"# Uncharted Global Engine Configuration (Sic Parvis Magna Collection)\ngraphics_api={GraphicsApi}\nvram_budget_mb={VramBudgetMb}\nframe_pacer={FramePacingMode}\nfps_limit={FpsLimit}\nvsync={VSync}\ntexture_quality={TextureQuality}\nshadow_quality={ShadowQuality}\nssao_quality={SsaoQuality}\nanisotropy={Anisotropy}\nupscaler={Upscaler}\ndeferred_readback=1\nstale_readback=0\n";
            string[] enginePaths = {
                Path.Combine(workspaceRoot, "Project", "EngineConfig.ini"),
                Path.Combine(workspaceRoot, "Project", "gow3.ini"),
                Path.Combine(workspaceRoot, "EngineConfig.ini"),
                Path.Combine(userDir, "EngineConfig.ini")
            };
            foreach (var ep in enginePaths)
            {
                File.WriteAllText(ep, engineConfig);
            }
        }
        catch { }
    }
}
