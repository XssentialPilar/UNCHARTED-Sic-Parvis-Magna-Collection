using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace UnchartedLauncher;

public partial class MainWindow : Window
{
    private readonly LauncherConfig _config;
    private readonly GameRunner _runner;
    private readonly string _workspaceRoot;

    public MainWindow()
    {
        InitializeComponent();

        _workspaceRoot = ResolveWorkspaceRoot();
        _config = LauncherConfig.Load();
        _runner = new GameRunner();

        _runner.LogReceived += OnLogReceived;
        _runner.StatusChanged += OnStatusChanged;
        _runner.GameExited += OnGameExited;

        ApplyConfigToUI();
        UpdateFooterSummary();
    }

    private static string ResolveWorkspaceRoot()
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

    private void ApplyConfigToUI()
    {
        TxtGameDir.Text = _config.GameDir;
        SelectGameCard(_config.SelectedGame);

        SetComboByTag(ComboApi, _config.GraphicsApi);
        SetComboByTag(ComboVramCap, _config.VramBudgetMb);
        SetComboByTag(ComboTexQuality, _config.TextureQuality);
        SetComboByTag(ComboShadowQuality, _config.ShadowQuality);
        SetComboByTag(ComboSsao, _config.SsaoQuality);
        SetComboByTag(ComboAniso, _config.Anisotropy);
        SetComboByTag(ComboResolution, _config.Resolution);
        SetComboByTag(ComboUpscaler, _config.Upscaler);
        SetComboByTag(ComboFpsLimit, _config.FpsLimit);
        SetComboByTag(ComboFramePacer, _config.FramePacingMode);

        ChkFullResTex.IsChecked = _config.FullResTextures;
        ChkHiresShadows.IsChecked = _config.HiresShadows;
        ChkVsync.IsChecked = _config.VSync == "1";
        ChkMotionBlur.IsChecked = _config.MotionBlur;
        ChkDepthOfField.IsChecked = _config.DepthOfField;

        SliderMouseSens.Value = _config.MouseSensitivity;
        TxtMouseSensVal.Text = $"{_config.MouseSensitivity:F1}x";
    }

    private void SaveUIToConfig()
    {
        _config.GameDir = TxtGameDir.Text.Trim();
        _config.GraphicsApi = GetComboTag(ComboApi, "dx11");
        _config.VramBudgetMb = GetComboTag(ComboVramCap, "1720");
        _config.TextureQuality = GetComboTag(ComboTexQuality, "medium");
        _config.ShadowQuality = GetComboTag(ComboShadowQuality, "medium");
        _config.SsaoQuality = GetComboTag(ComboSsao, "full");
        _config.Anisotropy = GetComboTag(ComboAniso, "16");
        _config.Resolution = GetComboTag(ComboResolution, "1920x1080");
        _config.Upscaler = GetComboTag(ComboUpscaler, "off");
        _config.FpsLimit = GetComboTag(ComboFpsLimit, "60");
        _config.FramePacingMode = GetComboTag(ComboFramePacer, "hybrid");

        _config.FullResTextures = ChkFullResTex.IsChecked == true;
        _config.HiresShadows = ChkHiresShadows.IsChecked == true;
        _config.VSync = ChkVsync.IsChecked == true ? "1" : "0";
        _config.MotionBlur = ChkMotionBlur.IsChecked == true;
        _config.DepthOfField = ChkDepthOfField.IsChecked == true;
        _config.MouseSensitivity = SliderMouseSens.Value;

        _config.Save();
        _config.SyncToIniFiles(_workspaceRoot);
        UpdateFooterSummary();
    }

    private void UpdateFooterSummary()
    {
        string gameName = _config.SelectedGame switch
        {
            "u1" => "Uncharted: Drake's Fortune",
            "u2" => "Uncharted 2: Among Thieves",
            "u3" => "Uncharted 3: Drake's Deception",
            _ => "Uncharted"
        };
        TxtReadyStatus.Text = $"Listo: {gameName}";
        string capText = _config.VramBudgetMb == "0" ? "Sin Cap" : $"{_config.VramBudgetMb} MB Cap";
        TxtFooterDetails.Text = $"{_config.GraphicsApi.ToUpper()} • {capText} • {_config.Resolution} • {_config.FpsLimit} FPS • {_config.FramePacingMode} Pacer";
        BtnPlay.Content = $"► JUGAR {(_config.SelectedGame switch { "u1" => "PARTE I", "u2" => "PARTE II", "u3" => "PARTE III", _ => "" })}";
    }

    private static void SetComboByTag(ComboBox combo, string tag)
    {
        foreach (ComboBoxItem item in combo.Items)
        {
            if (string.Equals(item.Tag as string, tag, StringComparison.OrdinalIgnoreCase))
            {
                combo.SelectedItem = item;
                return;
            }
        }
    }

    private static string GetComboTag(ComboBox combo, string fallback)
    {
        if (combo.SelectedItem is ComboBoxItem item && item.Tag is string s)
        {
            return s;
        }
        return fallback;
    }

    // --- Tab Navigation ---
    private void ShowTab(UIElement tabToShow, Button activeBtn)
    {
        TabExpeditions.Visibility = Visibility.Collapsed;
        TabGraphics.Visibility = Visibility.Collapsed;
        TabDisplay.Visibility = Visibility.Collapsed;
        TabInput.Visibility = Visibility.Collapsed;
        TabLogs.Visibility = Visibility.Collapsed;

        tabToShow.Visibility = Visibility.Visible;

        BtnNavExpeditions.Foreground = new SolidColorBrush(Color.FromRgb(194, 178, 148));
        BtnNavGraphics.Foreground = new SolidColorBrush(Color.FromRgb(194, 178, 148));
        BtnNavDisplay.Foreground = new SolidColorBrush(Color.FromRgb(194, 178, 148));
        BtnNavInput.Foreground = new SolidColorBrush(Color.FromRgb(194, 178, 148));
        BtnNavLogs.Foreground = new SolidColorBrush(Color.FromRgb(194, 178, 148));

        activeBtn.Foreground = new SolidColorBrush(Color.FromRgb(229, 193, 88));
    }

    private void OnNavExpeditions(object sender, RoutedEventArgs e) => ShowTab(TabExpeditions, BtnNavExpeditions);
    private void OnNavGraphics(object sender, RoutedEventArgs e) => ShowTab(TabGraphics, BtnNavGraphics);
    private void OnNavDisplay(object sender, RoutedEventArgs e) => ShowTab(TabDisplay, BtnNavDisplay);
    private void OnNavInput(object sender, RoutedEventArgs e) => ShowTab(TabInput, BtnNavInput);
    private void OnNavLogs(object sender, RoutedEventArgs e) => ShowTab(TabLogs, BtnNavLogs);

    private void OnQuit(object sender, RoutedEventArgs e)
    {
        _runner.Stop();
        Close();
    }

    // --- Journal Selection ---
    private void SelectGameCard(string gameKey)
    {
        _config.SelectedGame = gameKey;

        CardU1.BorderBrush = new SolidColorBrush(gameKey == "u1" ? Color.FromRgb(229, 193, 88) : Color.FromRgb(51, 68, 32));
        CardU1.BorderThickness = new Thickness(gameKey == "u1" ? 2 : 1);

        CardU2.BorderBrush = new SolidColorBrush(gameKey == "u2" ? Color.FromRgb(229, 193, 88) : Color.FromRgb(51, 68, 32));
        CardU2.BorderThickness = new Thickness(gameKey == "u2" ? 2 : 1);

        CardU3.BorderBrush = new SolidColorBrush(gameKey == "u3" ? Color.FromRgb(229, 193, 88) : Color.FromRgb(51, 68, 32));
        CardU3.BorderThickness = new Thickness(gameKey == "u3" ? 2 : 1);

        UpdateFooterSummary();
        _config.Save();
    }

    private void OnSelectU1(object sender, MouseButtonEventArgs e) => SelectGameCard("u1");
    private void OnSelectU2(object sender, MouseButtonEventArgs e) => SelectGameCard("u2");
    private void OnSelectU3(object sender, MouseButtonEventArgs e) => SelectGameCard("u3");

    // --- Presets ---
    private void OnPreset_iGPU(object sender, RoutedEventArgs e)
    {
        SetComboByTag(ComboApi, "dx11");
        SetComboByTag(ComboVramCap, "820");
        SetComboByTag(ComboTexQuality, "low");
        SetComboByTag(ComboShadowQuality, "low");
        SetComboByTag(ComboSsao, "off");
        SetComboByTag(ComboAniso, "2");
        SetComboByTag(ComboResolution, "1280x720");
        SetComboByTag(ComboFpsLimit, "60");
        ChkFullResTex.IsChecked = false;
        ChkHiresShadows.IsChecked = false;
        SaveUIToConfig();
    }

    private void OnPreset_Low(object sender, RoutedEventArgs e)
    {
        SetComboByTag(ComboApi, "dx11");
        SetComboByTag(ComboVramCap, "820");
        SetComboByTag(ComboTexQuality, "low");
        SetComboByTag(ComboShadowQuality, "low");
        SetComboByTag(ComboSsao, "half");
        SetComboByTag(ComboAniso, "4");
        SetComboByTag(ComboResolution, "1280x720");
        SetComboByTag(ComboFpsLimit, "60");
        ChkFullResTex.IsChecked = false;
        ChkHiresShadows.IsChecked = false;
        SaveUIToConfig();
    }

    private void OnPreset_Med(object sender, RoutedEventArgs e)
    {
        SetComboByTag(ComboApi, "dx11");
        SetComboByTag(ComboVramCap, "1720");
        SetComboByTag(ComboTexQuality, "medium");
        SetComboByTag(ComboShadowQuality, "medium");
        SetComboByTag(ComboSsao, "full");
        SetComboByTag(ComboAniso, "16");
        SetComboByTag(ComboResolution, "1920x1080");
        SetComboByTag(ComboFpsLimit, "60");
        ChkFullResTex.IsChecked = false;
        ChkHiresShadows.IsChecked = false;
        SaveUIToConfig();
    }

    private void OnPreset_High(object sender, RoutedEventArgs e)
    {
        SetComboByTag(ComboApi, "dx11");
        SetComboByTag(ComboVramCap, "3200");
        SetComboByTag(ComboTexQuality, "high");
        SetComboByTag(ComboShadowQuality, "high");
        SetComboByTag(ComboSsao, "full");
        SetComboByTag(ComboAniso, "16");
        SetComboByTag(ComboResolution, "1920x1080");
        SetComboByTag(ComboFpsLimit, "60");
        ChkFullResTex.IsChecked = true;
        ChkHiresShadows.IsChecked = true;
        SaveUIToConfig();
    }

    private void OnPreset_Ultra(object sender, RoutedEventArgs e)
    {
        SetComboByTag(ComboApi, "dx11");
        SetComboByTag(ComboVramCap, "6144");
        SetComboByTag(ComboTexQuality, "ultra");
        SetComboByTag(ComboShadowQuality, "ultra");
        SetComboByTag(ComboSsao, "full");
        SetComboByTag(ComboAniso, "16");
        SetComboByTag(ComboResolution, "2560x1440");
        SetComboByTag(ComboFpsLimit, "120");
        ChkFullResTex.IsChecked = true;
        ChkHiresShadows.IsChecked = true;
        SaveUIToConfig();
    }

    private void OnSettingChanged(object sender, RoutedEventArgs e)
    {
        if (!IsLoaded) return;
        SaveUIToConfig();
    }

    private void OnGameDirChanged(object sender, TextChangedEventArgs e)
    {
        if (!IsLoaded) return;
        _config.GameDir = TxtGameDir.Text.Trim();
        _config.Save();
    }

    private void OnBrowseGameDir(object sender, RoutedEventArgs e)
    {
        var dialog = new Microsoft.Win32.OpenFolderDialog
        {
            Title = "Seleccionar Carpeta del Juego Uncharted (CUSA02344)",
            InitialDirectory = Directory.Exists(_config.GameDir) ? _config.GameDir : "C:\\"
        };
        if (dialog.ShowDialog(this) == true)
        {
            TxtGameDir.Text = dialog.FolderName;
            _config.GameDir = dialog.FolderName;
            _config.Save();
        }
    }

    private void OnMouseSensChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (TxtMouseSensVal != null)
        {
            TxtMouseSensVal.Text = $"{e.NewValue:F1}x";
            _config.MouseSensitivity = e.NewValue;
            _config.Save();
        }
    }

    // --- Game Launch & Process Controls ---
    private async void OnPlayClicked(object sender, RoutedEventArgs e)
    {
        if (_runner.IsRunning) return;

        BtnPlay.IsEnabled = false;
        BtnPlay.Content = "INICIANDO...";
        BtnStop.Visibility = Visibility.Visible;

        TxtConsole.AppendText($"\n[{DateTime.Now:HH:mm:ss}] Iniciando {_config.SelectedGame.ToUpper()} con API={_config.GraphicsApi.ToUpper()} (VRAM Cap={_config.VramBudgetMb} MB)...\n");

        bool started = await _runner.LaunchAsync(_config, _workspaceRoot, _config.SelectedGame);
        if (!started)
        {
            BtnPlay.IsEnabled = true;
            BtnPlay.Content = $"► JUGAR {(_config.SelectedGame switch { "u1" => "PARTE I", "u2" => "PARTE II", "u3" => "PARTE III", _ => "" })}";
            BtnStop.Visibility = Visibility.Collapsed;
        }
    }

    private void OnStopClicked(object sender, RoutedEventArgs e)
    {
        _runner.Stop();
    }

    private void OnLogReceived(string line)
    {
        Dispatcher.InvokeAsync(() =>
        {
            TxtConsole.AppendText(line + "\n");
            TxtConsole.ScrollToEnd();
        });
    }

    private void OnStatusChanged(string status)
    {
        Dispatcher.InvokeAsync(() =>
        {
            TxtReadyStatus.Text = status;
        });
    }

    private void OnGameExited(int exitCode)
    {
        Dispatcher.InvokeAsync(() =>
        {
            BtnPlay.IsEnabled = true;
            UpdateFooterSummary();
            BtnStop.Visibility = Visibility.Collapsed;
        });
    }

    private void OnClearConsole(object sender, RoutedEventArgs e)
    {
        TxtConsole.Clear();
    }

    private async void OnVerifyFiles(object sender, RoutedEventArgs e)
    {
        string toolScript = Path.Combine(_workspaceRoot, "Project", "tools", "check_game_files.py");
        if (!File.Exists(toolScript)) return;

        TxtConsole.AppendText("\n[Verificación] Comprobando integridad SHA256 de archivos del juego...\n");
        await Task.Run(() =>
        {
            try
            {
                var psi = new ProcessStartInfo
                {
                    FileName = "python.exe",
                    Arguments = $"-I \"{toolScript}\" check \"{_config.GameDir}\"",
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                using var proc = Process.Start(psi);
                if (proc != null)
                {
                    string output = proc.StandardOutput.ReadToEnd();
                    string err = proc.StandardError.ReadToEnd();
                    proc.WaitForExit();
                    Dispatcher.InvokeAsync(() =>
                    {
                        TxtConsole.AppendText(output + "\n");
                        if (!string.IsNullOrEmpty(err)) TxtConsole.AppendText(err + "\n");
                    });
                }
            }
            catch (Exception ex)
            {
                Dispatcher.InvokeAsync(() => TxtConsole.AppendText($"[Error de verificación]: {ex.Message}\n"));
            }
        });
    }
}
