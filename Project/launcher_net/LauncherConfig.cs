using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace UnchartedLauncher;

public class ActionBinding
{
    public string Id { get; set; } = "";
    public string Group { get; set; } = "Acciones";
    public string TranslationKey { get; set; } = "";
    public string FallbackLabel { get; set; } = "";
    public string PrimaryKey { get; set; } = "";
    public string SecondaryKeyOrMouse { get; set; } = "";
    public string GamepadLabel { get; set; } = "";
    public bool IsStick { get; set; } = false;
}

public class LauncherConfig
{
    public string Language { get; set; } = "auto";
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

    // Controls & Mouse
    public double MouseSensitivity { get; set; } = 3.0;
    public double MouseAimSensitivity { get; set; } = 1.0;
    public double MouseVerticalSensitivity { get; set; } = 1.0;
    public bool MouseInvertY { get; set; } = false;
    public bool MouseInvertX { get; set; } = false;
    public double StickSpeed { get; set; } = 1.0;
    public bool Vibration { get; set; } = true;
    public string Prompts { get; set; } = "auto"; // auto, ps, xbox, kb

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

    public static string ResolveWorkspaceRoot()
    {
        string dir = AppDomain.CurrentDomain.BaseDirectory;
        DirectoryInfo? cur = new DirectoryInfo(dir);
        while (cur != null)
        {
            if (File.Exists(Path.Combine(cur.FullName, "uncharted_sic_parvis_magna_collection_master_readme.md")) ||
                Directory.Exists(Path.Combine(cur.FullName, "Project")))
            {
                return cur.FullName;
            }
            cur = cur.Parent;
        }
        return Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", ".."));
    }

    public static List<string> GetConfigPaths(string? workspaceRoot = null)
    {
        var paths = new List<string>();
        try
        {
            paths.Add(Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Uncharted Launcher",
                "launcher.json"));
        }
        catch { }

        try
        {
            paths.Add(Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "UnchartedLauncher",
                "launcher.json"));
        }
        catch { }

        string ws = workspaceRoot ?? ResolveWorkspaceRoot();
        if (!string.IsNullOrEmpty(ws))
        {
            paths.Add(Path.Combine(ws, "launcher.json"));
            paths.Add(Path.Combine(ws, "Project", "launcher.json"));
        }

        try
        {
            paths.Add(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "launcher.json"));
        }
        catch { }

        return paths;
    }

    public static LauncherConfig Load(string? workspaceRoot = null)
    {
        var paths = GetConfigPaths(workspaceRoot);
        LauncherConfig? loaded = null;

        foreach (var path in paths)
        {
            try
            {
                if (File.Exists(path))
                {
                    string json = File.ReadAllText(path);
                    if (!string.IsNullOrWhiteSpace(json))
                    {
                        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
                        var cfg = JsonSerializer.Deserialize<LauncherConfig>(json, options);
                        if (cfg != null)
                        {
                            loaded = cfg;
                            break;
                        }
                    }
                }
            }
            catch { }
        }

        if (loaded == null)
        {
            loaded = new LauncherConfig();
        }

        // Import settings from existing EngineConfig.ini or gow3.ini if present
        try
        {
            string ws = workspaceRoot ?? ResolveWorkspaceRoot();
            string[] iniCandidates = {
                Path.Combine(ws, "Project", "EngineConfig.ini"),
                Path.Combine(ws, "Project", "gow3.ini"),
                Path.Combine(ws, "EngineConfig.ini")
            };

            foreach (var ini in iniCandidates)
            {
                if (File.Exists(ini))
                {
                    foreach (var line in File.ReadAllLines(ini))
                    {
                        var trimmed = line.Trim();
                        if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith("#") || trimmed.StartsWith(";")) continue;
                        int eq = trimmed.IndexOf('=');
                        if (eq <= 0) continue;
                        string key = trimmed[..eq].Trim().ToLowerInvariant();
                        string val = trimmed[(eq + 1)..].Trim();

                        switch (key)
                        {
                            case "graphics_api": if (!string.IsNullOrEmpty(val)) loaded.GraphicsApi = val; break;
                            case "vram_budget_mb": if (!string.IsNullOrEmpty(val)) loaded.VramBudgetMb = val; break;
                            case "frame_pacer": if (!string.IsNullOrEmpty(val)) loaded.FramePacingMode = val; break;
                            case "fps_limit": if (!string.IsNullOrEmpty(val)) loaded.FpsLimit = val; break;
                            case "vsync": if (!string.IsNullOrEmpty(val)) loaded.VSync = val; break;
                            case "texture_quality": if (!string.IsNullOrEmpty(val)) loaded.TextureQuality = val; break;
                            case "shadow_quality": if (!string.IsNullOrEmpty(val)) loaded.ShadowQuality = val; break;
                            case "dynamic_shadows": if (!string.IsNullOrEmpty(val)) loaded.DynamicShadows = val; break;
                            case "view_distance": if (!string.IsNullOrEmpty(val)) loaded.ViewDistance = val; break;
                            case "mouse_sens": if (double.TryParse(val, System.Globalization.NumberStyles.Any, System.Globalization.CultureInfo.InvariantCulture, out double ms)) loaded.MouseSensitivity = ms; break;
                            case "mouse_aim_sens": if (double.TryParse(val, System.Globalization.NumberStyles.Any, System.Globalization.CultureInfo.InvariantCulture, out double mas)) loaded.MouseAimSensitivity = mas; break;
                            case "mouse_vertical": if (double.TryParse(val, System.Globalization.NumberStyles.Any, System.Globalization.CultureInfo.InvariantCulture, out double mv)) loaded.MouseVerticalSensitivity = mv; break;
                            case "mouse_invert": loaded.MouseInvertY = val == "1" || val.Equals("true", StringComparison.OrdinalIgnoreCase); break;
                            case "mouse_invert_x": loaded.MouseInvertX = val == "1" || val.Equals("true", StringComparison.OrdinalIgnoreCase); break;
                            case "stick_speed": if (double.TryParse(val, System.Globalization.NumberStyles.Any, System.Globalization.CultureInfo.InvariantCulture, out double ss)) loaded.StickSpeed = ss; break;
                            case "vibration": loaded.Vibration = val != "0" && !val.Equals("false", StringComparison.OrdinalIgnoreCase); break;
                            case "prompts": if (!string.IsNullOrEmpty(val)) loaded.Prompts = val; break;
                            case "key_map": if (!string.IsNullOrEmpty(val)) loaded.KeyMap = val; break;
                            case "pad_map": if (!string.IsNullOrEmpty(val)) loaded.PadMap = val; break;
                            case "mouse_map": if (!string.IsNullOrEmpty(val)) loaded.MouseMap = val; break;
                            case "gpu_name": if (!string.IsNullOrEmpty(val)) loaded.GpuName = val; break;
                            case "gpu_id": if (!string.IsNullOrEmpty(val)) loaded.GpuId = val; break;
                        }
                    }
                    break;
                }
            }
        }
        catch { }

        return loaded;
    }

    public void Save(string? workspaceRoot = null)
    {
        string ws = workspaceRoot ?? ResolveWorkspaceRoot();
        var paths = GetConfigPaths(ws);

        string json = JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });

        foreach (var p in paths)
        {
            try
            {
                string? dir = Path.GetDirectoryName(p);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                {
                    Directory.CreateDirectory(dir);
                }
                File.WriteAllText(p, json);
            }
            catch { }
        }

        SyncToIniFiles(ws);
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

                // Controls
                SetVal("mouse", "1");
                SetVal("mouse_sens", MouseSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetVal("mouse_aim_sens", MouseAimSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetVal("mouse_vertical", MouseVerticalSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetVal("mouse_invert", MouseInvertY ? "1" : "0");
                SetVal("mouse_invert_x", MouseInvertX ? "1" : "0");
                SetVal("stick_speed", StickSpeed.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetVal("vibration", Vibration ? "1" : "0");
                SetVal("prompts", Prompts);

                if (!string.IsNullOrEmpty(KeyMap)) SetVal("key_map", KeyMap);
                if (!string.IsNullOrEmpty(PadMap)) SetVal("pad_map", PadMap);
                if (!string.IsNullOrEmpty(MouseMap)) SetVal("mouse_map", MouseMap);

                File.WriteAllLines(iniPath, lines);
            }

            string[] enginePaths = {
                Path.Combine(workspaceRoot, "Project", "EngineConfig.ini"),
                Path.Combine(workspaceRoot, "Project", "gow3.ini"),
                Path.Combine(workspaceRoot, "EngineConfig.ini"),
                Path.Combine(workspaceRoot, "gow3.ini"),
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

                // Controls
                SetEpVal("mouse", "1");
                SetEpVal("mouse_sens", MouseSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetEpVal("mouse_aim_sens", MouseAimSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetEpVal("mouse_vertical", MouseVerticalSensitivity.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetEpVal("mouse_invert", MouseInvertY ? "1" : "0");
                SetEpVal("mouse_invert_x", MouseInvertX ? "1" : "0");
                SetEpVal("stick_speed", StickSpeed.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
                SetEpVal("vibration", Vibration ? "1" : "0");
                SetEpVal("prompts", Prompts);

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

    public static List<ActionBinding> GetDefaultBindings()
    {
        return new List<ActionBinding>
        {
            // Movimiento
            new ActionBinding { Id = "ls_up", Group = "Movimiento", TranslationKey = "ActionMoveForward", FallbackLabel = "Moverse Adelante", PrimaryKey = "W", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Izq Arriba", IsStick = true },
            new ActionBinding { Id = "ls_down", Group = "Movimiento", TranslationKey = "ActionMoveBackward", FallbackLabel = "Moverse Atrás", PrimaryKey = "S", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Izq Abajo", IsStick = true },
            new ActionBinding { Id = "ls_left", Group = "Movimiento", TranslationKey = "ActionMoveLeft", FallbackLabel = "Moverse Izquierda", PrimaryKey = "A", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Izq Izquierda", IsStick = true },
            new ActionBinding { Id = "ls_right", Group = "Movimiento", TranslationKey = "ActionMoveRight", FallbackLabel = "Moverse Derecha", PrimaryKey = "D", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Izq Derecha", IsStick = true },

            // Cámara
            new ActionBinding { Id = "rs_up", Group = "Cámara", TranslationKey = "ActionLookUp", FallbackLabel = "Mirar Arriba", PrimaryKey = "Up", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Der Arriba", IsStick = true },
            new ActionBinding { Id = "rs_down", Group = "Cámara", TranslationKey = "ActionLookDown", FallbackLabel = "Mirar Abajo", PrimaryKey = "Down", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Der Abajo", IsStick = true },
            new ActionBinding { Id = "rs_left", Group = "Cámara", TranslationKey = "ActionLookLeft", FallbackLabel = "Mirar Izquierda", PrimaryKey = "Left", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Der Izquierda", IsStick = true },
            new ActionBinding { Id = "rs_right", Group = "Cámara", TranslationKey = "ActionLookRight", FallbackLabel = "Mirar Derecha", PrimaryKey = "Right", SecondaryKeyOrMouse = "-", GamepadLabel = "Stick Der Derecha", IsStick = true },

            // Armas / D-Pad
            new ActionBinding { Id = "left", Group = "Armas", TranslationKey = "ActionWeapon1", FallbackLabel = "Arma 1 (D-Pad Izq)", PrimaryKey = "1", SecondaryKeyOrMouse = "Wheel down", GamepadLabel = "D-Pad Izq" },
            new ActionBinding { Id = "right", Group = "Armas", TranslationKey = "ActionWeapon2", FallbackLabel = "Arma 2 (D-Pad Der)", PrimaryKey = "2", SecondaryKeyOrMouse = "Wheel up", GamepadLabel = "D-Pad Der" },
            new ActionBinding { Id = "up", Group = "Armas", TranslationKey = "ActionWeapon3", FallbackLabel = "Arma 3 (D-Pad Arriba)", PrimaryKey = "3", SecondaryKeyOrMouse = "-", GamepadLabel = "D-Pad Arriba" },
            new ActionBinding { Id = "down", Group = "Armas", TranslationKey = "ActionWeapon4", FallbackLabel = "Arma 4 (D-Pad Abajo)", PrimaryKey = "4", SecondaryKeyOrMouse = "-", GamepadLabel = "D-Pad Abajo" },

            // Acciones
            new ActionBinding { Id = "cross", Group = "Acciones", TranslationKey = "ActionJump", FallbackLabel = "Saltar / Trepar", PrimaryKey = "Space", SecondaryKeyOrMouse = "-", GamepadLabel = "A / Cruz" },
            new ActionBinding { Id = "circle", Group = "Acciones", TranslationKey = "ActionCover", FallbackLabel = "Cubrirse / Rodar", PrimaryKey = "Left Ctrl", SecondaryKeyOrMouse = "-", GamepadLabel = "B / Círculo" },
            new ActionBinding { Id = "square", Group = "Acciones", TranslationKey = "ActionMelee", FallbackLabel = "Cuerpo a Cuerpo", PrimaryKey = "F", SecondaryKeyOrMouse = "-", GamepadLabel = "X / Cuadrado" },
            new ActionBinding { Id = "triangle", Group = "Acciones", TranslationKey = "ActionInteract", FallbackLabel = "Interactuar / Recoger", PrimaryKey = "R", SecondaryKeyOrMouse = "-", GamepadLabel = "Y / Triángulo" },

            // Combate
            new ActionBinding { Id = "l2", Group = "Combate", TranslationKey = "ActionAim", FallbackLabel = "Apuntar Arma", PrimaryKey = "-", SecondaryKeyOrMouse = "Right button", GamepadLabel = "LT / L2" },
            new ActionBinding { Id = "r2", Group = "Combate", TranslationKey = "ActionFire", FallbackLabel = "Disparar Arma", PrimaryKey = "-", SecondaryKeyOrMouse = "Left button", GamepadLabel = "RT / R2" },
            new ActionBinding { Id = "l1", Group = "Combate", TranslationKey = "ActionGrenade", FallbackLabel = "Lanzar Granada", PrimaryKey = "Q", SecondaryKeyOrMouse = "Back button", GamepadLabel = "LB / L1" },
            new ActionBinding { Id = "r1", Group = "Combate", TranslationKey = "ActionReload", FallbackLabel = "Recargar Arma", PrimaryKey = "E", SecondaryKeyOrMouse = "Forward button", GamepadLabel = "RB / R1" },

            // Especiales
            new ActionBinding { Id = "l3", Group = "Acciones", TranslationKey = "ActionSprint", FallbackLabel = "Esprintar (Stick Izq)", PrimaryKey = "Left Shift", SecondaryKeyOrMouse = "-", GamepadLabel = "LS / L3" },
            new ActionBinding { Id = "r3", Group = "Acciones", TranslationKey = "ActionZoom", FallbackLabel = "Zoom (Stick Der)", PrimaryKey = "V", SecondaryKeyOrMouse = "Middle button", GamepadLabel = "RS / R3" },
            new ActionBinding { Id = "touchpad_left", Group = "Sistema", TranslationKey = "ActionJournal", FallbackLabel = "Diario de Drake", PrimaryKey = "Tab", SecondaryKeyOrMouse = "-", GamepadLabel = "Touchpad / Back" },
            new ActionBinding { Id = "options", Group = "Sistema", TranslationKey = "ActionPause", FallbackLabel = "Menú de Pausa", PrimaryKey = "Escape", SecondaryKeyOrMouse = "Return", GamepadLabel = "Start / Options" },
        };
    }

    public static List<ActionBinding> ParseBindings(string keyMap, string mouseMap)
    {
        var list = GetDefaultBindings();
        var keyDict = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        var mouseDict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        if (!string.IsNullOrEmpty(keyMap))
        {
            foreach (var part in keyMap.Split(',', StringSplitOptions.RemoveEmptyEntries))
            {
                int eq = part.IndexOf('=');
                if (eq <= 0) continue;
                string id = part[..eq].Trim();
                string val = part[(eq + 1)..].Trim();
                if (!string.IsNullOrEmpty(val))
                {
                    if (!keyDict.TryGetValue(id, out var klist))
                    {
                        klist = new List<string>();
                        keyDict[id] = klist;
                    }
                    klist.Add(val);
                }
            }
        }

        if (!string.IsNullOrEmpty(mouseMap))
        {
            foreach (var part in mouseMap.Split(',', StringSplitOptions.RemoveEmptyEntries))
            {
                int eq = part.IndexOf('=');
                if (eq <= 0) continue;
                string id = part[..eq].Trim();
                string val = part[(eq + 1)..].Trim();
                if (!string.IsNullOrEmpty(val))
                {
                    string readable = val switch
                    {
                        "left" => "Left button",
                        "right" => "Right button",
                        "middle" => "Middle button",
                        "x1" => "Back button",
                        "x2" => "Forward button",
                        "wheel_up" => "Wheel up",
                        "wheel_down" => "Wheel down",
                        _ => val
                    };
                    mouseDict[id] = readable;
                }
            }
        }

        foreach (var item in list)
        {
            if (keyDict.TryGetValue(item.Id, out var keys) && keys.Count > 0)
            {
                item.PrimaryKey = keys[0];
                if (keys.Count > 1) item.SecondaryKeyOrMouse = keys[1];
            }
            if (mouseDict.TryGetValue(item.Id, out var mBtn))
            {
                if (string.IsNullOrEmpty(item.PrimaryKey) || item.PrimaryKey == "-")
                {
                    item.PrimaryKey = mBtn;
                }
                else
                {
                    item.SecondaryKeyOrMouse = mBtn;
                }
            }
        }

        return list;
    }

    public static (string keyMap, string mouseMap) SerializeBindings(IEnumerable<ActionBinding> bindings)
    {
        var keyParts = new List<string>();
        var mouseParts = new List<string>();

        foreach (var b in bindings)
        {
            string primary = b.PrimaryKey?.Trim() ?? "-";
            string secondary = b.SecondaryKeyOrMouse?.Trim() ?? "-";

            void ProcessInput(string input)
            {
                if (string.IsNullOrEmpty(input) || input == "-") return;

                string lower = input.ToLowerInvariant();
                if (lower.Contains("button") || lower.Contains("wheel") || lower.Contains("botón") || lower.Contains("rueda"))
                {
                    string mCode = lower switch
                    {
                        var s when s.Contains("left button") || s.Contains("izquierdo") => "left",
                        var s when s.Contains("right button") || s.Contains("derecho") => "right",
                        var s when s.Contains("middle button") || s.Contains("central") => "middle",
                        var s when s.Contains("back button") || s.Contains("x1") || s.Contains("lateral 1") => "x1",
                        var s when s.Contains("forward button") || s.Contains("x2") || s.Contains("lateral 2") => "x2",
                        var s when s.Contains("wheel up") || s.Contains("arriba") => "wheel_up",
                        var s when s.Contains("wheel down") || s.Contains("abajo") => "wheel_down",
                        _ => "left"
                    };
                    mouseParts.Add($"{b.Id}={mCode}");
                }
                else
                {
                    keyParts.Add($"{b.Id}={input}");
                }
            }

            ProcessInput(primary);
            ProcessInput(secondary);
        }

        return (string.Join(",", keyParts), string.Join(",", mouseParts));
    }
}
