using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

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
    [JsonPropertyName("gpu")]
    public string GpuName { get; set; } = "";

    [JsonPropertyName("gpu_id")]
    public string GpuId { get; set; } = "-1";

    [JsonPropertyName("dynamic_shadows")]
    public string DynamicShadows { get; set; } = "1"; // 1 = on, 0 = off

    [JsonPropertyName("view_distance")]
    public string ViewDistance { get; set; } = "medium"; // very_low, low, medium, high, ultra, auto

    [JsonPropertyName("key_map")]
    public string KeyMap { get; set; } = "ls_up=W,ls_down=S,ls_left=A,ls_right=D,rs_up=Up,rs_down=Down,rs_left=Left,rs_right=Right,left=1,right=2,up=3,down=4,cross=Space,circle=Left Ctrl,square=F,triangle=R,l2=,r2=,l1=Q,r1=E,l3=Left Shift,r3=V,options=Escape,options=Return,touchpad_left=Tab,touchpad_right=Backspace";

    [JsonPropertyName("pad_map")]
    public string PadMap { get; set; } = "left=dpleft,right=dpright,up=dpup,down=dpdown,cross=a,circle=b,square=x,triangle=y,l1=leftshoulder,r1=rightshoulder,l3=leftstick,r3=rightstick,options=start,touchpad_right=back";

    [JsonPropertyName("mouse_map")]
    public string MouseMap { get; set; } = "left=wheel_down,right=wheel_up,up=,down=,cross=,circle=,square=,triangle=,l2=right,r2=left,l1=x1,r1=x2,l3=,r3=middle,options=,touchpad_left=,touchpad_right=";

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
                var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
                var cfg = JsonSerializer.Deserialize<LauncherConfig>(json, options);
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
                SetVal("dynamic_shadows", DynamicShadows);
                SetVal("view_distance", ViewDistance);
                if (DynamicShadows == "0") {
                    SetVal("sun_shadow_distance", "0");
                    SetVal("light_shadow_distance", "0");
                } else if (ShadowQuality == "low") {
                    SetVal("sun_shadow_distance", "50");
                    SetVal("light_shadow_distance", "50");
                } else {
                    SetVal("sun_shadow_distance", "100");
                    SetVal("light_shadow_distance", "100");
                }
                if (ViewDistance == "very_low") SetVal("fade_distance", "50");
                else if (ViewDistance == "low") SetVal("fade_distance", "75");
                else if (ViewDistance == "medium") SetVal("fade_distance", "100");
                else if (ViewDistance == "high") SetVal("fade_distance", "125");
                else if (ViewDistance == "ultra") SetVal("fade_distance", "150");
                else if (ViewDistance == "auto") SetVal("fade_distance", "75");
                if (!string.IsNullOrEmpty(KeyMap)) SetVal("key_map", KeyMap);
                if (!string.IsNullOrEmpty(PadMap)) SetVal("pad_map", PadMap);
                if (!string.IsNullOrEmpty(MouseMap)) SetVal("mouse_map", MouseMap);

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
                SetEpVal("dynamic_shadows", DynamicShadows);
                SetEpVal("view_distance", ViewDistance);
                if (DynamicShadows == "0") {
                    SetEpVal("sun_shadow_distance", "0");
                    SetEpVal("light_shadow_distance", "0");
                } else if (ShadowQuality == "low") {
                    SetEpVal("sun_shadow_distance", "50");
                    SetEpVal("light_shadow_distance", "50");
                } else {
                    SetEpVal("sun_shadow_distance", "100");
                    SetEpVal("light_shadow_distance", "100");
                }
                if (ViewDistance == "very_low") SetEpVal("fade_distance", "50");
                else if (ViewDistance == "low") SetEpVal("fade_distance", "75");
                else if (ViewDistance == "medium") SetEpVal("fade_distance", "100");
                else if (ViewDistance == "high") SetEpVal("fade_distance", "125");
                else if (ViewDistance == "ultra") SetEpVal("fade_distance", "150");
                else if (ViewDistance == "auto") SetEpVal("fade_distance", "75");
                if (!string.IsNullOrEmpty(GpuName)) SetEpVal("gpu_name", GpuName);
                if (!string.IsNullOrEmpty(GpuId)) SetEpVal("gpu_id", GpuId);
                if (!string.IsNullOrEmpty(KeyMap)) SetEpVal("key_map", KeyMap);
                if (!string.IsNullOrEmpty(PadMap)) SetEpVal("pad_map", PadMap);
                if (!string.IsNullOrEmpty(MouseMap)) SetEpVal("mouse_map", MouseMap);

                var dir = Path.GetDirectoryName(ep);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) Directory.CreateDirectory(dir);
                File.WriteAllLines(ep, epLines);
            }
        }
        catch { }
    }
}
