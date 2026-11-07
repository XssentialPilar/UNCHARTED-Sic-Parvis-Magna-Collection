using System;
using System.Collections.Generic;
using System.Globalization;

namespace UnchartedLauncher;

public static class LocalizationManager
{
    public static readonly (string Code, string Name)[] SupportedLanguages = new[]
    {
        ("auto", "Auto (Sistema)"),
        ("es_419", "Español (Latinoamérica)"),
        ("es_ES", "Español (España)"),
        ("en", "English"),
        ("pt_BR", "Português (Brasil)"),
        ("fr", "Français"),
        ("de", "Deutsch"),
        ("it", "Italiano"),
        ("ru", "Русский"),
        ("ja", "日本語"),
        ("zh_CN", "简体中文")
    };

    public static string CurrentLanguage { get; private set; } = "en";

    private static readonly Dictionary<string, Dictionary<string, string>> Translations = new(StringComparer.OrdinalIgnoreCase);

    static LocalizationManager()
    {
        InitializeTranslations();
    }

    public static void Initialize(string preferredLanguage = "auto")
    {
        if (string.IsNullOrEmpty(preferredLanguage) || preferredLanguage.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            CurrentLanguage = DetectSystemLanguage();
        }
        else
        {
            CurrentLanguage = ValidateLanguageCode(preferredLanguage);
        }
    }

    public static void SetLanguage(string langCode)
    {
        if (langCode.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            CurrentLanguage = DetectSystemLanguage();
        }
        else
        {
            CurrentLanguage = ValidateLanguageCode(langCode);
        }
    }

    public static string DetectSystemLanguage()
    {
        try
        {
            string uiLang = CultureInfo.CurrentUICulture.Name;
            if (string.IsNullOrEmpty(uiLang)) uiLang = CultureInfo.CurrentCulture.Name;

            if (uiLang.StartsWith("es", StringComparison.OrdinalIgnoreCase))
            {
                if (uiLang.Equals("es-ES", StringComparison.OrdinalIgnoreCase)) return "es_ES";
                return "es_419"; // Default to Latin American Spanish for all other Spanish regions
            }
            if (uiLang.StartsWith("pt", StringComparison.OrdinalIgnoreCase)) return "pt_BR";
            if (uiLang.StartsWith("fr", StringComparison.OrdinalIgnoreCase)) return "fr";
            if (uiLang.StartsWith("de", StringComparison.OrdinalIgnoreCase)) return "de";
            if (uiLang.StartsWith("it", StringComparison.OrdinalIgnoreCase)) return "it";
            if (uiLang.StartsWith("ru", StringComparison.OrdinalIgnoreCase)) return "ru";
            if (uiLang.StartsWith("ja", StringComparison.OrdinalIgnoreCase)) return "ja";
            if (uiLang.StartsWith("zh", StringComparison.OrdinalIgnoreCase)) return "zh_CN";
        }
        catch { }

        return "en";
    }

    public static string ValidateLanguageCode(string code)
    {
        foreach (var (c, _) in SupportedLanguages)
        {
            if (c.Equals(code, StringComparison.OrdinalIgnoreCase) && c != "auto")
            {
                return c;
            }
        }
        return "en";
    }

    public static string Get(string key, string fallback = "")
    {
        if (Translations.TryGetValue(CurrentLanguage, out var dict) && dict.TryGetValue(key, out var val))
        {
            return val;
        }
        if (Translations.TryGetValue("en", out var enDict) && enDict.TryGetValue(key, out var enVal))
        {
            return enVal;
        }
        return string.IsNullOrEmpty(fallback) ? key : fallback;
    }

    private static void InitializeTranslations()
    {
        // 1. ESPAÑOL (LATINOAMÉRICA) - es_419
        Translations["es_419"] = new Dictionary<string, string>
        {
            ["AppTitle"] = "UNCHARTED: Sic Parvis Magna Collection",
            ["NavExpeditions"] = "EXPEDICIONES",
            ["NavGraphics"] = "GRÁFICOS Y VRAM",
            ["NavDisplay"] = "PANTALLA Y ESCALADO",
            ["NavControls"] = "CONTROLES Y JUEGO",
            ["NavTelemetry"] = "TELEMETRÍA Y LOGS",
            ["NavQuit"] = "SALIR",
            ["PresetsLabel"] = "PREAJUSTES:",
            ["PresetIgpu"] = "iGPU (820MB)",
            ["PresetLow"] = "BAJO 1GB",
            ["PresetMed"] = "MEDIO 2GB ★",
            ["PresetHigh"] = "ALTO",
            ["PresetUltra"] = "ULTRA",
            ["ActiveGpu"] = "GPU Activa:",
            ["Dedicated"] = "Dedicada",
            ["Integrated"] = "Integrada CPU",
            ["SelectExpedition"] = "SELECCIONA TU EXPEDICIÓN",
            ["Part1Title"] = "PARTE I",
            ["Part1Sub"] = "DRAKE'S FORTUNE",
            ["Part2Title"] = "PARTE II",
            ["Part2Sub"] = "AMONG THIEVES",
            ["Part3Title"] = "PARTE III",
            ["Part3Sub"] = "DRAKE'S DECEPTION",
            ["BtnPlay"] = "► JUGAR",
            ["BtnStarting"] = "INICIANDO...",
            ["BtnStop"] = "DETENER",
            ["ReadyStatus"] = "Listo: {0}",
            ["GraphicsHeader"] = "CONFIGURACIÓN DE GRÁFICOS Y GESTIÓN DE VRAM",
            ["GpuActiveLabel"] = "Tarjeta Gráfica Activa (GPU):",
            ["GpuActiveSub"] = "Selecciona GPU dedicada (RTX) o integrada (CPU)",
            ["ApiLabel"] = "API Gráfica de Presentación:",
            ["ApiSub"] = "DirectX 9Ex ofrece menor latencia; DirectX 11 provee Flip-Discard",
            ["VramCapLabel"] = "Límite Estricto de VRAM (Cap):",
            ["VramCapSub"] = "Evita saturación de bus PCIe y caídas de FPS",
            ["TexQualityLabel"] = "Calidad de Texturas:",
            ["TexQualitySub"] = "Ajuste de mipmaps y LOD Bias (Bajo ahorra 75% VRAM)",
            ["ShadowQualityLabel"] = "Calidad de Sombras:",
            ["ShadowQualitySub"] = "Resolución de cascadas solares (Bajo = 32 taps)",
            ["DynamicShadowsLabel"] = "Sombras Dinámicas:",
            ["DynamicShadowsSub"] = "Desactivar elimina pases de sombra solares (+FPS masivo)",
            ["ViewDistanceLabel"] = "Distancia de Renderizado (LOD):",
            ["ViewDistanceSub"] = "Escala de detalle de geometría con suavizado anti-popping",
            ["SsaoLabel"] = "Oclusión Ambiental (SSAO):",
            ["SsaoSub"] = "Sombreado de esquinas y profundidad",
            ["AnisoLabel"] = "Filtrado Anisótropo:",
            ["AnisoSub"] = "Nitidez de texturas en ángulos oblicuos",
            ["ChkFullResTex"] = "Permitir Texturas a Resolución Completa (requiere >2GB VRAM)",
            ["ChkHiresShadows"] = "Sombras en Alta Resolución (requiere GPU dedicada)",
            ["DisplayHeader"] = "RESOLUCIÓN, ESCALADO Y SINCRONIZACIÓN",
            ["ResLabel"] = "Resolución de Renderizado:",
            ["ResSub"] = "Resolución interna de la escena",
            ["UpscalerLabel"] = "Tecnología de Escalado / Anti-Aliasing:",
            ["UpscalerSub"] = "Reconstrucción temporal de imagen",
            ["FpsLimitLabel"] = "Límite de Fotogramas (FPS):",
            ["FpsLimitSub"] = "Estabilidad de tiempo de cuadro",
            ["FramePacerLabel"] = "Regulador de Cuadros (Frame Pacer):",
            ["FramePacerSub"] = "Temporizador microsegundo de presentación",
            ["ChkVsync"] = "Sincronización Vertical (V-Sync activado)",
            ["ChkMotionBlur"] = "Desenfoque de Movimiento (Motion Blur)",
            ["ChkDepthOfField"] = "Profundidad de Campo (Depth of Field cinematográfico)",
            ["ControlsHeader"] = "MAPEO DE CONTROLES, RATÓN Y CONFIGURACIÓN",
            ["GameDirLabel"] = "Directorio de Archivos del Juego (CUSA02344):",
            ["BtnBrowse"] = "BUSCAR...",
            ["MouseSectionHeader"] = "CONFIGURACIÓN AVANZADA DE RATÓN Y APUNTADO",
            ["MouseSensLabel"] = "Sensibilidad General del Ratón:",
            ["MouseAimSensLabel"] = "Sensibilidad al Apuntar con Arma:",
            ["MouseVertLabel"] = "Sensibilidad Vertical del Ratón:",
            ["ChkInvertY"] = "Invertir Eje Y del Ratón (Mirar Arriba/Abajo)",
            ["ChkInvertX"] = "Invertir Eje X del Ratón (Mirar Izquierda/Derecha)",
            ["GamepadSectionHeader"] = "MANDO Y DISPOSITIVO DE CONTROL",
            ["StickSpeedLabel"] = "Velocidad de Cámara con Palancas de Mando:",
            ["ChkVibration"] = "Vibración de Mando (Rumble Háptico)",
            ["PromptsLabel"] = "Iconos de Botones en Pantalla:",
            ["PromptsAuto"] = "Automático (Detectado)",
            ["PromptsPs"] = "PlayStation (△ ○ ✕ □)",
            ["PromptsXbox"] = "Xbox (Y B A X)",
            ["PromptsPc"] = "Teclado y Ratón (PC)",
            ["KeybindsHeader"] = "ASIGNACIÓN DE TECLAS Y ACCIONES",
            ["KeybindsSub"] = "Haz clic en cualquier tecla para cambiar su asignación libremente sin restricciones.",
            ["BtnResetControls"] = "RESTABLECER CONTROLES PREDETERMINADOS",
            ["BtnSaveControls"] = "GUARDAR CAMBIOS DE CONTROLES",
            ["ActionMoveForward"] = "Moverse Adelante",
            ["ActionMoveBackward"] = "Moverse Atrás",
            ["ActionMoveLeft"] = "Moverse a la Izquierda",
            ["ActionMoveRight"] = "Moverse a la Derecha",
            ["ActionLookUp"] = "Mirar Arriba",
            ["ActionLookDown"] = "Mirar Abajo",
            ["ActionLookLeft"] = "Mirar a la Izquierda",
            ["ActionLookRight"] = "Mirar a la Derecha",
            ["ActionJump"] = "Saltar / Trepar",
            ["ActionCover"] = "Cubrirse / Rodar / Dejarse caer",
            ["ActionMelee"] = "Golpe Cuerpo a Cuerpo",
            ["ActionInteract"] = "Interactuar / Recoger",
            ["ActionAim"] = "Apuntar Arma",
            ["ActionFire"] = "Disparar Arma",
            ["ActionReload"] = "Recargar Arma",
            ["ActionGrenade"] = "Lanzar Granada",
            ["ActionSprint"] = "Esprintar (Stick Izquierdo)",
            ["ActionZoom"] = "Zoom Mirilla (Stick Derecho)",
            ["ActionWeapon1"] = "Arma 1 (D-Pad Izq)",
            ["ActionWeapon2"] = "Arma 2 (D-Pad Der)",
            ["ActionWeapon3"] = "Arma 3 (D-Pad Arriba)",
            ["ActionWeapon4"] = "Arma 4 (D-Pad Abajo)",
            ["ActionJournal"] = "Diario de Drake",
            ["ActionPause"] = "Menú de Pausa",
            ["ActionOverlay"] = "Menú In-Game (Overlay)",
            ["PressKeyPrompt"] = "Presiona una tecla...",
            ["TelemetryHeader"] = "REGISTRO Y TELEMETRÍA EN TIEMPO REAL",
            ["BtnVerify"] = "VERIFICAR ARCHIVOS (1.02)",
            ["BtnClearConsole"] = "LIMPIAR CONSOLA",
            ["LanguageLabel"] = "Idioma:"
        };

        // 2. ESPAÑOL (ESPAÑA) - es_ES
        Translations["es_ES"] = new Dictionary<string, string>(Translations["es_419"])
        {
            ["AppTitle"] = "UNCHARTED: Sic Parvis Magna Collection",
            ["SelectExpedition"] = "SELECCIONA TU EXPEDICIÓN",
            ["GameDirLabel"] = "Directorio de archivos del juego (CUSA02344):",
            ["ActionCover"] = "Cubrirse / Rodar / Soltarse",
            ["ActionMelee"] = "Cuerpo a cuerpo",
            ["ActionSprint"] = "Esprintar (Pulsar stick izquierdo)",
            ["ActionZoom"] = "Zoom / Mirilla (Pulsar stick derecho)",
            ["ActionJournal"] = "Cuaderno de Drake",
            ["LanguageLabel"] = "Idioma:"
        };

        // 3. ENGLISH - en
        Translations["en"] = new Dictionary<string, string>
        {
            ["AppTitle"] = "UNCHARTED: Sic Parvis Magna Collection",
            ["NavExpeditions"] = "EXPEDITIONS",
            ["NavGraphics"] = "GRAPHICS & VRAM",
            ["NavDisplay"] = "DISPLAY & SCALING",
            ["NavControls"] = "INPUT & CONTROLS",
            ["NavTelemetry"] = "TELEMETRY & LOGS",
            ["NavQuit"] = "QUIT",
            ["PresetsLabel"] = "PRESETS:",
            ["PresetIgpu"] = "iGPU (820MB)",
            ["PresetLow"] = "LOW 1GB",
            ["PresetMed"] = "MED 2GB ★",
            ["PresetHigh"] = "HIGH",
            ["PresetUltra"] = "ULTRA",
            ["ActiveGpu"] = "Active GPU:",
            ["Dedicated"] = "Discrete",
            ["Integrated"] = "CPU Integrated",
            ["SelectExpedition"] = "SELECT YOUR EXPEDITION",
            ["Part1Title"] = "PART I",
            ["Part1Sub"] = "DRAKE'S FORTUNE",
            ["Part2Title"] = "PART II",
            ["Part2Sub"] = "AMONG THIEVES",
            ["Part3Title"] = "PART III",
            ["Part3Sub"] = "DRAKE'S DECEPTION",
            ["BtnPlay"] = "► PLAY",
            ["BtnStarting"] = "LAUNCHING...",
            ["BtnStop"] = "STOP",
            ["ReadyStatus"] = "Ready: {0}",
            ["GraphicsHeader"] = "GRAPHICS CONFIGURATION & VRAM MANAGEMENT",
            ["GpuActiveLabel"] = "Active Graphics Adapter (GPU):",
            ["GpuActiveSub"] = "Select discrete GPU (RTX) or integrated (CPU)",
            ["ApiLabel"] = "Presentation Graphics API:",
            ["ApiSub"] = "DirectX 9Ex offers lower latency; DirectX 11 provides Flip-Discard",
            ["VramCapLabel"] = "Strict VRAM Budget Cap:",
            ["VramCapSub"] = "Prevents PCIe bus paging and frame drops",
            ["TexQualityLabel"] = "Texture Quality:",
            ["TexQualitySub"] = "Mipmap and LOD bias adjustment (Low saves 75% VRAM)",
            ["ShadowQualityLabel"] = "Shadow Quality:",
            ["ShadowQualitySub"] = "Sun cascade resolution (Low = 32 taps)",
            ["DynamicShadowsLabel"] = "Dynamic Shadows:",
            ["DynamicShadowsSub"] = "Disabling skips sun shadow passes (massive FPS boost)",
            ["ViewDistanceLabel"] = "Draw Distance (LOD):",
            ["ViewDistanceSub"] = "Geometry detail scaling with anti-popping exponential filter",
            ["SsaoLabel"] = "Ambient Occlusion (SSAO):",
            ["SsaoSub"] = "Depth contact shading and corner darkness",
            ["AnisoLabel"] = "Anisotropic Filtering:",
            ["AnisoSub"] = "Texture sharpness at oblique grazing angles",
            ["ChkFullResTex"] = "Allow Full Resolution Textures (requires >2GB VRAM)",
            ["ChkHiresShadows"] = "High Resolution Shadows (requires discrete GPU)",
            ["DisplayHeader"] = "RESOLUTION, SCALING & SYNCHRONIZATION",
            ["ResLabel"] = "Internal Render Resolution:",
            ["ResSub"] = "Scene internal rasterization resolution",
            ["UpscalerLabel"] = "Upscaling & Anti-Aliasing:",
            ["UpscalerSub"] = "Temporal reconstruction technique",
            ["FpsLimitLabel"] = "Frame Rate Limit (FPS):",
            ["FpsLimitSub"] = "Frame pacing stability constraint",
            ["FramePacerLabel"] = "Frame Pacer Mode:",
            ["FramePacerSub"] = "Microsecond presentation timer",
            ["ChkVsync"] = "Vertical Synchronization (V-Sync enabled)",
            ["ChkMotionBlur"] = "Motion Blur Effect",
            ["ChkDepthOfField"] = "Cinematic Depth of Field",
            ["ControlsHeader"] = "INPUT MAPPING, MOUSE & CONTROLLER CONFIGURATION",
            ["GameDirLabel"] = "Game Data Directory (CUSA02344):",
            ["BtnBrowse"] = "BROWSE...",
            ["MouseSectionHeader"] = "ADVANCED MOUSE & AIMING TUNING",
            ["MouseSensLabel"] = "General Mouse Sensitivity:",
            ["MouseAimSensLabel"] = "Weapon Aiming Sensitivity:",
            ["MouseVertLabel"] = "Vertical Mouse Sensitivity:",
            ["ChkInvertY"] = "Invert Mouse Y-Axis (Look Up/Down)",
            ["ChkInvertX"] = "Invert Mouse X-Axis (Look Left/Right)",
            ["GamepadSectionHeader"] = "CONTROLLER & GAMEPAD PREFERENCES",
            ["StickSpeedLabel"] = "Gamepad Stick Camera Speed:",
            ["ChkVibration"] = "Controller Vibration (Haptic Rumble)",
            ["PromptsLabel"] = "Button Prompt Glyphs:",
            ["PromptsAuto"] = "Automatic (Connected Device)",
            ["PromptsPs"] = "PlayStation (△ ○ ✕ □)",
            ["PromptsXbox"] = "Xbox (Y B A X)",
            ["PromptsPc"] = "Keyboard & Mouse (PC)",
            ["KeybindsHeader"] = "KEYBOARD & MOUSE REBINDING",
            ["KeybindsSub"] = "Click on any binding button to reassign keys freely without restrictions.",
            ["BtnResetControls"] = "RESET TO DEFAULT BINDINGS",
            ["BtnSaveControls"] = "SAVE CONTROLS",
            ["ActionMoveForward"] = "Move Forward",
            ["ActionMoveBackward"] = "Move Backward",
            ["ActionMoveLeft"] = "Move Left",
            ["ActionMoveRight"] = "Move Right",
            ["ActionLookUp"] = "Look Up",
            ["ActionLookDown"] = "Look Down",
            ["ActionLookLeft"] = "Look Left",
            ["ActionLookRight"] = "Look Right",
            ["ActionJump"] = "Jump / Climb",
            ["ActionCover"] = "Take Cover / Roll / Drop",
            ["ActionMelee"] = "Melee Attack",
            ["ActionInteract"] = "Interact / Pick Up",
            ["ActionAim"] = "Aim Weapon",
            ["ActionFire"] = "Fire Weapon",
            ["ActionReload"] = "Reload Weapon",
            ["ActionGrenade"] = "Throw Grenade",
            ["ActionSprint"] = "Sprint (Left Stick Click)",
            ["ActionZoom"] = "Zoom / Scope (Right Stick Click)",
            ["ActionWeapon1"] = "Weapon 1 (D-Pad Left)",
            ["ActionWeapon2"] = "Weapon 2 (D-Pad Right)",
            ["ActionWeapon3"] = "Weapon 3 (D-Pad Up)",
            ["ActionWeapon4"] = "Weapon 4 (D-Pad Down)",
            ["ActionJournal"] = "Drake's Journal",
            ["ActionPause"] = "Pause Menu",
            ["ActionOverlay"] = "In-Game Menu (Overlay)",
            ["PressKeyPrompt"] = "Press a key...",
            ["TelemetryHeader"] = "REAL-TIME LOGGING & TELEMETRY",
            ["BtnVerify"] = "VERIFY GAME FILES (1.02)",
            ["BtnClearConsole"] = "CLEAR LOG CONSOLE",
            ["LanguageLabel"] = "Language:"
        };

        // 4. PORTUGUÊS (BRASIL) - pt_BR
        Translations["pt_BR"] = new Dictionary<string, string>(Translations["en"])
        {
            ["NavExpeditions"] = "EXPEDIÇÕES",
            ["NavGraphics"] = "GRÁFICOS E VRAM",
            ["NavDisplay"] = "TELA E ESCALONAMENTO",
            ["NavControls"] = "CONTROLES E JOGO",
            ["NavTelemetry"] = "TELEMETRIA E LOGS",
            ["NavQuit"] = "SAIR",
            ["PresetsLabel"] = "PREDEFINIÇÕES:",
            ["ActiveGpu"] = "GPU Ativa:",
            ["SelectExpedition"] = "SELECIONE SUA EXPEDIÇÃO",
            ["Part1Title"] = "PARTE I",
            ["Part2Title"] = "PARTE II",
            ["Part3Title"] = "PARTE III",
            ["BtnPlay"] = "► JOGAR",
            ["BtnStarting"] = "INICIANDO...",
            ["BtnStop"] = "PARAR",
            ["ReadyStatus"] = "Pronto: {0}",
            ["GraphicsHeader"] = "CONFIGURAÇÃO GRÁFICA E GESTÃO DE VRAM",
            ["GpuActiveLabel"] = "Placa de Vídeo Ativa (GPU):",
            ["ApiLabel"] = "API Gráfica de Apresentação:",
            ["VramCapLabel"] = "Limite Estrito de VRAM:",
            ["TexQualityLabel"] = "Qualidade das Texturas:",
            ["ShadowQualityLabel"] = "Qualidade das Sombras:",
            ["DynamicShadowsLabel"] = "Sombras Dinâmicas:",
            ["ViewDistanceLabel"] = "Distância de Renderização (LOD):",
            ["SsaoLabel"] = "Oclusão Ambiental (SSAO):",
            ["DisplayHeader"] = "RESOLUÇÃO, ESCALA E SINCRONIZAÇÃO",
            ["ResLabel"] = "Resolução Interna de Renderização:",
            ["FpsLimitLabel"] = "Limite de Quadros (FPS):",
            ["ChkVsync"] = "Sincronização Vertical (V-Sync)",
            ["ControlsHeader"] = "MAPEMANTO DE CONTROLES E CONFIGURAÇÃO",
            ["GameDirLabel"] = "Diretório do Jogo (CUSA02344):",
            ["BtnBrowse"] = "PROCURAR...",
            ["MouseSensLabel"] = "Sensibilidade do Mouse:",
            ["MouseAimSensLabel"] = "Sensibilidade ao Mirar:",
            ["ChkVibration"] = "Vibração do Controle",
            ["BtnResetControls"] = "RESTAURAR PADRÕES",
            ["BtnSaveControls"] = "SALVAR CONTROLES",
            ["ActionMoveForward"] = "Mover para Frente",
            ["ActionMoveBackward"] = "Mover para Trás",
            ["ActionMoveLeft"] = "Mover para Esquerda",
            ["ActionMoveRight"] = "Mover para Direita",
            ["ActionJump"] = "Pular / Escalar",
            ["ActionCover"] = "Cobrir-se / Rolar",
            ["ActionMelee"] = "Golpe Corpo a Corpo",
            ["ActionInteract"] = "Interagir / Pegar",
            ["ActionAim"] = "Mirar Arma",
            ["ActionFire"] = "Atirar",
            ["ActionReload"] = "Recarregar",
            ["ActionGrenade"] = "Lançar Granada",
            ["ActionSprint"] = "Correr (Pressionar Stick Esquerdo)",
            ["ActionZoom"] = "Zoom (Pressionar Stick Direito)",
            ["ActionJournal"] = "Diário de Drake",
            ["ActionPause"] = "Menu de Pausa",
            ["PressKeyPrompt"] = "Pressione uma tecla...",
            ["LanguageLabel"] = "Idioma:"
        };

        // 5. FRANÇAIS - fr
        Translations["fr"] = new Dictionary<string, string>(Translations["en"])
        {
            ["NavExpeditions"] = "EXPÉDITIONS",
            ["NavGraphics"] = "GRAPHISMES ET VRAM",
            ["NavDisplay"] = "AFFICHAGE ET ÉCHELLE",
            ["NavControls"] = "COMMANDES ET JEU",
            ["NavTelemetry"] = "TÉLÉMÉTRIE ET LOGS",
            ["NavQuit"] = "QUITTER",
            ["PresetsLabel"] = "PRÉRÉGLAGES :",
            ["ActiveGpu"] = "GPU Actif :",
            ["SelectExpedition"] = "SÉLECTIONNEZ VOTRE EXPÉDITION",
            ["Part1Title"] = "PARTIE I",
            ["Part2Title"] = "PARTIE II",
            ["Part3Title"] = "PARTIE III",
            ["BtnPlay"] = "► JOUER",
            ["BtnStarting"] = "LANCEMENT...",
            ["BtnStop"] = "ARRÊTER",
            ["ReadyStatus"] = "Prêt : {0}",
            ["GraphicsHeader"] = "CONFIGURATION GRAPHIQUE & GESTION VRAM",
            ["GpuActiveLabel"] = "Carte Graphique Active (GPU) :",
            ["ApiLabel"] = "API Graphique de Présentation :",
            ["VramCapLabel"] = "Limite Stricte de VRAM :",
            ["TexQualityLabel"] = "Qualité des Textures :",
            ["ShadowQualityLabel"] = "Qualité des Ombres :",
            ["DynamicShadowsLabel"] = "Ombres Dynamiques :",
            ["ViewDistanceLabel"] = "Distance d'Affichage (LOD) :",
            ["SsaoLabel"] = "Occlusion Ambiante (SSAO) :",
            ["ControlsHeader"] = "ATTRIBUTION DES COMMANDES ET CONFIGURATION",
            ["GameDirLabel"] = "Répertoire du Jeu (CUSA02344) :",
            ["BtnBrowse"] = "PARCOURIR...",
            ["BtnResetControls"] = "RÉTABLIR LES VALEURS PAR DÉFAUT",
            ["BtnSaveControls"] = "ENREGISTRER LES COMMANDES",
            ["ActionMoveForward"] = "Avancer",
            ["ActionMoveBackward"] = "Reculer",
            ["ActionMoveLeft"] = "Aller à Gauche",
            ["ActionMoveRight"] = "Aller à Droite",
            ["ActionJump"] = "Sauter / Escalader",
            ["ActionCover"] = "Se mettre à l'abri / Roulade",
            ["ActionMelee"] = "Corps à corps",
            ["ActionInteract"] = "Interagir / Ramasser",
            ["ActionAim"] = "Viser",
            ["ActionFire"] = "Tirer",
            ["ActionReload"] = "Recharger",
            ["ActionGrenade"] = "Lancer une grenade",
            ["ActionSprint"] = "Sprinter (Stick gauche)",
            ["ActionZoom"] = "Zoomer (Stick droit)",
            ["ActionJournal"] = "Journal de Drake",
            ["ActionPause"] = "Menu Pause",
            ["PressKeyPrompt"] = "Appuyez sur une touche...",
            ["LanguageLabel"] = "Langue :"
        };

        // 6. DEUTSCH - de
        Translations["de"] = new Dictionary<string, string>(Translations["en"])
        {
            ["NavExpeditions"] = "EXPEDITIONEN",
            ["NavGraphics"] = "GRAFIK & VRAM",
            ["NavDisplay"] = "ANZEIGE & SKALIERUNG",
            ["NavControls"] = "STEUERUNG & SPIEL",
            ["NavTelemetry"] = "TELEMETRIE & LOGS",
            ["NavQuit"] = "BEENDEN",
            ["PresetsLabel"] = "VOREINSTELLUNGEN:",
            ["ActiveGpu"] = "Aktive GPU:",
            ["SelectExpedition"] = "EXPEDITION WÄHLEN",
            ["Part1Title"] = "TEIL I",
            ["Part2Title"] = "TEIL II",
            ["Part3Title"] = "TEIL III",
            ["BtnPlay"] = "► SPIELEN",
            ["BtnStarting"] = "STARTET...",
            ["BtnStop"] = "STOPP",
            ["ReadyStatus"] = "Bereit: {0}",
            ["GraphicsHeader"] = "GRAFIKKONFIGURATION & VRAM-VERWALTUNG",
            ["GpuActiveLabel"] = "Aktive Grafikkarte (GPU):",
            ["ApiLabel"] = "Grafik-API:",
            ["VramCapLabel"] = "Strikte VRAM-Begrenzung:",
            ["TexQualityLabel"] = "Texturqualität:",
            ["ShadowQualityLabel"] = "Schattenqualität:",
            ["DynamicShadowsLabel"] = "Dynamische Schatten:",
            ["ViewDistanceLabel"] = "Sichtweite (LOD):",
            ["ControlsHeader"] = "TASTENBELEGUNG & STEUERUNG",
            ["GameDirLabel"] = "Spielverzeichnis (CUSA02344):",
            ["BtnBrowse"] = "DURCHSUCHEN...",
            ["BtnResetControls"] = "AUF STANDARDEINSTELLUNGEN ZURÜCKSETZEN",
            ["BtnSaveControls"] = "STEUERUNG SPEICHERN",
            ["ActionMoveForward"] = "Vorwärts bewegen",
            ["ActionMoveBackward"] = "Rückwärts bewegen",
            ["ActionMoveLeft"] = "Nach links",
            ["ActionMoveRight"] = "Nach rechts",
            ["ActionJump"] = "Springen / Klettern",
            ["ActionCover"] = "In Deckung gehen / Rollen",
            ["ActionMelee"] = "Nahkampf",
            ["ActionInteract"] = "Interagieren / Aufheben",
            ["ActionAim"] = "Zielen",
            ["ActionFire"] = "Feuern",
            ["ActionReload"] = "Nachladen",
            ["ActionGrenade"] = "Granate werfen",
            ["ActionSprint"] = "Sprinten (Linker Stick)",
            ["ActionZoom"] = "Zoom (Rechter Stick)",
            ["ActionJournal"] = "Drakes Tagebuch",
            ["ActionPause"] = "Pause-Menü",
            ["PressKeyPrompt"] = "Taste drücken...",
            ["LanguageLabel"] = "Sprache:"
        };

        // 7. ITALIANO - it
        Translations["it"] = new Dictionary<string, string>(Translations["en"])
        {
            ["NavExpeditions"] = "SPEDIZIONI",
            ["NavGraphics"] = "GRAFICA E VRAM",
            ["NavDisplay"] = "SCHERMO E SCALATURA",
            ["NavControls"] = "CONTROLLI E GIOCO",
            ["NavTelemetry"] = "TELEMETRIA E LOG",
            ["NavQuit"] = "ESCI",
            ["PresetsLabel"] = "PREDEFINITI:",
            ["ActiveGpu"] = "GPU Attiva:",
            ["SelectExpedition"] = "SELEZIONA LA TUA SPEDIZIONE",
            ["Part1Title"] = "PARTE I",
            ["Part2Title"] = "PARTE II",
            ["Part3Title"] = "PARTE III",
            ["BtnPlay"] = "► GIOCA",
            ["BtnStarting"] = "AVVIO IN CORSO...",
            ["BtnStop"] = "FERMA",
            ["ReadyStatus"] = "Pronto: {0}",
            ["GraphicsHeader"] = "CONFIGURAZIONE GRAFICA E GESTIONE VRAM",
            ["GpuActiveLabel"] = "Scheda Video Attiva (GPU):",
            ["ApiLabel"] = "API Grafica di Presentazione:",
            ["VramCapLabel"] = "Limite Rigido VRAM:",
            ["TexQualityLabel"] = "Qualità Texture:",
            ["ShadowQualityLabel"] = "Qualità Ombre:",
            ["DynamicShadowsLabel"] = "Ombre Dinamiche:",
            ["ViewDistanceLabel"] = "Distanza Visiva (LOD):",
            ["ControlsHeader"] = "MAPPATURA CONTROLLI E CONFIGURAZIONE",
            ["GameDirLabel"] = "Cartella di Gioco (CUSA02344):",
            ["BtnBrowse"] = "SFOGLIA...",
            ["BtnResetControls"] = "RIPRISTINA PREDEFINITI",
            ["BtnSaveControls"] = "SALVA CONTROLLI",
            ["ActionMoveForward"] = "Avanti",
            ["ActionMoveBackward"] = "Indietro",
            ["ActionMoveLeft"] = "Sinistra",
            ["ActionMoveRight"] = "Destra",
            ["ActionJump"] = "Salta / Arrampicati",
            ["ActionCover"] = "Riparo / Capriola",
            ["ActionMelee"] = "Corpo a corpo",
            ["ActionInteract"] = "Interagisci / Raccogli",
            ["ActionAim"] = "Mira",
            ["ActionFire"] = "Spara",
            ["ActionReload"] = "Ricarica",
            ["ActionGrenade"] = "Lancia granata",
            ["ActionSprint"] = "Scatto (Premi Levetta Sinistra)",
            ["ActionZoom"] = "Zoom (Premi Levetta Destra)",
            ["ActionJournal"] = "Diario di Drake",
            ["ActionPause"] = "Menu di Pausa",
            ["PressKeyPrompt"] = "Premi un tasto...",
            ["LanguageLabel"] = "Lingua:"
        };

        // 8. РУССКИЙ - ru
        Translations["ru"] = new Dictionary<string, string>(Translations["en"])
        {
            ["NavExpeditions"] = "ЭКСПЕДИЦИИ",
            ["NavGraphics"] = "ГРАФИКА И VRAM",
            ["NavDisplay"] = "ЭКРАН И МАСШТАБИРОВАНИЕ",
            ["NavControls"] = "УПРАВЛЕНИЕ И ИГРА",
            ["NavTelemetry"] = "ТЕЛЕМЕТРИЯ И ЛОГИ",
            ["NavQuit"] = "ВЫХОД",
            ["PresetsLabel"] = "ПРЕСЕТЫ:",
            ["ActiveGpu"] = "Активная GPU:",
            ["SelectExpedition"] = "ВЫБЕРИТЕ ЭКСПЕДИЦИЮ",
            ["Part1Title"] = "ЧАСТЬ I",
            ["Part2Title"] = "ЧАСТЬ II",
            ["Part3Title"] = "ЧАСТЬ III",
            ["BtnPlay"] = "► ИГРАТЬ",
            ["BtnStarting"] = "ЗАПУСК...",
            ["BtnStop"] = "ОСТАНОВИТЬ",
            ["ReadyStatus"] = "Готово: {0}",
            ["GraphicsHeader"] = "НАСТРОЙКА ГРАФИКИ И УПРАВЛЕНИЕ VRAM",
            ["GpuActiveLabel"] = "Активный видеоадаптер (GPU):",
            ["ApiLabel"] = "Графический API вывода:",
            ["VramCapLabel"] = "Строгий лимит VRAM:",
            ["TexQualityLabel"] = "Качество текстур:",
            ["ShadowQualityLabel"] = "Качество теней:",
            ["DynamicShadowsLabel"] = "Динамические тени:",
            ["ViewDistanceLabel"] = "Дальность прорисовки (LOD):",
            ["ControlsHeader"] = "НАЗНАЧЕНИЕ КЛАВИШ И УПРАВЛЕНИЕ",
            ["GameDirLabel"] = "Папка с файлами игры (CUSA02344):",
            ["BtnBrowse"] = "ОБЗОР...",
            ["BtnResetControls"] = "СБРОСИТЬ ПО УМОЛЧАНИЮ",
            ["BtnSaveControls"] = "СОХРАНИТЬ УПРАВЛЕНИЕ",
            ["ActionMoveForward"] = "Движение вперед",
            ["ActionMoveBackward"] = "Движение назад",
            ["ActionMoveLeft"] = "Движение влево",
            ["ActionMoveRight"] = "Движение вправо",
            ["ActionJump"] = "Прыжок / Карабканье",
            ["ActionCover"] = "Укрытие / Кувырок",
            ["ActionMelee"] = "Ближний бой",
            ["ActionInteract"] = "Действие / Подобрать",
            ["ActionAim"] = "Прицеливание",
            ["ActionFire"] = "Стрельба",
            ["ActionReload"] = "Перезарядка",
            ["ActionGrenade"] = "Бросок гранаты",
            ["ActionSprint"] = "Спринт (Нажатие L3)",
            ["ActionZoom"] = "Прицел / Зум (Нажатие R3)",
            ["ActionJournal"] = "Дневник Дрейка",
            ["ActionPause"] = "Меню паузы",
            ["PressKeyPrompt"] = "Нажмите клавишу...",
            ["LanguageLabel"] = "Язык:"
        };

        // 9. 日本語 - ja
        Translations["ja"] = new Dictionary<string, string>(Translations["en"])
        {
            ["NavExpeditions"] = "遠征 (EXPEDITIONS)",
            ["NavGraphics"] = "グラフィック & VRAM",
            ["NavDisplay"] = "画面 & スケーリング",
            ["NavControls"] = "操作 & コントローラー",
            ["NavTelemetry"] = "テレメトリ & ログ",
            ["NavQuit"] = "終了",
            ["PresetsLabel"] = "プリセット:",
            ["ActiveGpu"] = "使用中のGPU:",
            ["SelectExpedition"] = "遠征を選択してください",
            ["Part1Title"] = "第1部",
            ["Part1Sub"] = "エル・ドラドの秘宝",
            ["Part2Title"] = "第2部",
            ["Part2Sub"] = "黄金刀と消えた船団",
            ["Part3Title"] = "第3部",
            ["Part3Sub"] = "砂漠に眠るアトランティス",
            ["BtnPlay"] = "► プレイ開始",
            ["BtnStarting"] = "起動中...",
            ["BtnStop"] = "停止",
            ["ReadyStatus"] = "準備完了: {0}",
            ["GraphicsHeader"] = "グラフィックス設定およびVRAM管理",
            ["GpuActiveLabel"] = "アクティブグラフィックカード (GPU):",
            ["ApiLabel"] = "描画API:",
            ["VramCapLabel"] = "VRAM厳格制限 (Cap):",
            ["TexQualityLabel"] = "テクスチャ品質:",
            ["ShadowQualityLabel"] = "影の品質:",
            ["DynamicShadowsLabel"] = "動的シャドウ:",
            ["ViewDistanceLabel"] = "描画距離 (LOD):",
            ["ControlsHeader"] = "キー割り当ておよび入力設定",
            ["GameDirLabel"] = "ゲームデータフォルダ (CUSA02344):",
            ["BtnBrowse"] = "参照...",
            ["BtnResetControls"] = "初期設定に戻す",
            ["BtnSaveControls"] = "操作設定を保存",
            ["ActionMoveForward"] = "前進",
            ["ActionMoveBackward"] = "後退",
            ["ActionMoveLeft"] = "左移動",
            ["ActionMoveRight"] = "右移動",
            ["ActionJump"] = "ジャンプ / 登る",
            ["ActionCover"] = "カバー / ローリング",
            ["ActionMelee"] = "近接格闘",
            ["ActionInteract"] = "調べる / 拾う",
            ["ActionAim"] = "照準",
            ["ActionFire"] = "射撃",
            ["ActionReload"] = "リロード",
            ["ActionGrenade"] = "グレネード投擲",
            ["ActionSprint"] = "ダッシュ (左スティック押し込み)",
            ["ActionZoom"] = "ズーム (右スティック押し込み)",
            ["ActionJournal"] = "ドレイクの手帳",
            ["ActionPause"] = "ポーズメニュー",
            ["PressKeyPrompt"] = "キーを押してください...",
            ["LanguageLabel"] = "言語:"
        };

        // 10. 简体中文 - zh_CN
        Translations["zh_CN"] = new Dictionary<string, string>(Translations["en"])
        {
            ["NavExpeditions"] = "远征探险",
            ["NavGraphics"] = "图形与显存",
            ["NavDisplay"] = "显示与缩放",
            ["NavControls"] = "控制与按键",
            ["NavTelemetry"] = "实时日志与监控",
            ["NavQuit"] = "退出程序",
            ["PresetsLabel"] = "预设方案:",
            ["ActiveGpu"] = "当前GPU:",
            ["SelectExpedition"] = "请选择你的探险章节",
            ["Part1Title"] = "第一部",
            ["Part1Sub"] = "德雷克的宝藏",
            ["Part2Title"] = "第二部",
            ["Part2Sub"] = "纵横四海",
            ["Part3Title"] = "第三部",
            ["Part3Sub"] = "德雷克的诡计",
            ["BtnPlay"] = "► 开始游戏",
            ["BtnStarting"] = "正在启动...",
            ["BtnStop"] = "停止运行",
            ["ReadyStatus"] = "就绪: {0}",
            ["GraphicsHeader"] = "图形核心设置与显存分配管理",
            ["GpuActiveLabel"] = "活动显卡适配器 (GPU):",
            ["ApiLabel"] = "渲染呈现 API:",
            ["VramCapLabel"] = "严格显存上限 (Cap):",
            ["TexQualityLabel"] = "纹理材质质量:",
            ["ShadowQualityLabel"] = "阴影渲染质量:",
            ["DynamicShadowsLabel"] = "动态太阳阴影:",
            ["ViewDistanceLabel"] = "视距细节范围 (LOD):",
            ["ControlsHeader"] = "键鼠映射与手柄控制配置",
            ["GameDirLabel"] = "游戏数据安装目录 (CUSA02344):",
            ["BtnBrowse"] = "浏览路径...",
            ["BtnResetControls"] = "恢复默认按键",
            ["BtnSaveControls"] = "保存按键设置",
            ["ActionMoveForward"] = "向前移动",
            ["ActionMoveBackward"] = "向后移动",
            ["ActionMoveLeft"] = "向左移动",
            ["ActionMoveRight"] = "向右移动",
            ["ActionJump"] = "跳跃 / 攀爬",
            ["ActionCover"] = "掩体掩护 / 翻滚",
            ["ActionMelee"] = "近战肉搏",
            ["ActionInteract"] = "互动操作 / 拾取",
            ["ActionAim"] = "举枪瞄准",
            ["ActionFire"] = "开火射击",
            ["ActionReload"] = "装填弹药",
            ["ActionGrenade"] = "投掷手雷",
            ["ActionSprint"] = "疾跑冲刺 (左摇杆按下)",
            ["ActionZoom"] = "准星缩放 (右摇杆按下)",
            ["ActionJournal"] = "德雷克的日记",
            ["ActionPause"] = "暂停菜单",
            ["PressKeyPrompt"] = "请按下任意按键...",
            ["LanguageLabel"] = "界面语言:"
        };
    }
}
