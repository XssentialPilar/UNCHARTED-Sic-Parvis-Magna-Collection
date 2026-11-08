using System;

using System.Collections.Generic;

using System.Diagnostics;

using System.IO;

using System.Threading.Tasks;

using System.Windows;

using System.Windows.Controls;

using System.Windows.Input;

using System.Windows.Media;

using System.Windows.Media.Animation;



namespace UnchartedLauncher;



public partial class MainWindow : Window

{

    private readonly LauncherConfig _config;

    private readonly GameRunner _runner;

    private readonly string _workspaceRoot;

    private List<ActionBinding> _bindings = new();



    private ActionBinding? _capturingBinding;

    private int _capturingSlot; // 1 = Primary, 2 = Secondary

    private Button? _activeCaptureButton;



    public MainWindow()

    {

        InitializeComponent();



        _workspaceRoot = LauncherConfig.ResolveWorkspaceRoot();

        _config = LauncherConfig.Load(_workspaceRoot);

        _runner = new GameRunner();



        _runner.LogReceived += OnLogReceived;

        _runner.StatusChanged += OnStatusChanged;

        _runner.GameExited += OnGameExited;



        LocalizationManager.Initialize(_config.Language);



        Closing += (s, e) => SaveUIToConfig();

        PreviewKeyDown += OnWindowPreviewKeyDown;

        PreviewMouseDown += OnWindowPreviewMouseDown;

        PreviewMouseWheel += OnWindowPreviewMouseWheel;



        ApplyConfigToUI();

        ApplyLocalization();

        UpdateFooterSummary();

        CheckForUpdatesAsync();

    }



    private void ApplyConfigToUI()

    {

        TxtGameDir.Text = _config.GameDir;

        SelectGameCard(_config.SelectedGame);



        SetComboByTag(ComboLanguage, _config.Language);

        SetComboByTag(ComboApi, _config.GraphicsApi);

        SetComboByTag(ComboVramCap, _config.VramBudgetMb);

        SetComboByTag(ComboTexQuality, _config.TextureQuality);

        SetComboByTag(ComboShadowQuality, _config.ShadowQuality);

        SetComboByTag(ComboDynamicShadows, _config.DynamicShadows);

        SetComboByTag(ComboViewDistance, _config.ViewDistance);

        SetComboByTag(ComboSsao, _config.SsaoQuality);

        SetComboByTag(ComboAniso, _config.Anisotropy);

        SetComboByTag(ComboDisplayMode, _config.DisplayMode);

        SetComboByTag(ComboResolution, _config.Resolution);

        SetComboByTag(ComboUpscaler, _config.Upscaler);

        SetComboByTag(ComboFpsLimit, _config.FpsLimit);

        SetComboByTag(ComboFramePacer, _config.FramePacingMode);

        SetComboByTag(ComboPrompts, _config.Prompts);



        PopulateGpuCombo();



        ChkFullResTex.IsChecked = _config.FullResTextures;

        ChkHiresShadows.IsChecked = _config.HiresShadows;

        ChkVsync.IsChecked = _config.VSync == "1";

        ChkMotionBlur.IsChecked = _config.MotionBlur;

        ChkDepthOfField.IsChecked = _config.DepthOfField;



        // Controls & Sensitivities

        SliderMouseSens.Value = _config.MouseSensitivity;

        TxtMouseSensVal.Text = $"{_config.MouseSensitivity:F1}x";



        SliderMouseAimSens.Value = _config.MouseAimSensitivity;

        TxtMouseAimSensVal.Text = $"{_config.MouseAimSensitivity:F1}x";



        SliderMouseVert.Value = _config.MouseVerticalSensitivity;

        TxtMouseVertVal.Text = $"{_config.MouseVerticalSensitivity:F1}x";



        ChkInvertY.IsChecked = _config.MouseInvertY;

        ChkInvertX.IsChecked = _config.MouseInvertX;



        SliderStickSpeed.Value = _config.StickSpeed;

        TxtStickSpeedVal.Text = $"{_config.StickSpeed:F1}x";



        ChkVibration.IsChecked = _config.Vibration;



        // Parse keybindings

        _bindings = LauncherConfig.ParseBindings(_config.KeyMap, _config.MouseMap);

        RefreshBindingsList();

    }



    private void RefreshBindingsList()

    {

        foreach (var b in _bindings)

        {

            if (!string.IsNullOrEmpty(b.TranslationKey))

            {

                b.FallbackLabel = LocalizationManager.Get(b.TranslationKey, b.FallbackLabel);

            }

        }

        ItemsBindings.ItemsSource = null;

        ItemsBindings.ItemsSource = _bindings;

    }



    public void ApplyLocalization()

    {

        Title = LocalizationManager.Get("AppTitle", "UNCHARTED: Sic Parvis Magna Collection");

        TxtLangLabel.Text = LocalizationManager.Get("LanguageLabel", "Idioma:");

        TxtPresetsHeader.Text = LocalizationManager.Get("PresetsLabel", "PREAJUSTES:");

        BtnPresetIgpu.Content = LocalizationManager.Get("PresetIgpu", "iGPU (820MB)");

        BtnPresetLow.Content = LocalizationManager.Get("PresetLow", "BAJO 1GB");

        BtnPresetMed.Content = LocalizationManager.Get("PresetMed", "MEDIO 2GB ★");

        BtnPresetHigh.Content = LocalizationManager.Get("PresetHigh", "ALTO");

        BtnPresetUltra.Content = LocalizationManager.Get("PresetUltra", "ULTRA");



        BtnNavExpeditions.Content = LocalizationManager.Get("NavExpeditions", "EXPEDICIONES");

        BtnNavGraphics.Content = LocalizationManager.Get("NavGraphics", "GRÁFICOS Y VRAM");

        BtnNavDisplay.Content = LocalizationManager.Get("NavDisplay", "PANTALLA Y ESCALADO");

        BtnNavInput.Content = LocalizationManager.Get("NavControls", "CONTROLES Y JUEGO");

        BtnNavLogs.Content = LocalizationManager.Get("NavTelemetry", "TELEMETRÍA Y LOGS");

        BtnNavQuit.Content = LocalizationManager.Get("NavQuit", "SALIR");



        TxtSelectExpedition.Text = LocalizationManager.Get("SelectExpedition", "SELECCIONA TU EXPEDICIÓN");

        TxtExpeditionGpuLabel.Text = LocalizationManager.Get("ActiveGpu", "GPU Activa:");

        TxtPart1Title.Text = LocalizationManager.Get("Part1Title", "PARTE I");

        TxtPart1Sub.Text = LocalizationManager.Get("Part1Sub", "DRAKE'S FORTUNE");

        TxtPart2Title.Text = LocalizationManager.Get("Part2Title", "PARTE II");

        TxtPart2Sub.Text = LocalizationManager.Get("Part2Sub", "AMONG THIEVES");

        TxtPart3Title.Text = LocalizationManager.Get("Part3Title", "PARTE III");

        TxtPart3Sub.Text = LocalizationManager.Get("Part3Sub", "DRAKE'S DECEPTION");



        TxtGraphicsHeader.Text = LocalizationManager.Get("GraphicsHeader", "CONFIGURACIÓN DE GRÁFICOS Y GESTIÓN DE VRAM");

        TxtGpuActiveLabel.Text = LocalizationManager.Get("GpuActiveLabel", "Tarjeta Gráfica Activa (GPU):");

        TxtGpuActiveSub.Text = LocalizationManager.Get("GpuActiveSub", "Selecciona GPU dedicada (RTX) o integrada (CPU)");

        TxtApiLabel.Text = LocalizationManager.Get("ApiLabel", "API Gráfica de Presentación:");

        TxtApiSub.Text = LocalizationManager.Get("ApiSub", "DirectX 11 ofrece Flip-Discard; D3D9Ex para hardware modesto");

        TxtVramCapLabel.Text = LocalizationManager.Get("VramCapLabel", "Límite Estricto de VRAM (Cap):");

        TxtVramCapSub.Text = LocalizationManager.Get("VramCapSub", "Evita saturación de bus y caídas de FPS");

        TxtTexQualityLabel.Text = LocalizationManager.Get("TexQualityLabel", "Calidad de Texturas:");

        TxtTexQualitySub.Text = LocalizationManager.Get("TexQualitySub", "Compresión GPU y streaming adaptativo");

        TxtShadowQualityLabel.Text = LocalizationManager.Get("ShadowQualityLabel", "Calidad de Sombras:");

        TxtShadowQualitySub.Text = LocalizationManager.Get("ShadowQualitySub", "Resolución de cascadas y filtrado solar");

        TxtDynamicShadowsLabel.Text = LocalizationManager.Get("DynamicShadowsLabel", "Sombras Dinámicas:");

        TxtDynamicShadowsSub.Text = LocalizationManager.Get("DynamicShadowsSub", "Desactivar elimina pases de sombra (+FPS masivo)");

        TxtViewDistanceLabel.Text = LocalizationManager.Get("ViewDistanceLabel", "Distancia de Renderizado (LOD):");

        TxtViewDistanceSub.Text = LocalizationManager.Get("ViewDistanceSub", "Escala de detalle con anti-popping exponencial");

        TxtSsaoLabel.Text = LocalizationManager.Get("SsaoLabel", "Oclusión Ambiental (SSAO):");

        TxtSsaoSub.Text = LocalizationManager.Get("SsaoSub", "Sombreado de esquinas y profundidad");

        TxtAnisoLabel.Text = LocalizationManager.Get("AnisoLabel", "Filtrado Anisótropo:");

        TxtAnisoSub.Text = LocalizationManager.Get("AnisoSub", "Nitidez en texturas con ángulo de cámara");

        ChkFullResTex.Content = LocalizationManager.Get("ChkFullResTex", "Permitir Texturas a Resolución Completa (requiere >2GB VRAM)");

        ChkHiresShadows.Content = LocalizationManager.Get("ChkHiresShadows", "Sombras en Alta Resolución (requiere GPU dedicada)");



        TxtDisplayHeader.Text = LocalizationManager.Get("DisplayHeader", "RESOLUCIÓN, ESCALADO Y SINCRONIZACIÓN");

        TxtResLabel.Text = LocalizationManager.Get("ResLabel", "Resolución de Renderizado:");

        TxtResSub.Text = LocalizationManager.Get("ResSub", "Tamaño del búfer interno del motor");

        TxtUpscalerLabel.Text = LocalizationManager.Get("UpscalerLabel", "Tecnología de Escalado (Upscaler):");

        TxtUpscalerSub.Text = LocalizationManager.Get("UpscalerSub", "Fidelidad visual con ahorro de cálculo");

        TxtFpsLimitLabel.Text = LocalizationManager.Get("FpsLimitLabel", "Límite de Fotogramas (FPS):");

        TxtFpsLimitSub.Text = LocalizationManager.Get("FpsLimitSub", "Tope de tasa de refresco");

        TxtFramePacerLabel.Text = LocalizationManager.Get("FramePacerLabel", "Temporizador / Frame Pacer:");

        TxtFramePacerSub.Text = LocalizationManager.Get("FramePacerSub", "Estabilidad y precisión submilisegundo");

        ChkVsync.Content = LocalizationManager.Get("ChkVsync", "Sincronización Vertical (V-Sync activado)");

        ChkMotionBlur.Content = LocalizationManager.Get("ChkMotionBlur", "Desenfoque de Movimiento (Motion Blur)");

        ChkDepthOfField.Content = LocalizationManager.Get("ChkDepthOfField", "Profundidad de Campo (Depth of Field cinematográfico)");



        TxtControlsHeader.Text = LocalizationManager.Get("ControlsHeader", "MAPEO EXTENSO DE CONTROLES, RATÓN Y MANDO");

        TxtGameDirLabel.Text = LocalizationManager.Get("GameDirLabel", "Directorio de Archivos del Juego (CUSA02344):");

        BtnBrowse.Content = LocalizationManager.Get("BtnBrowse", "BUSCAR...");

        TxtMouseSectionHeader.Text = LocalizationManager.Get("MouseSectionHeader", "RATÓN Y PRECISIÓN DE APUNTADO");

        TxtMouseSensLabel.Text = LocalizationManager.Get("MouseSensLabel", "Sensibilidad General:");

        TxtMouseAimSensLabel.Text = LocalizationManager.Get("MouseAimSensLabel", "Sensibilidad Apuntado:");

        TxtMouseVertLabel.Text = LocalizationManager.Get("MouseVertLabel", "Sensibilidad Vertical:");

        ChkInvertY.Content = LocalizationManager.Get("ChkInvertY", "Invertir Eje Y (Arriba/Abajo)");

        ChkInvertX.Content = LocalizationManager.Get("ChkInvertX", "Invertir Eje X");

        TxtGamepadSectionHeader.Text = LocalizationManager.Get("GamepadSectionHeader", "MANDO, HÁPTICA Y GLIFOS");

        TxtStickSpeedLabel.Text = LocalizationManager.Get("StickSpeedLabel", "Velocidad Palancas:");

        TxtPromptsLabel.Text = LocalizationManager.Get("PromptsLabel", "Iconos de Botones:");

        ChkVibration.Content = LocalizationManager.Get("ChkVibration", "Vibración de Mando (Rumble Háptico)");



        TxtKeybindsHeader.Text = LocalizationManager.Get("KeybindsHeader", "ASIGNACIÓN LIBRE DE TECLAS Y RATÓN");

        TxtKeybindsSub.Text = LocalizationManager.Get("KeybindsSub", "Haz clic en cualquier tecla para cambiar su asignación libremente sin restricciones.");

        BtnResetControls.Content = LocalizationManager.Get("BtnResetControls", "RESTABLECER CONTROLES PREDETERMINADOS");

        BtnSaveControls.Content = LocalizationManager.Get("BtnSaveControls", "GUARDAR CONTROLES");



        TxtTelemetryHeader.Text = LocalizationManager.Get("TelemetryHeader", "REGISTRO Y TELEMETRÍA EN TIEMPO REAL");

        BtnVerifyFiles.Content = LocalizationManager.Get("BtnVerify", "VERIFICAR ARCHIVOS (1.02)");

        BtnClearConsole.Content = LocalizationManager.Get("BtnClearConsole", "LIMPIAR CONSOLA");



        BtnStop.Content = LocalizationManager.Get("BtnStop", "DETENER");



        RefreshBindingsList();

        UpdateFooterSummary();

    }



    private void OnLanguageChanged(object sender, SelectionChangedEventArgs e)

    {

        if (!IsLoaded) return;

        string lang = GetComboTag(ComboLanguage, "auto");

        _config.Language = lang;

        LocalizationManager.SetLanguage(lang);

        ApplyLocalization();

        SaveUIToConfig();

    }



    private void SaveUIToConfig()

    {

        _config.Language = GetComboTag(ComboLanguage, "auto");

        _config.GameDir = TxtGameDir.Text.Trim();

        _config.GraphicsApi = GetComboTag(ComboApi, "dx11");

        _config.VramBudgetMb = GetComboTag(ComboVramCap, "1720");

        _config.TextureQuality = GetComboTag(ComboTexQuality, "medium");

        _config.ShadowQuality = GetComboTag(ComboShadowQuality, "medium");

        _config.DynamicShadows = GetComboTag(ComboDynamicShadows, "1");

        _config.ViewDistance = GetComboTag(ComboViewDistance, "medium");

        _config.SsaoQuality = GetComboTag(ComboSsao, "full");

        _config.Anisotropy = GetComboTag(ComboAniso, "16");

        _config.DisplayMode = GetComboTag(ComboDisplayMode, "fullscreen");

        _config.Resolution = GetComboTag(ComboResolution, "1920x1080");

        _config.Upscaler = GetComboTag(ComboUpscaler, "off");

        _config.FpsLimit = GetComboTag(ComboFpsLimit, "60");

        _config.FramePacingMode = GetComboTag(ComboFramePacer, "hybrid");

        _config.Prompts = GetComboTag(ComboPrompts, "auto");



        if (ComboGpu.SelectedItem is ComboBoxItem gpuItem && gpuItem.Tag is string gpuTag)

        {

            var parts = gpuTag.Split('|');

            if (parts.Length >= 2)

            {

                _config.GpuId = parts[0];

                _config.GpuName = parts[1];

            }

        }



        _config.FullResTextures = ChkFullResTex.IsChecked == true;

        _config.HiresShadows = ChkHiresShadows.IsChecked == true;

        _config.VSync = ChkVsync.IsChecked == true ? "1" : "0";

        _config.MotionBlur = ChkMotionBlur.IsChecked == true;

        _config.DepthOfField = ChkDepthOfField.IsChecked == true;



        _config.MouseSensitivity = SliderMouseSens.Value;

        _config.MouseAimSensitivity = SliderMouseAimSens.Value;

        _config.MouseVerticalSensitivity = SliderMouseVert.Value;

        _config.MouseInvertY = ChkInvertY.IsChecked == true;

        _config.MouseInvertX = ChkInvertX.IsChecked == true;

        _config.StickSpeed = SliderStickSpeed.Value;

        _config.Vibration = ChkVibration.IsChecked == true;



        if (_bindings.Count > 0)

        {

            var (km, mm) = LauncherConfig.SerializeBindings(_bindings);

            _config.KeyMap = km;

            _config.MouseMap = mm;

        }



        _config.Save(_workspaceRoot);

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

        string readyFmt = LocalizationManager.Get("ReadyStatus", "Listo: {0}");

        TxtReadyStatus.Text = string.Format(readyFmt, gameName);



        string capText = _config.VramBudgetMb == "0" ? "Sin Cap" : $"{_config.VramBudgetMb} MB Cap";

        string gpuShort = !string.IsNullOrEmpty(_config.GpuName) ? (_config.GpuName.Contains("760M") ? "AMD 760M (CPU)" : "RTX 5050") : "Auto GPU";

        TxtFooterDetails.Text = $"{gpuShort} • {_config.GraphicsApi.ToUpper()} • {capText} • {_config.Resolution} • {_config.FpsLimit} FPS • {_config.FramePacingMode} Pacer";



        string btnPlayBase = LocalizationManager.Get("BtnPlay", "► JUGAR");

        BtnPlay.Content = $"{btnPlayBase} {(_config.SelectedGame switch { "u1" => "PARTE I", "u2" => "PARTE II", "u3" => "PARTE III", _ => "" })}";

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



    // --- Tab Navigation with Smooth 180ms Animations ---

    private void ShowTab(UIElement tabToShow, Button activeBtn)

    {

        TabExpeditions.Visibility = Visibility.Collapsed;

        TabGraphics.Visibility = Visibility.Collapsed;

        TabDisplay.Visibility = Visibility.Collapsed;

        TabInput.Visibility = Visibility.Collapsed;

        TabLogs.Visibility = Visibility.Collapsed;



        tabToShow.Visibility = Visibility.Visible;



        // Animate smooth fade & subtle glide

        var animOpacity = new DoubleAnimation(0.0, 1.0, TimeSpan.FromMilliseconds(180))

        {

            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }

        };

        var animY = new DoubleAnimation(7.0, 0.0, TimeSpan.FromMilliseconds(180))

        {

            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }

        };



        TabContainerHost.BeginAnimation(UIElement.OpacityProperty, animOpacity);

        TabTranslateTransform.BeginAnimation(TranslateTransform.YProperty, animY);



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

        SaveUIToConfig();

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

        if (IsLoaded)

        {

            SaveUIToConfig();

        }

    }



    private void OnSelectU1(object sender, MouseButtonEventArgs e) => SelectGameCard("u1");

    private void OnSelectU2(object sender, MouseButtonEventArgs e) => SelectGameCard("u2");

    private void OnSelectU3(object sender, MouseButtonEventArgs e) => SelectGameCard("u3");



    // --- Quick Presets ---

    private void OnPreset_iGPU(object sender, RoutedEventArgs e)

    {

        SetComboByTag(ComboApi, "vulkan");

        SetComboByTag(ComboVramCap, "820");

        SetComboByTag(ComboTexQuality, "low");

        SetComboByTag(ComboShadowQuality, "low");

        SetComboByTag(ComboDynamicShadows, "0");

        SetComboByTag(ComboViewDistance, "low");

        SetComboByTag(ComboSsao, "off");

        SetComboByTag(ComboAniso, "4");

        SetComboByTag(ComboResolution, "1280x720");

        SetComboByTag(ComboUpscaler, "fsr3");

        SetComboByTag(ComboFpsLimit, "60");

        SetComboByTag(ComboFramePacer, "hybrid");

        ChkFullResTex.IsChecked = false;

        ChkHiresShadows.IsChecked = false;

        SaveUIToConfig();

    }



    private void OnPreset_Low(object sender, RoutedEventArgs e)

    {

        SetComboByTag(ComboApi, "dx11");

        SetComboByTag(ComboVramCap, "1720");

        SetComboByTag(ComboTexQuality, "low");

        SetComboByTag(ComboShadowQuality, "low");

        SetComboByTag(ComboDynamicShadows, "1");

        SetComboByTag(ComboViewDistance, "low");

        SetComboByTag(ComboSsao, "half");

        SetComboByTag(ComboAniso, "8");

        SetComboByTag(ComboResolution, "1280x720");

        SetComboByTag(ComboUpscaler, "off");

        SetComboByTag(ComboFpsLimit, "60");

        SetComboByTag(ComboFramePacer, "hybrid");

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

        SetComboByTag(ComboDynamicShadows, "1");

        SetComboByTag(ComboViewDistance, "medium");

        SetComboByTag(ComboSsao, "full");

        SetComboByTag(ComboAniso, "16");

        SetComboByTag(ComboResolution, "1920x1080");

        SetComboByTag(ComboUpscaler, "off");

        SetComboByTag(ComboFpsLimit, "60");

        SetComboByTag(ComboFramePacer, "hybrid");

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

        SetComboByTag(ComboDynamicShadows, "1");

        SetComboByTag(ComboViewDistance, "high");

        SetComboByTag(ComboSsao, "full");

        SetComboByTag(ComboAniso, "16");

        SetComboByTag(ComboResolution, "1920x1080");

        SetComboByTag(ComboUpscaler, "off");

        SetComboByTag(ComboFpsLimit, "60");

        SetComboByTag(ComboFramePacer, "hybrid");

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

        SetComboByTag(ComboDynamicShadows, "1");

        SetComboByTag(ComboViewDistance, "ultra");

        SetComboByTag(ComboSsao, "full");

        SetComboByTag(ComboAniso, "16");

        SetComboByTag(ComboResolution, "2560x1440");

        SetComboByTag(ComboUpscaler, "off");

        SetComboByTag(ComboFpsLimit, "120");

        SetComboByTag(ComboFramePacer, "hybrid");

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

        _config.Save(_workspaceRoot);

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

            _config.Save(_workspaceRoot);

        }

    }



    private void OnMouseSensChanged(object sender, RoutedPropertyChangedEventArgs<double> e)

    {

        if (TxtMouseSensVal != null)

        {

            TxtMouseSensVal.Text = $"{e.NewValue:F1}x";

            _config.MouseSensitivity = e.NewValue;

            if (IsLoaded) SaveUIToConfig();

        }

    }



    private void OnMouseAimSensChanged(object sender, RoutedPropertyChangedEventArgs<double> e)

    {

        if (TxtMouseAimSensVal != null)

        {

            TxtMouseAimSensVal.Text = $"{e.NewValue:F1}x";

            _config.MouseAimSensitivity = e.NewValue;

            if (IsLoaded) SaveUIToConfig();

        }

    }



    private void OnMouseVertChanged(object sender, RoutedPropertyChangedEventArgs<double> e)

    {

        if (TxtMouseVertVal != null)

        {

            TxtMouseVertVal.Text = $"{e.NewValue:F1}x";

            _config.MouseVerticalSensitivity = e.NewValue;

            if (IsLoaded) SaveUIToConfig();

        }

    }



    private void OnStickSpeedChanged(object sender, RoutedPropertyChangedEventArgs<double> e)

    {

        if (TxtStickSpeedVal != null)

        {

            TxtStickSpeedVal.Text = $"{e.NewValue:F1}x";

            _config.StickSpeed = e.NewValue;

            if (IsLoaded) SaveUIToConfig();

        }

    }



    // --- Interactive Controls Rebinding ---

    private void OnPrimaryBindClick(object sender, RoutedEventArgs e)

    {

        if (sender is Button btn && btn.Tag is ActionBinding binding)

        {

            CancelCapture();

            _capturingBinding = binding;

            _capturingSlot = 1;

            _activeCaptureButton = btn;

            btn.Content = LocalizationManager.Get("PressKeyPrompt", "[ PRESIONA UNA TECLA O RATÓN... ]");

            btn.Background = new SolidColorBrush(Color.FromArgb(80, 229, 193, 88));

        }

    }



    private void OnSecondaryBindClick(object sender, RoutedEventArgs e)

    {

        if (sender is Button btn && btn.Tag is ActionBinding binding)

        {

            CancelCapture();

            _capturingBinding = binding;

            _capturingSlot = 2;

            _activeCaptureButton = btn;

            btn.Content = LocalizationManager.Get("PressKeyPrompt", "[ PRESIONA UNA TECLA O RATÓN... ]");

            btn.Background = new SolidColorBrush(Color.FromArgb(80, 229, 193, 88));

        }

    }



    private void OnClearBindingClick(object sender, RoutedEventArgs e)

    {

        if (sender is Button btn && btn.Tag is ActionBinding binding)

        {

            CancelCapture();

            if (binding.SecondaryKeyOrMouse != "-")

            {

                binding.SecondaryKeyOrMouse = "-";

            }

            else

            {

                binding.PrimaryKey = "-";

            }

            SaveUIToConfig();

            RefreshBindingsList();

        }

    }



    private void CancelCapture()

    {

        if (_activeCaptureButton != null && _capturingBinding != null)

        {

            _activeCaptureButton.Content = _capturingSlot == 1 ? _capturingBinding.PrimaryKey : _capturingBinding.SecondaryKeyOrMouse;

            _activeCaptureButton.Background = new SolidColorBrush(Color.FromRgb(22, 25, 32));

        }

        _capturingBinding = null;

        _capturingSlot = 0;

        _activeCaptureButton = null;

    }



    private void OnWindowPreviewKeyDown(object sender, KeyEventArgs e)

    {

        if (_capturingBinding == null) return;



        if (e.Key == Key.Escape)

        {

            CancelCapture();

            e.Handled = true;

            return;

        }



        string keyName = e.Key switch

        {

            Key.Space => "Space",

            Key.LeftCtrl => "Left Ctrl",

            Key.RightCtrl => "Right Ctrl",

            Key.LeftShift => "Left Shift",

            Key.RightShift => "Right Shift",

            Key.LeftAlt => "Left Alt",

            Key.RightAlt => "Right Alt",

            Key.Tab => "Tab",

            Key.Return => "Return",

            Key.Back => "Backspace",

            Key.Up => "Up",

            Key.Down => "Down",

            Key.Left => "Left",

            Key.Right => "Right",

            Key.D0 => "0",

            Key.D1 => "1",

            Key.D2 => "2",

            Key.D3 => "3",

            Key.D4 => "4",

            Key.D5 => "5",

            Key.D6 => "6",

            Key.D7 => "7",

            Key.D8 => "8",

            Key.D9 => "9",

            _ => e.Key.ToString()

        };



        if (_capturingSlot == 1)

        {

            _capturingBinding.PrimaryKey = keyName;

        }

        else

        {

            _capturingBinding.SecondaryKeyOrMouse = keyName;

        }



        CancelCapture();

        SaveUIToConfig();

        RefreshBindingsList();

        e.Handled = true;

    }



    private void OnWindowPreviewMouseDown(object sender, MouseButtonEventArgs e)

    {

        if (_capturingBinding == null) return;



        // If clicking on the active button itself, ignore to allow initial click

        if (_activeCaptureButton != null && _activeCaptureButton.IsMouseOver)

        {

            return;

        }



        string mouseBtn = e.ChangedButton switch

        {

            MouseButton.Left => "Left button",

            MouseButton.Right => "Right button",

            MouseButton.Middle => "Middle button",

            MouseButton.XButton1 => "Back button",

            MouseButton.XButton2 => "Forward button",

            _ => "Left button"

        };



        if (_capturingSlot == 1)

        {

            _capturingBinding.PrimaryKey = mouseBtn;

        }

        else

        {

            _capturingBinding.SecondaryKeyOrMouse = mouseBtn;

        }



        CancelCapture();

        SaveUIToConfig();

        RefreshBindingsList();

        e.Handled = true;

    }



    private void OnWindowPreviewMouseWheel(object sender, MouseWheelEventArgs e)

    {

        if (_capturingBinding == null) return;



        string wheel = e.Delta > 0 ? "Wheel up" : "Wheel down";

        if (_capturingSlot == 1)

        {

            _capturingBinding.PrimaryKey = wheel;

        }

        else

        {

            _capturingBinding.SecondaryKeyOrMouse = wheel;

        }



        CancelCapture();

        SaveUIToConfig();

        RefreshBindingsList();

        e.Handled = true;

    }



    private void OnResetControlsClicked(object sender, RoutedEventArgs e)

    {

        CancelCapture();

        _bindings = LauncherConfig.GetDefaultBindings();

        SliderMouseSens.Value = 3.0;

        SliderMouseAimSens.Value = 1.0;

        SliderMouseVert.Value = 1.0;

        ChkInvertY.IsChecked = false;

        ChkInvertX.IsChecked = false;

        SliderStickSpeed.Value = 1.0;

        ChkVibration.IsChecked = true;

        SetComboByTag(ComboPrompts, "auto");



        SaveUIToConfig();

        RefreshBindingsList();



        TxtControlSavedNotice.Text = "✓ " + LocalizationManager.Get("BtnResetControls", "Controles restablecidos");

        TxtControlSavedNotice.Visibility = Visibility.Visible;

    }



    private async void OnSaveControlsClicked(object sender, RoutedEventArgs e)

    {

        CancelCapture();

        SaveUIToConfig();

        TxtControlSavedNotice.Text = "✓ " + LocalizationManager.Get("BtnSaveControls", "Controles guardados permanentemente");

        TxtControlSavedNotice.Visibility = Visibility.Visible;

        await Task.Delay(3000);

        TxtControlSavedNotice.Visibility = Visibility.Collapsed;

    }



    // --- Game Launch & Process Controls ---

    private async void OnPlayClicked(object sender, RoutedEventArgs e)

    {

        if (_runner.IsRunning) return;



        // Guaranteed immediate save before launch

        SaveUIToConfig();



        BtnPlay.IsEnabled = false;

        BtnPlay.Content = LocalizationManager.Get("BtnStarting", "INICIANDO...");

        BtnStop.Visibility = Visibility.Visible;



        TxtConsole.AppendText($"\n[{DateTime.Now:HH:mm:ss}] Iniciando {_config.SelectedGame.ToUpper()} con API={_config.GraphicsApi.ToUpper()} (VRAM Cap={_config.VramBudgetMb} MB)...\n");



        bool started = await _runner.LaunchAsync(_config, _workspaceRoot, _config.SelectedGame);

        if (!started)

        {

            BtnPlay.IsEnabled = true;

            string btnPlayBase = LocalizationManager.Get("BtnPlay", "► JUGAR");

            BtnPlay.Content = $"{btnPlayBase} {(_config.SelectedGame switch { "u1" => "PARTE I", "u2" => "PARTE II", "u3" => "PARTE III", _ => "" })}";

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

        string[] checkCandidates = {
            Path.Combine(_workspaceRoot, "core", "tools", "check_game_files.py"),
            Path.Combine(_workspaceRoot, "tools", "check_game_files.py"),
            Path.Combine(_workspaceRoot, "Project", "tools", "check_game_files.py"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "core", "tools", "check_game_files.py")
        };
        string toolScript = "";
        foreach (var c in checkCandidates) { if (File.Exists(c)) { toolScript = c; break; } }
        if (string.IsNullOrEmpty(toolScript)) return;



        TxtConsole.AppendText("\n[Verificación] Comprobando integridad SHA256 de archivos del juego...\n");

        await Task.Run(() =>

        {

            try

            {

                var psi = new ProcessStartInfo

                {

                    FileName = GameRunner.ResolvePython(_workspaceRoot),
                    WorkingDirectory = Path.GetDirectoryName(toolScript) ?? _workspaceRoot,

                    Arguments = $"\"{toolScript}\" check \"{_config.GameDir}\"",

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



    private record GpuInfo(int Id, string Name, string Type);



    private List<GpuInfo> EnumerateGpus()

    {

        var list = new List<GpuInfo>();

        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);



        string[] candidates = {

            Path.Combine(_workspaceRoot, "core", "gow3-gpu-capabilities.exe"),
            Path.Combine(_workspaceRoot, "Build", "Windows", "gow3-gpu-capabilities.exe"),

            Path.Combine(_workspaceRoot, "Distribution", "Uncharted", "gow3-gpu-capabilities.exe"),

            Path.Combine(_workspaceRoot, "gow3-gpu-capabilities.exe"),

            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "gow3-gpu-capabilities.exe"),

            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "gow3-gpu-capabilities.exe"),

        };

        foreach (var exe in candidates)

        {

            if (File.Exists(exe))

            {

                try

                {

                    var psi = new ProcessStartInfo(exe, "--list")

                    {

                        RedirectStandardOutput = true,

                        UseShellExecute = false,

                        CreateNoWindow = true

                    };

                    using var proc = Process.Start(psi);

                    if (proc != null)

                    {

                        string output = proc.StandardOutput.ReadToEnd();

                        proc.WaitForExit(3000);

                        foreach (var rawLine in output.Split('\n', '\r'))

                        {

                            var line = rawLine.Trim();

                            if (string.IsNullOrEmpty(line)) continue;

                            var parts = line.Split('\t');

                            if (parts.Length >= 2)

                            {

                                int id = int.TryParse(parts[0].Trim(), out int parsed) ? parsed : list.Count;

                                string name = parts[1].Trim();

                                string type = parts.Length > 2 ? parts[2].Trim() : (name.Contains("Radeon", StringComparison.OrdinalIgnoreCase) || name.Contains("Intel", StringComparison.OrdinalIgnoreCase) ? "integrated" : "discrete");

                                if (!string.IsNullOrEmpty(name) && seen.Add(name))

                                {

                                    list.Add(new GpuInfo(id, name, type));

                                }

                            }

                        }

                        if (list.Count > 0) return list;

                    }

                }

                catch { }

            }

        }



        try

        {

            var psi = new ProcessStartInfo("powershell", "-NoProfile -Command \"(Get-CimInstance Win32_VideoController).Name\"")

            {

                RedirectStandardOutput = true,

                UseShellExecute = false,

                CreateNoWindow = true

            };

            using var proc = Process.Start(psi);

            if (proc != null)

            {

                string output = proc.StandardOutput.ReadToEnd();

                proc.WaitForExit(4000);

                foreach (var rawLine in output.Split('\n', '\r'))

                {

                    string name = rawLine.Trim();

                    if (!string.IsNullOrEmpty(name) && seen.Add(name))

                    {

                        string type = (name.Contains("Radeon", StringComparison.OrdinalIgnoreCase) || name.Contains("Intel", StringComparison.OrdinalIgnoreCase)) ? "integrated" : "discrete";

                        list.Add(new GpuInfo(list.Count, name, type));

                    }

                }

            }

        }

        catch { }



        if (list.Count == 0)

        {

            list.Add(new GpuInfo(0, "NVIDIA GeForce RTX 5050 Laptop GPU", "discrete"));

            list.Add(new GpuInfo(1, "AMD Radeon 760M Graphics", "integrated"));

        }

        return list;

    }



    private void PopulateGpuCombo()

    {

        var gpus = EnumerateGpus();

        ComboGpu.Items.Clear();

        int selectedIndex = 0;

        for (int i = 0; i < gpus.Count; i++)

        {

            var gpu = gpus[i];

            string typeLabel = gpu.Type.Equals("integrated", StringComparison.OrdinalIgnoreCase) ?

                LocalizationManager.Get("Integrated", "Integrada CPU") :

                LocalizationManager.Get("Dedicated", "Dedicada");

            var item = new ComboBoxItem

            {

                Content = $"{gpu.Name} ({typeLabel})",

                Tag = $"{gpu.Id}|{gpu.Name}|{gpu.Type}"

            };

            ComboGpu.Items.Add(item);



            if (!string.IsNullOrEmpty(_config.GpuName))

            {

                if (gpu.Name.Equals(_config.GpuName, StringComparison.OrdinalIgnoreCase) ||

                    (int.TryParse(_config.GpuId, out int cfgId) && cfgId == gpu.Id))

                {

                    selectedIndex = i;

                }

            }

            else if (gpu.Type.Equals("discrete", StringComparison.OrdinalIgnoreCase))

            {

                selectedIndex = i;

            }

        }



        ComboExpeditionGpu.Items.Clear();

        for (int i = 0; i < ComboGpu.Items.Count; i++)

        {

            var orig = (ComboBoxItem)ComboGpu.Items[i];

            ComboExpeditionGpu.Items.Add(new ComboBoxItem { Content = orig.Content, Tag = orig.Tag });

        }

        if (ComboGpu.Items.Count > 0)

        {

            ComboGpu.SelectedIndex = selectedIndex;

            ComboExpeditionGpu.SelectedIndex = selectedIndex;

        }

    }



    private void OnExpeditionGpuChanged(object sender, SelectionChangedEventArgs e)

    {

        if (!IsLoaded) return;

        if (ComboExpeditionGpu.SelectedIndex >= 0 && ComboExpeditionGpu.SelectedIndex < ComboGpu.Items.Count)

        {

            if (ComboGpu.SelectedIndex != ComboExpeditionGpu.SelectedIndex)

            {

                ComboGpu.SelectedIndex = ComboExpeditionGpu.SelectedIndex;

            }

        }

    }



    private void OnGpuChanged(object sender, SelectionChangedEventArgs e)

    {

        if (ComboExpeditionGpu != null && ComboGpu.SelectedIndex >= 0 && ComboGpu.SelectedIndex < ComboExpeditionGpu.Items.Count)

        {

            if (ComboExpeditionGpu.SelectedIndex != ComboGpu.SelectedIndex)

            {

                ComboExpeditionGpu.SelectedIndex = ComboGpu.SelectedIndex;

            }

        }

        if (!IsLoaded) return;

        if (ComboGpu.SelectedItem is ComboBoxItem item && item.Tag is string tag)

        {

            var parts = tag.Split('|');

            if (parts.Length >= 2)

            {

                _config.GpuId = parts[0];

                _config.GpuName = parts[1];

                string type = parts.Length > 2 ? parts[2] : "discrete";



                if (type.Equals("integrated", StringComparison.OrdinalIgnoreCase))

                {

                    if (_config.VramBudgetMb == "0" || (int.TryParse(_config.VramBudgetMb, out int mb) && mb > 1024))

                    {

                        SetComboByTag(ComboVramCap, "820");

                        _config.VramBudgetMb = "820";

                    }

                }



                SaveUIToConfig();

            }

        }

    }



    // --- GitHub Auto-Updater ---

    private async void CheckForUpdatesAsync()

    {

        try

        {

            using var client = new System.Net.Http.HttpClient();

            client.DefaultRequestHeaders.UserAgent.ParseAdd("UnchartedLauncher/1.0");

            client.Timeout = TimeSpan.FromSeconds(3);



            string url = "https://api.github.com/repos/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/latest";

            var response = await client.GetAsync(url);

            if (response.IsSuccessStatusCode)

            {

                string json = await response.Content.ReadAsStringAsync();

                using var doc = System.Text.Json.JsonDocument.Parse(json);

                if (doc.RootElement.TryGetProperty("tag_name", out var tagElem))

                {

                    string latestTag = tagElem.GetString() ?? "";

                    string htmlUrl = doc.RootElement.TryGetProperty("html_url", out var urlElem) ? urlElem.GetString() ?? "" : "";

                    if (!string.IsNullOrEmpty(latestTag))

                    {

                        Dispatcher.Invoke(() =>

                        {

                            TxtUpdateTitle.Text = $"★ NUEVA ACTUALIZACIÓN DISPONIBLE: {latestTag}";

                            TxtUpdateDesc.Text = "Se ha detectado una versión oficial más reciente en GitHub. Tus ajustes y controles se mantendrán intactos.";

                            BorderUpdateBanner.Tag = htmlUrl;

                            BorderUpdateBanner.Visibility = Visibility.Visible;

                        });

                    }

                }

            }

        }

        catch

        {

            // Gracefully ignore network errors / offline mode

        }

    }



    private void OnUpdateNowClicked(object sender, RoutedEventArgs e)

    {

        string url = BorderUpdateBanner.Tag as string ?? "https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases";

        try

        {

            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo

            {

                FileName = url,

                UseShellExecute = true

            });

        }

        catch { }

    }



    private void OnDismissUpdateClicked(object sender, RoutedEventArgs e)

    {

        BorderUpdateBanner.Visibility = Visibility.Collapsed;

    }

}

