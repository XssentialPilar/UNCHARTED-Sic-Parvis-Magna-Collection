#!/usr/bin/env python3
"""Uncharted: Sic Parvis Magna Collection - Explorer's Desk Launcher (Windows / Wine, Stdlib Tkinter).

Select an expedition journal (Part I, Part II, Part III), configure DirectX 11 / DirectX 12 / Vulkan
presentation, strict VRAM budgets (1 GB / 2 GB / 4 GB / 8 GB+), hybrid sub-millisecond frame pacing,
display scaling, controls, and telemetry, and launch via tools/probe_uncharted.py.
"""
import ctypes
import json
import os
from pathlib import Path
import queue
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import tkinter as tk
import tkinter.font as tkfont
from tkinter import messagebox

try:
    from PIL import Image, ImageDraw, ImageTk
except ImportError:
    Image = ImageDraw = ImageTk = None

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import net_lan
except ImportError:
    net_lan = None
try:
    import updater
except ImportError:
    updater = None

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "launcher" / "assets"
DEFAULT_GAME_DIR = ROOT / "game" / "CUSA02344" / "uroot"
STARTER_EXE = ROOT / "Uncharted - Sic Parvis Magna Collection.exe"
USER_DIR = ROOT / "analysis" / "uncharted-user"
INI_FILE = USER_DIR / "probe.ini"
RUN_SCRIPT = ROOT / "tools" / "probe_uncharted.py"
PREFIX_CONFIG_FILE = Path(os.environ.get("APPDATA", Path.home())) / "Uncharted Launcher" / "launcher.json"
NET_GAMES_FILE = ROOT / "launcher" / "net_games.json"

DESK_W = 1280
DESK_H = 720


def wine_kernel32():
    """kernel32 when the launcher runs under Wine (Proton, Lutris, CrossOver), else None."""
    try:
        if hasattr(ctypes.windll.ntdll, "wine_get_version"):
            return ctypes.windll.kernel32
    except (AttributeError, OSError):
        pass
    return None


WINE = wine_kernel32()


def slow_drive_reason(path):
    """Reports USB, SD/MMC, removable drives and FAT/exFAT volumes that cause uncached I/O stalls."""
    if os.name != "nt" or WINE:
        return None
    drive = os.path.splitdrive(str(Path(path).resolve()))[0]
    if len(drive) != 2 or drive[1] != ":":
        return None
    kernel32 = ctypes.windll.kernel32
    reasons = []
    fs_name = ctypes.create_unicode_buffer(32)
    if kernel32.GetVolumeInformationW(drive + "\\", None, 0, None, None, None, fs_name, len(fs_name)):
        if fs_name.value.upper() in ("EXFAT", "FAT32", "FAT"):
            reasons.append(f"{fs_name.value}-formatted")
    if kernel32.GetDriveTypeW(drive + "\\") == 2:
        reasons.append("removable")
    kernel32.CreateFileW.restype = ctypes.c_void_p
    handle = kernel32.CreateFileW("\\\\.\\" + drive, 0, 3, None, 3, 0, None)
    if handle and handle != ctypes.c_void_p(-1).value:
        try:
            query = struct.pack("<II4x", 0, 0)
            out = ctypes.create_string_buffer(1024)
            returned = ctypes.c_ulong(0)
            if kernel32.DeviceIoControl(ctypes.c_void_p(handle), 0x2D1400, query, len(query), out,
                                        len(out), ctypes.byref(returned), None) and returned.value >= 32:
                bus = struct.unpack_from("<I", out.raw, 28)[0]
                names = {7: "USB", 12: "SD card", 13: "MMC card", 4: "FireWire"}
                if bus in names:
                    reasons.insert(0, names[bus])
        finally:
            kernel32.CloseHandle(ctypes.c_void_p(handle))
    if not reasons:
        return None
    return f"{drive} is a {', '.join(dict.fromkeys(reasons))} drive"


def _wine_string(pointer, wide):
    if not pointer:
        return None
    try:
        return ctypes.wstring_at(pointer) if wide else ctypes.string_at(pointer).decode("utf-8", "replace")
    finally:
        WINE.GetProcessHeap.restype = ctypes.c_void_p
        WINE.HeapFree.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p]
        WINE.HeapFree(WINE.GetProcessHeap(), 0, pointer)


def unix_path(path):
    if not WINE:
        return None
    function = WINE.wine_get_unix_file_name
    function.argtypes, function.restype = [ctypes.c_wchar_p], ctypes.c_void_p
    return _wine_string(function(str(path)), wide=False)


def windows_path(path):
    if not WINE or not path:
        return None
    function = WINE.wine_get_dos_file_name
    function.argtypes, function.restype = [ctypes.c_char_p], ctypes.c_void_p
    return _wine_string(function(path.encode("utf-8")), wide=True)


def shared_config_file():
    home = os.environ.get("HOME", "")
    data = os.environ.get("XDG_DATA_HOME") or (home + "/.local/share" if home.startswith("/") else "")
    folder = windows_path(data) if data.startswith("/") else None
    if not folder and os.environ.get("WINEHOMEDIR", "").startswith("\\??\\"):
        folder = str(Path(os.environ["WINEHOMEDIR"][4:]) / ".local" / "share")
    return Path(folder) / "uncharted-launcher" / "launcher.json" if folder else None


CONFIG_FILE = (WINE and shared_config_file()) or PREFIX_CONFIG_FILE
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
APP_TITLE = "Uncharted: Sic Parvis Magna Collection"
CREDITS = (
    "Credits: DLSS and DLAA use NVIDIA DLSS technology. NVIDIA, RTX and DLSS are trademarks of "
    "NVIDIA Corporation. AMD FidelityFX Super Resolution (FSR 1, FSR 3.1, FSR 4) by AMD. "
    "Uncharted camera motion vectors & architecture unified for Sic Parvis Magna Collection."
)


def multiplayer_preview():
    return os.environ.get("UNCHARTED_MULTIPLAYER") == "1" or (USER_DIR / "multiplayer-preview").exists()


NET_CHARACTERS = [
    ("", "Default (Drake)"), ("hero-default", "Drake"), ("hero-parka", "Drake (winter)"),
    ("mp-hero-chloe-variation", "Chloe"), ("mp-hero-elena", "Elena"), ("mp-hero-elena-divesuit", "Elena (dive suit)"),
    ("mp-hero-divesuit", "Drake (dive suit)"), ("mp-hero-sullivan-blue", "Sully (blue)"),
    ("mp-hero-sullivan-red", "Sully (red)"), ("mp-hero-sullivan-winter", "Sully (winter)"),
    ("mp-hero-eddie", "Eddy Raja"), ("mp-hero-navarro", "Navarro"), ("mp-hero-roman", "Roman"),
    ("mp-hero-red", "Harry Flynn"), ("mp-hero-pirate", "Pirate"), ("mp-hero-merc", "Mercenary"),
    ("mp-hero-udf", "UDF soldier"), ("mp-hero-skeleton-1-glow", "Skeleton (glow)"),
    ("mp-hero-skeleton-1-platinum", "Skeleton (platinum)"),
]


def net_characters(game_dir):
    actors = Path(game_dir) / "u2data" / "build" / "main" / "actor27" / "ps4" if game_dir else None
    if not actors or not actors.is_dir():
        return list(NET_CHARACTERS)
    return [(a, label) for a, label in NET_CHARACTERS if not a or (actors / (a + ".pak")).exists()]


def net_games():
    games = []
    try:
        data = json.loads(NET_GAMES_FILE.read_text(encoding="utf-8"))
        for entry in data.get("games", []) if isinstance(data, dict) else data:
            if entry.get("complete") and isinstance(entry.get("index"), int):
                mode = {"coop": "Co-op"}.get(entry.get("kind"), (entry.get("mode") or "").title())
                label = " - ".join(part for part in (entry.get("map") or entry.get("name"), mode) if part)
                games.append({"index": entry["index"], "label": label or f"Net game {entry['index']}"})
    except (OSError, ValueError, AttributeError, TypeError):
        pass
    return games or [{"index": 114, "label": "The Village - Co-op"}]


GAMES = [
    {"key": "u1", "number": "I", "short": "I: Fortune", "title": "DRAKE'S FORTUNE",
     "name": "Uncharted: Drake's Fortune",
     "subtitle": "El Dorado Expedition  •  1596 Sir Francis Drake Log",
     "exe": "eboot.bin", "log": "uncharted", "cx": 568, "cy": 348},
    {"key": "u2", "number": "II", "short": "II: Thieves", "title": "AMONG THIEVES",
     "name": "Uncharted 2: Among Thieves",
     "subtitle": "Shambhala Expedition  •  1292 Marco Polo Fleet",
     "exe": "big2-ps4_Shipping.elf", "log": "big2-ps4_Shipping", "cx": 796, "cy": 348},
    {"key": "u3", "number": "III", "short": "III: Deception", "title": "DRAKE'S DECEPTION",
     "name": "Uncharted 3: Drake's Deception",
     "subtitle": "Iram of the Pillars  •  Rub' al Khali Crossing",
     "exe": "big3-ps4_Shipping.elf", "log": "big3-ps4_Shipping", "cx": 1024, "cy": 348},
]

# Palette: Lantern-Lit 16th-Century Explorer's Desk (Warm Walnut Obsidian & Francis Drake Gold)
BG = "#16110d"          # Matches interior of spm_panel_glass.png seamlessly
WINDOW_BG = "#0b0806"   # Outer canvas matte
HEADER_BG = "#231a12"   # Warm bronze header strip inside settings panel
PANEL = "#221a13"       # Control well background
PANEL_HI = "#33261a"    # Hovered control well
LINE = "#4e3b27"        # Antique brass control trim
TEXT = "#f5ebd6"        # Crisp warm parchment ivory
MUTED = "#bfa987"       # Aged map brass-tan
DIM = "#8a7659"         # Muted leather bronze
GOLD = "#d8ad48"        # Francis Drake Royal Gold
GOLD_HI = "#f4cf72"     # Lantern-lit bright gold
NAV_IDLE = "#ccb072"    # Unselected left-menu serif gold
INK = "#120d08"         # Deep sepia ink on gold segments
STOP_RED = "#c24d3b"

MAX_FPS = 120
FPS_LIMITS = [(30, "30"), (40, "40"), (45, "45"), (50, "50"), (55, "55"), (60, "60"), (75, "75"),
              (90, "90"), (100, "100"), (MAX_FPS, str(MAX_FPS))]

GRAPHICS_APIS = [
    ("dx11", "DirectX 11"),
    ("dx12", "DirectX 12"),
    ("vulkan", "Vulkan 1.3"),
]

VRAM_CAPS = [
    ("0", "Auto (Native VRAM)"),
    ("820", "820 MB (1 GB)"),
    ("1720", "1720 MB (2 GB)"),
    ("3200", "3200 MB (4 GB)"),
    ("6144", "6144 MB (8 GB+)"),
]

FRAME_PACER_OPTIONS = [
    ("hybrid", "Hybrid QPC"),
    ("timer", "OS Timer"),
    ("spin", "Spin-Lock"),
    ("off", "Off"),
]

DISTANCE_SCALES = [
    ("50", "50%"),
    ("75", "75%"),
    ("100", "100%"),
    ("125", "125%"),
    ("150", "150%"),
]

# Runtime options persisted in analysis/uncharted-user/probe-u*.ini (shared with C++ gow3_settings.cpp)
INI_DEFAULTS = {
    "graphics_api": "dx11",
    "vram_budget_mb": "0",
    "frame_pacer": "hybrid",
    "display_mode": "borderless",
    "vsync": "1",
    "fps_limit": "60",
    "frames_queued": "1",
    "async_shaders": "1",
    "deferred_readback": "1",
    "stale_readback": "0",
    "fsr1": "0",
    "rcas": "1",
    "rcas_strength": "75",
    "show_fps": "1",
    "upscaler": "off",
    "fade_distance": "100",
    "sun_shadow_distance": "100",
    "light_shadow_distance": "100",
    "overlay_fps": "1",
    "overlay_frametime": "1",
    "overlay_gpu": "1",
    "overlay_cpu": "1",
    "overlay_ram": "0",
    "overlay_vram": "1",
    "overlay_upscaler": "1",
    "overlay_corner": "0",
    "overlay_layout": "0",
    "overlay_scale": "100",
    "overlay_opacity": "50",
}

CHEATS = (
    ("invincible", "Invincible", "Nate takes no damage in all three games."),
    ("ammo", "Infinite ammo", "Weapon reserve ammo never runs out in all three games."),
    ("noreload", "No reload", "Weapon magazine never empties (Uncharted 2 & 3)."),
    ("grenades", "Infinite grenades", "Grenades never run out (Uncharted 3)."),
    ("onehit", "One-hit kills", "Enemies go down in a single hit (Uncharted 3)."),
)

LAUNCHER_DEFAULTS = {
    "game": "u1",
    "tab": "expeditions",
    "diagnostics": False,
    "game_dir": "",
    "check_updates": False,
    "language": "1",
    "unlock": False,
    "unlock_rewards": False,
    "graphics_api": "dx11",
    "vram_budget_mb": "0",
    "texture_quality": "medium",
    "shadow_quality": "medium",
    "anisotropy": "16",
    "hires_shadows": False,
    "full_res_textures": False,
    "ssao_quality": "full",
    "dof_mode": "enabled",
    "motion_blur_mode": "enabled",
    "frame_pacing_mode": "hybrid",
    "job_sleep": "auto",
    "low_vram": "auto",
    "torch_forward": True,
    "speakers": "auto",
    "u3_exposure": "0",
    "movie_stretch": False,
}

LANGUAGES = [
    ("1", "English (US)"), ("18", "English (UK)"), ("2", "Français"), ("4", "Deutsch"),
    ("5", "Italiano"), ("3", "Español (España)"), ("20", "Español (Latinoamérica)"),
    ("7", "Português (Portugal)"), ("17", "Português (Brasil)"), ("6", "Nederlands"),
    ("14", "Dansk"), ("15", "Norsk"), ("13", "Svenska"), ("12", "Suomi"), ("16", "Polski"),
    ("23", "Čeština"), ("25", "Ελληνικά"), ("8", "Русский"), ("19", "Türkçe"),
    ("0", "日本語"), ("9", "한국어"), ("10", "中文 (繁體)"),
]
SPEECH_PACKS = {
    "english": ("1", "18"), "french": ("2",), "german": ("4",), "italian": ("5",),
    "spanish": ("3", "20"), "portuguese": ("7", "17"), "dutch": ("6",), "danish": ("14",),
    "norwegian": ("15",), "swedish": ("13",), "finnish": ("12",), "polish": ("16",),
    "czech": ("23",), "greek": ("25",), "russian": ("8",), "turkish": ("19",),
    "japanese": ("0",), "korean": ("9",), "chinese": ("10",),
}


def voice_languages(game_dir):
    ids = set()
    for pattern in ("u*data/build/main/sound1/speech/*_ps4.psarc",
                    "u*data/build/main/sound1/streams/speech/*_ps4.psarc"):
        for pack in Path(game_dir).glob(pattern) if game_dir else ():
            ids.update(SPEECH_PACKS.get(pack.name.removesuffix("_ps4.psarc").lower(), ()))
    return ids


GAME_KEYS = ("u1", "u2", "u3")
GAME_SETTING_DEFAULTS = {
    "preset": "medium",
    "resolution": "1920x1080",
    "upscale": "off",
    "aa": "off",
    "aspect": "16:9",
    "multithreaded": True,
    "save_profile": "1",
    "async_readback": True,
    "controls": None,
    "pad_controls": None,
    "mouse": True,
    "mouse_sens": "1.0",
    "mouse_invert": False,
    "prompts": "auto",
    "mouse_aim_sens": "1.0",
    "mouse_vertical": "1.0",
    "mouse_invert_x": False,
    "vibration": True,
}

CONTROL_GROUPS = (
    ("MOVEMENT", (("ls_up", "Move forward"), ("ls_down", "Move back"), ("ls_left", "Move left"),
                  ("ls_right", "Move right"), ("rs_up", "Look up"), ("rs_down", "Look down"),
                  ("rs_left", "Look left"), ("rs_right", "Look right"))),
    ("WEAPONS (D-PAD)", (("left", "Left"), ("right", "Right"), ("up", "Up"), ("down", "Down"))),
    ("ACTIONS", (("cross", "Jump, climb"), ("circle", "Cover, roll, drop"),
                 ("square", "Melee"), ("triangle", "Interact, pick up"),
                 ("l2", "Aim"), ("r2", "Fire"), ("l1", "Grenade (hold)"),
                 ("r1", "Reload"), ("l3", "Left stick press"), ("r3", "Right stick press"),
                 ("options", "Pause menu"), ("touchpad_left", "Touch left"),
                 ("touchpad_right", "Touch right"))),
)

PAD_DEFAULTS = {
    "cross": "a", "circle": "b", "square": "x", "triangle": "y", "l1": "leftshoulder",
    "r1": "rightshoulder", "l3": "leftstick", "r3": "rightstick", "options": "start",
    "touchpad_right": "back", "up": "dpup", "down": "dpdown", "left": "dpleft", "right": "dpright",
}
PAD_FIXED = {
    "l2": "lefttrigger", "r2": "righttrigger", "touchpad_left": "leftshoulder+back",
    "ls_up": "lstick", "ls_down": "lstick", "ls_left": "lstick", "ls_right": "lstick",
    "rs_up": "rstick", "rs_down": "rstick", "rs_left": "rstick", "rs_right": "rstick",
}
SDL_BUTTONS = (
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
    "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2",
    "paddle3", "paddle4", "touchpad",
)
PAD_NAMES = {
    "a": ("A", "Cross"), "b": ("B", "Circle"), "x": ("X", "Square"), "y": ("Y", "Triangle"),
    "back": ("View", "Share"), "guide": ("Guide", "PS"), "start": ("Menu", "Options"),
    "leftstick": ("LS", "L3"), "rightstick": ("RS", "R3"), "leftshoulder": ("LB", "L1"),
    "rightshoulder": ("RB", "R1"), "lefttrigger": ("LT", "L2"), "righttrigger": ("RT", "R2"),
    "dpup": ("D-Up", "D-Up"), "dpdown": ("D-Down", "D-Down"),
    "dpleft": ("D-Left", "D-Left"), "dpright": ("D-Right", "D-Right"),
    "misc1": ("Share", "Mute"), "paddle1": ("P1", "P1"), "paddle2": ("P2", "P2"),
    "paddle3": ("P3", "P3"), "paddle4": ("P4", "P4"), "touchpad": ("Touch", "Touch"),
    "lstick": ("L-Stick", "L-Stick"), "rstick": ("R-Stick", "R-Stick"),
}


def pad_button_text(button, style):
    ps = 1 if style == "playstation" else 0
    if "+" in button:
        return " + ".join(PAD_NAMES.get(b, (b, b))[ps] for b in button.split("+"))
    return PAD_NAMES.get(button, (button, button))[ps]


def merged_pad_controls(pad_controls):
    merged = dict(PAD_DEFAULTS)
    for action, button in (pad_controls or {}).items():
        if action in merged:
            merged[action] = button or None
    return merged


def pad_map(pad_controls):
    merged = merged_pad_controls(pad_controls)
    if merged == PAD_DEFAULTS:
        return ""
    return ",".join(f"{action}={button or ''}" for action, button in merged.items())


def _pad_font(size):
    from PIL import ImageFont
    for name in ("segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def pad_icon(button, style, height):
    """Optional PIL controller icon generator when Pillow is available."""
    s = 4
    H = height * s
    ps = style == "playstation"
    body, rim, ink = (38, 29, 21, 255), (170, 138, 84, 255), (245, 235, 214, 255)
    if "+" in button:
        parts = [pad_icon(b, style, height) for b in button.split("+")]
        plus_w = height // 2
        img = Image.new("RGBA", (sum(i.width for i in parts) + plus_w * (len(parts) - 1), height))
        x = 0
        for i, part in enumerate(parts):
            img.alpha_composite(part, (x, 0))
            x += part.width
            if i + 1 < len(parts):
                d = ImageDraw.Draw(img)
                cx, cy, r = x + plus_w // 2, height // 2, max(2, height // 6)
                d.line((cx - r, cy, cx + r, cy), fill=ink, width=max(1, height // 12))
                d.line((cx, cy - r, cx, cy + r), fill=ink, width=max(1, height // 12))
                x += plus_w
        return img

    def canvas(width_factor):
        W = int(H * width_factor)
        return Image.new("RGBA", (W, H)), W

    def finish(img):
        return img.resize((max(1, img.width // s), height), Image.LANCZOS)

    def text(d, W, label, color, size_factor=0.55, dy=0):
        font = _pad_font(max(8, int(H * size_factor)))
        box = d.textbbox((0, 0), label, font=font)
        d.text(((W - (box[2] - box[0])) / 2 - box[0], (H - (box[3] - box[1])) / 2 - box[1] + dy), label,
               font=font, fill=color)

    line = max(2, H // 12)
    if button in ("a", "b", "x", "y"):
        img, W = canvas(1.0)
        d = ImageDraw.Draw(img)
        d.ellipse((line, line, W - line, H - line), fill=body, outline=rim, width=line)
        if ps:
            color = {"a": (124, 156, 255, 255), "b": (255, 102, 102, 255), "x": (240, 140, 210, 255),
                     "y": (64, 214, 160, 255)}[button]
            c, r, w = W / 2, H * 0.22, int(H * 0.08)
            if button == "a":
                d.line((c - r, c - r, c + r, c + r), fill=color, width=w)
                d.line((c - r, c + r, c + r, c - r), fill=color, width=w)
            elif button == "b":
                d.ellipse((c - r, c - r, c + r, c + r), outline=color, width=w)
            elif button == "x":
                d.rectangle((c - r * 0.85, c - r * 0.85, c + r * 0.85, c + r * 0.85), outline=color, width=w)
            else:
                d.polygon([(c, c - r * 1.05), (c - r * 1.1, c + r * 0.75), (c + r * 1.1, c + r * 0.75)],
                          outline=color, width=w)
        else:
            color = {"a": (108, 194, 74, 255), "b": (229, 71, 59, 255), "x": (63, 142, 230, 255),
                     "y": (244, 196, 48, 255)}[button]
            text(d, W, button.upper(), color, 0.6)
        return finish(img)
    label = pad_button_text(button, style)
    img, W = canvas(max(1.15, 0.38 * len(label)))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((line, H * 0.14, W - line, H * 0.86), radius=H * 0.25, fill=body, outline=rim, width=line)
    text(d, W, label, ink, 0.42)
    return finish(img)


class Gamepads:
    """Controllers through SDL3 (gow3_pc/out/SDL3.dll or MSYS2)."""
    PLAYSTATION_TYPES = (4, 5, 6)

    def __init__(self):
        self.sdl = None
        self.pads = {}
        msys = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))
        codex_msys = Path.home() / ".codex/tmp/uncharted-build/msys64/clang64/bin/SDL3.dll"
        for path in (ROOT / "gow3_pc/out/SDL3.dll", codex_msys, msys / "clang64/bin/SDL3.dll",
                     ROOT.parent / "msys64/clang64/bin/SDL3.dll"):
            if not path.is_file():
                continue
            try:
                sdl = ctypes.CDLL(str(path))
            except OSError:
                continue
            sdl.SDL_SetHint.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
            sdl.SDL_SetHint.restype = ctypes.c_bool
            sdl.SDL_Init.argtypes = [ctypes.c_uint32]
            sdl.SDL_Init.restype = ctypes.c_bool
            sdl.SDL_GetGamepads.argtypes = [ctypes.POINTER(ctypes.c_int)]
            sdl.SDL_GetGamepads.restype = ctypes.POINTER(ctypes.c_uint32)
            sdl.SDL_OpenGamepad.argtypes = [ctypes.c_uint32]
            sdl.SDL_OpenGamepad.restype = ctypes.c_void_p
            sdl.SDL_CloseGamepad.argtypes = [ctypes.c_void_p]
            sdl.SDL_GetGamepadButton.argtypes = [ctypes.c_void_p, ctypes.c_int]
            sdl.SDL_GetGamepadButton.restype = ctypes.c_bool
            sdl.SDL_GetGamepadAxis.argtypes = [ctypes.c_void_p, ctypes.c_int]
            sdl.SDL_GetGamepadAxis.restype = ctypes.c_int16
            sdl.SDL_GetGamepadType.argtypes = [ctypes.c_void_p]
            sdl.SDL_GetGamepadType.restype = ctypes.c_int
            sdl.SDL_free.argtypes = [ctypes.c_void_p]
            sdl.SDL_SetHint(b"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", b"1")
            if sdl.SDL_Init(0x2000):
                self.sdl = sdl
            break

    def refresh(self):
        if not self.sdl:
            return []
        self.sdl.SDL_PumpEvents()
        count = ctypes.c_int(0)
        ids = self.sdl.SDL_GetGamepads(ctypes.byref(count))
        current = [ids[i] for i in range(count.value)] if ids else []
        if ids:
            self.sdl.SDL_free(ctypes.cast(ids, ctypes.c_void_p))
        for jid in current:
            if jid not in self.pads:
                pad = self.sdl.SDL_OpenGamepad(jid)
                if pad:
                    self.pads[jid] = pad
        for jid in [j for j in self.pads if j not in current]:
            self.sdl.SDL_CloseGamepad(self.pads.pop(jid))
        self.sdl.SDL_UpdateGamepads()
        return list(self.pads.values())

    def held(self):
        down = set()
        for pad in self.refresh():
            for index, name in enumerate(SDL_BUTTONS):
                if self.sdl.SDL_GetGamepadButton(pad, index):
                    down.add(name)
            for axis, name in ((4, "lefttrigger"), (5, "righttrigger")):
                if self.sdl.SDL_GetGamepadAxis(pad, axis) > 16000:
                    down.add(name)
        return down

    def style(self):
        for pad in self.refresh():
            return "playstation" if self.sdl.SDL_GetGamepadType(pad) in self.PLAYSTATION_TYPES else "xbox"
        return None


STICK_ACTIONS = {"ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right"}
DEFAULT_CONTROLS = {
    "ls_up": ["key:W"], "ls_down": ["key:S"], "ls_left": ["key:A"], "ls_right": ["key:D"],
    "rs_up": ["key:Up"], "rs_down": ["key:Down"], "rs_left": ["key:Left"], "rs_right": ["key:Right"],
    "left": ["key:1", "mouse:wheel_down"], "right": ["key:2", "mouse:wheel_up"], "up": ["key:3"],
    "down": ["key:4"],
    "cross": ["key:Space"], "circle": ["key:Left Ctrl"], "square": ["key:F"], "triangle": ["key:R"],
    "l2": ["mouse:right"], "r2": ["mouse:left"], "l1": ["key:Q", "mouse:x1"], "r1": ["key:E", "mouse:x2"],
    "l3": ["key:Left Shift"], "r3": ["key:V", "mouse:middle"], "options": ["key:Escape", "key:Return"],
    "touchpad_left": ["key:Tab"], "touchpad_right": ["key:Backspace"],
}
PROMPT_MODES = [("auto", "Auto"), ("keyboard", "Keyboard"), ("playstation", "PlayStation"), ("xbox", "Xbox")]
MOUSE_SENSITIVITIES = [("0.5", "0.5x"), ("0.75", "0.75x"), ("1.0", "1x"), ("1.5", "1.5x"), ("2.0", "2x"),
                       ("3.0", "3x"), ("4.0", "4x"), ("5.0", "5x")]
MOUSE_FACTORS = [("0.5", "0.5x"), ("0.75", "0.75x"), ("1.0", "1x"), ("1.25", "1.25x"), ("1.5", "1.5x"),
                 ("2.0", "2x")]
MOUSE_AIM_FACTORS = [("0.5", "0.5x"), ("1.0", "1x"), ("2.0", "2x"), ("3.0", "3x"), ("5.0", "5x"),
                     ("10.0", "10x")]
MOUSE_LABELS = {"left": "Left mouse", "middle": "Middle mouse", "right": "Right mouse", "x1": "Mouse back",
                "x2": "Mouse forward", "wheel_up": "Wheel up", "wheel_down": "Wheel down"}
TK_TO_SDL = {
    "space": "Space", "Return": "Return", "Escape": "Escape", "Tab": "Tab", "BackSpace": "Backspace",
    "Shift_L": "Left Shift", "Shift_R": "Right Shift", "Control_L": "Left Ctrl", "Control_R": "Right Ctrl",
    "Alt_L": "Left Alt", "Alt_R": "Right Alt", "Caps_Lock": "CapsLock", "Up": "Up", "Down": "Down",
    "Left": "Left", "Right": "Right", "Delete": "Delete", "Home": "Home", "End": "End", "Prior": "PageUp",
    "Next": "PageDown", "minus": "-", "equal": "=", "bracketleft": "[", "bracketright": "]",
    "backslash": "\\", "semicolon": ";", "apostrophe": "'", "grave": "`", "period": ".", "slash": "/",
    "KP_Divide": "Keypad /", "KP_Multiply": "Keypad *", "KP_Subtract": "Keypad -", "KP_Add": "Keypad +",
    "KP_Enter": "Keypad Enter", "KP_Decimal": "Keypad .", "Pause": "Pause", "Scroll_Lock": "ScrollLock",
}
TK_TO_SDL.update({f"F{n}": f"F{n}" for n in range(1, 13)})
TK_TO_SDL.update({f"KP_{n}": f"Keypad {n}" for n in range(10)})
RESERVED_KEYS = {"Insert", "F9", "comma"}


def merged_controls(controls):
    merged = {action: list(bindings) for action, bindings in DEFAULT_CONTROLS.items()}
    for action, bindings in (controls or {}).items():
        if action in merged:
            merged[action] = list(bindings)[:2]
    return merged


def control_maps(controls):
    keys, mouse = [], []
    for action, bindings in merged_controls(controls).items():
        names = [b[4:] for b in bindings if b.startswith("key:")]
        keys += [f"{action}={name}" for name in names] or [f"{action}="]
        if action not in STICK_ACTIONS:
            buttons = [b[6:] for b in bindings if b.startswith("mouse:")]
            mouse += [f"{action}={name}" for name in buttons] or [f"{action}="]
    return ",".join(keys), ",".join(mouse)


def parse_map(spec):
    entries = []
    for entry in (spec or "").split(","):
        action, eq, name = entry.partition("=")
        if eq:
            entries.append((action.strip(), name.strip()))
    return entries


def apply_map(bindings, spec, prefix):
    replaced = set()
    for action, name in parse_map(spec):
        if action not in bindings:
            continue
        if action not in replaced:
            replaced.add(action)
            bindings[action] = [b for b in bindings[action] if not b.startswith(prefix)]
        if name:
            bindings[action].append(prefix + name)


def sensitivity_value(text):
    try:
        value = float(text)
    except (TypeError, ValueError):
        return "1.0"
    for option, _ in MOUSE_SENSITIVITIES:
        if abs(float(option) - value) < 0.005:
            return option
    return f"{value:g}"


def factor_value(text, options):
    try:
        value = float(text)
    except (TypeError, ValueError):
        return "1.0"
    for option, _ in options:
        if abs(float(option) - value) < 0.005:
            return option
    return f"{value:g}"


def controls_to_ini(settings):
    keys, mouse = control_maps(settings.get("controls"))
    return {
        "key_map": keys,
        "mouse_map": mouse,
        "pad_map": pad_map(settings.get("pad_controls")),
        "mouse": "1" if settings.get("mouse", True) else "0",
        "mouse_sens": settings.get("mouse_sens", "1.0"),
        "mouse_invert": "1" if settings.get("mouse_invert") else "0",
        "prompts": settings.get("prompts", "auto"),
        "mouse_aim_sens": settings.get("mouse_aim_sens", "1.0"),
        "mouse_vertical": settings.get("mouse_vertical", "1.0"),
        "mouse_invert_x": "1" if settings.get("mouse_invert_x") else "0",
        "vibration": "1" if settings.get("vibration", True) else "0",
    }


def controls_from_ini(values, settings):
    if not any(key in values for key in ("key_map", "mouse_map", "pad_map")):
        return False
    controls = {action: list(bindings) for action, bindings in DEFAULT_CONTROLS.items()}
    apply_map(controls, values.get("key_map"), "key:")
    apply_map(controls, values.get("mouse_map"), "mouse:")
    settings["controls"] = {action: bindings[:2] for action, bindings in controls.items()}
    pad = dict(PAD_DEFAULTS)
    replaced = set()
    for action, name in parse_map(values.get("pad_map")):
        if action in pad and action not in replaced:
            replaced.add(action)
            pad[action] = name or None
    settings["pad_controls"] = {
        action: button for action, button in pad.items() if button != PAD_DEFAULTS.get(action)
    }
    if "mouse" in values:
        settings["mouse"] = values["mouse"] != "0"
    if "mouse_sens" in values:
        settings["mouse_sens"] = sensitivity_value(values["mouse_sens"])
    if "mouse_invert" in values:
        settings["mouse_invert"] = values["mouse_invert"] == "1"
    for key, options in (("mouse_aim_sens", MOUSE_AIM_FACTORS), ("mouse_vertical", MOUSE_FACTORS)):
        if key in values:
            settings[key] = factor_value(values[key], options)
    if "mouse_invert_x" in values:
        settings["mouse_invert_x"] = values["mouse_invert_x"] == "1"
    if "vibration" in values:
        settings["vibration"] = values["vibration"] == "1"
    if "prompts" in values:
        settings["prompts"] = values["prompts"] if values["prompts"] in dict(PROMPT_MODES) else "auto"
    return True


def binding_label(binding):
    if not binding:
        return "—"
    kind, _, name = binding.partition(":")
    return MOUSE_LABELS.get(name, name) if kind == "mouse" else name


def is_game_dir(folder):
    return bool(folder) and (Path(folder) / "eboot.bin").is_file() and \
        (Path(folder) / "sce_sys" / "param.sfo").is_file()


UPDATE_SUFFIXES = ("-patch", "-update", "_patch", "_update")


def game_update_dir(folder):
    folder = Path(folder)
    title = folder.parent if folder.name.lower() == "uroot" else folder
    names = {title.name.lower() + suffix for suffix in UPDATE_SUFFIXES}
    try:
        for entry in title.parent.iterdir():
            if entry.name.lower() in names:
                for candidate in (entry / "uroot", entry):
                    if is_game_dir(candidate):
                        return candidate
    except OSError:
        pass
    return None


UPDATE_102_CODE = bytes.fromhex("488d05cc9fd500 8b4850 84c9 7440 8a50".replace(" ", ""))
UPDATE_100_CODE = bytes.fromhex(
    "c4e37904c900c5f0c2c900c5f850c183f80f7438e8a8d71a004885c07427488d3d4c9c8300"
    "488d354b998300488d155b9c8300488d0d7e99830041b84d0400004531c9ffd0cd41"
)
_update_102_cache = {}


def _executable_codes(folder):
    path = Path(folder) / "big2-ps4_Shipping.elf"
    try:
        key = (str(path), path.stat().st_mtime_ns)
    except OSError:
        return False, False
    if key not in _update_102_cache:
        try:
            data = path.read_bytes()
            _update_102_cache[key] = (UPDATE_102_CODE in data, UPDATE_100_CODE in data)
        except OSError:
            _update_102_cache[key] = (False, False)
    return _update_102_cache[key]


def has_update_102_code(folder):
    return _executable_codes(folder)[0]


def has_update_100_code(folder):
    return _executable_codes(folder)[1]


def app_version(folder):
    try:
        data = (Path(folder) / "sce_sys" / "param.sfo").read_bytes()
        _, _, keys, values, count = struct.unpack_from("<IIIII", data)
        for i in range(count):
            key_at, _, length, _, value_at = struct.unpack_from("<HHIII", data, 20 + i * 16)
            key = data[keys + key_at:data.index(bytes(1), keys + key_at)].decode()
            if key == "APP_VER":
                version = data[values + value_at:values + value_at + length].rstrip(bytes(1)).decode()
                if version < "01.02" and has_update_102_code(folder):
                    return "01.02"
                if version >= "01.02" and has_update_100_code(folder):
                    return "01.00"
                return version
    except (OSError, ValueError, struct.error):
        pass
    return None


def dark_title_bar(window):
    if os.name != "nt" or WINE:
        return
    try:
        hwnd = ctypes.windll.user32.GetParent(window.winfo_id()) or window.winfo_id()
        value = ctypes.c_int(1)
        for attribute in (20, 19):
            if ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(value),
                                                          ctypes.sizeof(value)) == 0:
                break
    except (AttributeError, OSError):
        pass


def desktop_folder():
    try:
        from ctypes import wintypes

        class GUID(ctypes.Structure):
            _fields_ = [("Data1", wintypes.DWORD), ("Data2", wintypes.WORD), ("Data3", wintypes.WORD),
                        ("Data4", ctypes.c_ubyte * 8)]

        desktop_id = GUID(0xB4BFCC3A, 0xDB2C, 0x424C,
                          (ctypes.c_ubyte * 8)(0xB0, 0x29, 0x7F, 0xE9, 0x9A, 0x87, 0xC6, 0x41))
        found = ctypes.c_void_p()
        shell32 = ctypes.windll.shell32
        shell32.SHGetKnownFolderPath.argtypes = [ctypes.POINTER(GUID), wintypes.DWORD, wintypes.HANDLE,
                                                 ctypes.POINTER(ctypes.c_void_p)]
        if shell32.SHGetKnownFolderPath(ctypes.byref(desktop_id), 0, None, ctypes.byref(found)) == 0:
            try:
                folder = Path(ctypes.wstring_at(found.value))
            finally:
                ctypes.windll.ole32.CoTaskMemFree(found)
            if folder.is_dir():
                return folder
    except (AttributeError, OSError, ValueError):
        pass
    return Path(os.environ.get("USERPROFILE", Path.home())) / "Desktop"


def monitor_refresh_hz():
    try:
        user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
        dc = user32.GetDC(0)
        hz = gdi32.GetDeviceCaps(dc, 116)
        user32.ReleaseDC(0, dc)
        return hz if hz > 1 else 60
    except Exception:
        return 60


def fps_limit_value(value):
    listed = {str(v) for v, _ in FPS_LIMITS}
    if str(value) in listed:
        return str(value)
    try:
        number = int(value)
    except (TypeError, ValueError):
        return "60"
    if number == 0 or number > MAX_FPS:
        return str(MAX_FPS)
    return str(max(24, number))


RESOLUTIONS = [
    ("960x540", "540p"),
    ("1280x720", "720p"),
    ("1600x900", "900p"),
    ("1920x1080", "1080p"),
    ("2560x1440", "1440p"),
    ("2880x1620", "1620p"),
    ("3840x2160", "4K"),
]
ASPECTS = [("16:10", "16:10"), ("16:9", "16:9"), ("21:9", "21:9"), ("32:9", "32:9")]
ASPECT_WIDTHS = {"16:10": 16 / 10, "16:9": 16 / 9, "21:9": 64 / 27, "32:9": 32 / 9}
UPSCALE_PRESETS = [
    ("off", "Native AA"),
    ("quality", "Quality"),
    ("balanced", "Balanced"),
    ("performance", "Perf"),
    ("ultra", "Ultra Perf"),
]
UPSCALE_SCALES = {"quality": 1.5, "balanced": 1.7, "performance": 2.0, "ultra": 3.0}
TEMPORAL_UPSCALERS = ("fsr3", "fsr4", "fsr411", "dlss")


def upscaling_choice(ini):
    upscaler = ini.get("upscaler", INI_DEFAULTS["upscaler"])
    if upscaler in TEMPORAL_UPSCALERS:
        return upscaler
    return "fsr1" if ini.get("fsr1", "0") == "1" else "off"


def anti_aliasing_choice(ini, settings):
    upscaler = ini.get("upscaler", INI_DEFAULTS["upscaler"])
    if upscaler in ("off", "taa"):
        return upscaler
    return "taa" if settings.get("aa") == "taa" else "off"


def aspect_resolution(resolution, aspect):
    width, height = (int(v) for v in resolution.split("x"))
    if aspect not in ASPECT_WIDTHS or aspect == "16:9":
        return resolution
    if ASPECT_WIDTHS[aspect] < 16 / 9:
        height = int(round(width / ASPECT_WIDTHS[aspect] / 4)) * 4
    elif aspect == "21:9" and height in (1080, 1440, 1600):
        width = {1080: 2560, 1440: 3440, 1600: 3840}[height]
    else:
        width = int(round(height * ASPECT_WIDTHS[aspect] / 16)) * 16
    return f"{width}x{height}"


def game_sizes(settings, upscaling, vram_budget_mb="0"):
    output = aspect_resolution(settings["resolution"], settings.get("aspect", "16:9"))
    out_w, out_h = (int(v) for v in output.split("x"))
    scale = UPSCALE_SCALES.get(settings.get("upscale", "off"))
    if scale and (upscaling in TEMPORAL_UPSCALERS or upscaling == "fsr1"):
        rw = max(16, round(out_w / scale / 16) * 16)
        rh = max(4, round(out_h / scale / 4) * 4)
        render = f"{rw}x{rh}"
    else:
        rw, rh = out_w, out_h
        render = output

    # Mirror probe_uncharted.py's VRAM hard-cap internal resolution clamp for preview accuracy
    try:
        cap = int(vram_budget_mb or 0)
    except (TypeError, ValueError):
        cap = 0
    if 0 < cap <= 1024 and rh > 720:
        clamp_scale = 720.0 / rh
        rw = max(640, int(round((rw * clamp_scale) / 16.0)) * 16)
        rh = 720
        render = f"{rw}x{rh}"
    elif 1024 < cap <= 2048 and rh > 1080:
        clamp_scale = 1080.0 / rh
        rw = max(960, int(round((rw * clamp_scale) / 16.0)) * 16)
        rh = 1080
        render = f"{rw}x{rh}"

    if render != output:
        return render, output
    return output, None


def resolution_options(aspect):
    options = []
    for value, _ in RESOLUTIONS:
        height = int(aspect_resolution(value, aspect).split("x")[1])
        options.append((value, "4K" if height == 2160 else f"{height}p"))
    return options


PRESETS = {
    "igpu": {
        "title": "IGPU",
        "chip": "IGPU",
        "summary": "30–60 FPS  •  540–720p FSR 1 (820 MB Cap)",
        "resolution": "1920x1080",
        "upscale": "performance",
        "vram_budget_mb": "820",
        "texture_quality": "low",
        "shadow_quality": "low",
        "ssao_quality": "half",
        "full_res_textures": False,
        "hires_shadows": False,
        "values": {
            "fps_limit": "60", "frames_queued": "1", "vsync": "1", "async_shaders": "1",
            "fsr1": "1", "rcas": "1", "rcas_strength": "75", "upscaler": "off",
            "fade_distance": "75", "sun_shadow_distance": "75", "light_shadow_distance": "75",
        },
    },
    "low": {
        "title": "LOW (1 GB)",
        "chip": "LOW 1GB",
        "summary": "30–60 FPS  •  2015 Min (GTX 460 / HD 5770, 820 MB VRAM)",
        "resolution": "1280x720",
        "upscale": "off",
        "vram_budget_mb": "820",
        "texture_quality": "low",
        "shadow_quality": "low",
        "ssao_quality": "half",
        "full_res_textures": False,
        "hires_shadows": False,
        "values": {
            "fps_limit": "45", "frames_queued": "1", "vsync": "1", "async_shaders": "1",
            "fsr1": "0", "rcas": "1", "rcas_strength": "75", "upscaler": "off",
            "fade_distance": "75", "sun_shadow_distance": "75", "light_shadow_distance": "75",
        },
    },
    "medium": {
        "title": "MED (2 GB)",
        "chip": "MED 2GB",
        "summary": "60 FPS  •  2015 Rec PS4 1080p (GTX 750 Ti / HD 7850, 1720 MB)",
        "resolution": "1920x1080",
        "upscale": "off",
        "vram_budget_mb": "1720",
        "texture_quality": "medium",
        "shadow_quality": "medium",
        "ssao_quality": "full",
        "full_res_textures": False,
        "hires_shadows": False,
        "values": {
            "fps_limit": "60", "frames_queued": "1", "vsync": "1", "async_shaders": "1",
            "fsr1": "0", "rcas": "1", "rcas_strength": "75", "upscaler": "taa",
            "fade_distance": "100", "sun_shadow_distance": "100", "light_shadow_distance": "100",
        },
    },
    "high": {
        "title": "HIGH (4 GB)",
        "chip": "HIGH",
        "summary": "60 FPS  •  1440p Enhanced Shadows (3200 MB VRAM)",
        "resolution": "2560x1440",
        "upscale": "off",
        "vram_budget_mb": "3200",
        "texture_quality": "high",
        "shadow_quality": "high",
        "ssao_quality": "full",
        "full_res_textures": False,
        "hires_shadows": True,
        "values": {
            "fps_limit": "60", "frames_queued": "1", "vsync": "1", "async_shaders": "1",
            "fsr1": "0", "rcas": "1", "rcas_strength": "50", "upscaler": "taa",
            "fade_distance": "125", "sun_shadow_distance": "125", "light_shadow_distance": "125",
        },
    },
    "ultra": {
        "title": "ULTRA (8 GB+)",
        "chip": "ULTRA",
        "summary": "Up to 120 FPS  •  4K Uncompressed Mip0 & HBAO+ (6144 MB)",
        "resolution": "3840x2160",
        "upscale": "off",
        "vram_budget_mb": "6144",
        "texture_quality": "ultra",
        "shadow_quality": "ultra",
        "ssao_quality": "hbao",
        "full_res_textures": True,
        "hires_shadows": True,
        "values": {
            "fps_limit": str(MAX_FPS), "frames_queued": "1", "vsync": "1", "async_shaders": "1",
            "fsr1": "0", "rcas": "1", "rcas_strength": "50", "upscaler": "taa",
            "fade_distance": "150", "sun_shadow_distance": "150", "light_shadow_distance": "150",
        },
    },
}
PRESET_ORDER = ("igpu", "low", "medium", "high", "ultra")


def upscaling_options():
    options = [("off", "Off"), ("fsr1", "FSR 1"), ("fsr3", "FSR 3.1")]
    if (ROOT / "gow3_pc/fsr4_shaders/fsr4_model_v07_i8_native_1080_pre.spv").is_file():
        options.append(("fsr4", "FSR 4"))
    out = ROOT / "gow3_pc/out"
    if (out / "gow3_dlss.dll").is_file() and (out / "nvngx_dlss.dll").is_file():
        options.append(("dlss", "DLSS"))
    return options


def preset_values(name):
    return dict(PRESETS[name]["values"])


def gpu_log_info():
    logs = [ROOT / "analysis" / g["log"] / "boot.log" for g in GAMES]
    for log in sorted((log for log in logs if log.exists()), key=lambda p: p.stat().st_mtime, reverse=True):
        name, kinds = None, {}
        try:
            with open(log, encoding="utf-8", errors="replace") as lines:
                for number, line in enumerate(lines):
                    if line.startswith("GPU: using "):
                        name = line[len("GPU: using "):].strip()
                    elif line.startswith("GPU ") and ": " in line:
                        head, _, card = line.partition(": ")
                        if head[4:].strip().isdigit() and card.rstrip().endswith(")"):
                            model, _, kind = card.rstrip().rpartition(" (")
                            kinds[model] = kind[:-1]
                    if number > 400:
                        break
        except OSError:
            continue
        if name:
            kind = kinds.get(name)
            return {
                "name": name,
                "integrated": True if kind == "IntegratedGpu" else False if kind == "DiscreteGpu" else None,
            }
    return None


def gpu_names():
    info = gpu_log_info()
    if info:
        return [info["name"]]
    try:
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_VideoController).Name"],
            capture_output=True, text=True, timeout=15, creationflags=NO_WINDOW,
        )
        return [line.strip() for line in result.stdout.splitlines() if line.strip()]
    except (OSError, subprocess.SubprocessError):
        return []


INTEGRATED_AMD = re.compile(
    r"\bradeon\s*(?:610m|640m|660m|680m|740m|760m|780m|860m|880m|890m)\b"
    r"|\bradeon\s+graphics\b|\bradeon\s+vega\b|\bvega\s*(?:3|6|7|8|9|10|11)\b"
    r"|\bryzen\s+.*\bgraphics\b"
)
DISCRETE_ARC = re.compile(r"\barc\s*(?:pro\s*)?[ab]\d{2,3}")
STRIX_HALO = re.compile(r"\bradeon\s*80[4-6]0s\b")


def gpu_name_key(name):
    return " ".join((name or "").lower().replace("(tm)", " ").replace("(r)", " ").split())


def gpu_strix_halo(name):
    return bool(STRIX_HALO.search(gpu_name_key(name)))


def gpu_integrated(name):
    n = gpu_name_key(name)
    if not n or gpu_strix_halo(n) or "nvidia" in n or "geforce" in n:
        return False
    if "vega 56" in n or "vega 64" in n or "radeon vii" in n or re.search(r"\brx\s*\d", n):
        return False
    if "intel" in n:
        return not DISCRETE_ARC.search(n)
    return bool(INTEGRATED_AMD.search(n))


def igpu_class(name):
    n = gpu_name_key(name)
    if ("intel" in n and "arc" in n) or re.search(r"\b(880m|890m)\b", n):
        return "strong"
    if re.search(r"\b(680m|760m|780m|860m)\b", n) or ("intel" in n and "iris xe" in n):
        return "mainstream"
    if "intel" in n or "vega" in n or re.search(r"\b(610m|640m|660m|740m)\b", n) or "radeon graphics" in n:
        return "weak"
    return "mainstream"


def gpu_recommendation(name, integrated=None):
    n = gpu_name_key(name)
    if gpu_strix_halo(n):
        return gpu_tier(n)
    if integrated is None or DISCRETE_ARC.search(n) \
            or re.search(r"\b(?:nvidia|geforce|radeon vii)\b|\brx\s*(?:\d|vega)", n):
        integrated = gpu_integrated(n)
    return "igpu" if integrated else gpu_tier(name)


def gpu_tier(name):
    n = gpu_name_key(name)
    if gpu_strix_halo(n):
        return "medium"
    if gpu_integrated(n):
        return "igpu"
    nvidia = re.search(r"\b(rtx|gtx|gt|mx)\s*(\d{3,4})\s*(ti)?\s*(super)?", n)
    amd = re.search(r"\brx\s*(\d{3,4})\s*(xtx|xt|gre)?", n)
    amd_legacy = re.search(r"\b(?:radeon\s+)?(hd|r7|r9)\s*(\d{3,4})\s*(x2|x|boost|ghz)?", n)
    arc = re.search(r"\barc\s*([ab])(\d{3})", n)
    if nvidia:
        kind, number = nvidia.group(1), int(nvidia.group(2))
        ti, super_ = bool(nvidia.group(3)), bool(nvidia.group(4))
        if kind in ("gt", "mx"):
            tier = "low"
        elif kind == "gtx":
            if number == 1080 and ti:
                tier = "high"
            elif 1060 <= number < 1100 or 1660 <= number < 1700 or (number == 1650 and super_):
                tier = "medium"
            elif (number == 750 and ti) or number in (660, 670, 680, 760, 770, 780, 950, 960, 970, 980):
                tier = "medium"
            else:
                tier = "low"
        else:
            series, model = number // 100, number % 100
            if model >= 80:
                tier = "ultra" if series >= 30 else "high"
            elif model == 70:
                tier = "ultra" if series >= 40 and (ti or super_) else "high"
            elif model == 60:
                tier = "high" if ti or series >= 50 else "medium"
            else:
                tier = "medium"
    elif amd:
        number, suffix = int(amd.group(1)), amd.group(2) or ""
        if number >= 9000:
            tier = {7: "ultra", 6: "high"}.get((number % 100) // 10, "medium")
        elif number >= 1000:
            series, model = number // 1000, (number % 1000) // 100
            if model >= 9:
                tier = "ultra"
            elif model == 8:
                tier = "ultra" if suffix.startswith("xt") else "high"
            elif model == 7:
                tier = "medium" if series == 5 else "high"
            elif model == 6 or number == 5500:
                tier = "medium"
            else:
                tier = "low"
        else:
            tier = "medium" if number in (470, 480, 570, 580, 590) else "low"
    elif amd_legacy:
        series_tag, number = amd_legacy.group(1), int(amd_legacy.group(2))
        if series_tag == "hd":
            tier = "medium" if number >= 7850 else "low"
        elif series_tag == "r9":
            tier = "medium" if number >= 270 else "low"
        else:
            tier = "medium" if number >= 265 else "low"
    elif "vega 56" in n or "vega 64" in n:
        tier = "medium"
    elif "radeon vii" in n:
        tier = "high"
    elif arc:
        tier = "medium" if int(arc.group(2)) >= 500 else "low"
    elif "intel" in n or ("radeon" in n and "graphics" in n):
        tier = "low"
    else:
        return None
    if "laptop" in n or "max-q" in n or "mobile" in n or re.search(r"\d{3,4}m\b", n):
        tier = {"ultra": "high", "high": "medium"}.get(tier, tier)
    return tier


# ---------------------------------------------------------------------------------------------
# Settings & INI synchronization

def ini_file(game):
    return USER_DIR / f"probe-{game}.ini"


def ini_lines(game):
    for path in (ini_file(game), INI_FILE):
        try:
            return path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
    return ["# Uncharted: Sic Parvis Magna Collection runtime settings (Launcher & In-Game Insert Menu)"]


def normalize_frame_pacer(value):
    val = str(value or "hybrid").strip().lower()
    if val in ("hybrid", "timer", "spin", "off"):
        return val
    if val in ("vsync", "fast_sync"):
        return "hybrid"
    if val == "unlocked":
        return "off"
    return "hybrid"


def normalize_graphics_api(value):
    val = str(value or "dx11").strip().lower()
    if val in ("dx11", "d3d11"):
        return "dx11"
    if val in ("dx12", "d3d12"):
        return "dx12"
    if val in ("vulkan", "vk"):
        return "vulkan"
    return "dx11"


def normalize_vram_cap(value, tex_quality="medium"):
    val = str(value if value is not None else "").strip()
    if val in {"820", "1720", "3200", "6144", "0"}:
        return val
    return "0" 


def read_ini(game):
    values = dict(INI_DEFAULTS)
    for line in ini_lines(game):
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    values["fps_limit"] = fps_limit_value(values["fps_limit"])
    values["graphics_api"] = normalize_graphics_api(values.get("graphics_api"))
    values["frame_pacer"] = normalize_frame_pacer(values.get("frame_pacer"))
    return values


def ini_mtime(game):
    try:
        return ini_file(game).stat().st_mtime_ns
    except OSError:
        return None


def write_ini(game, updates):
    lines = ini_lines(game)
    done = set()
    for i, line in enumerate(lines):
        if "=" in line and not line.lstrip().startswith("#"):
            key = line.split("=", 1)[0].strip()
            if key in updates:
                lines[i] = f"{key}={updates[key]}"
                done.add(key)
    lines += [f"{key}={value}" for key, value in updates.items() if key not in done]
    path = ini_file(game)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def load_config_file(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except OSError:
        return {}
    except ValueError:
        try:
            shutil.copy2(path, path.with_name(path.name + ".bad"))
        except OSError:
            pass
        return {}


def read_config():
    config = dict(LAUNCHER_DEFAULTS)
    stored = load_config_file(CONFIG_FILE)
    if CONFIG_FILE != PREFIX_CONFIG_FILE:
        older = load_config_file(PREFIX_CONFIG_FILE)
        if older:
            games = {}
            for source in (older, stored):
                for game, entry in (source.get("games") if isinstance(source.get("games"), dict) else {}).items():
                    if isinstance(entry, dict):
                        games.setdefault(game, {}).update(entry)
            stored = {**older, **stored, "games": games}
    config.update(stored)

    if not isinstance(config.get("games"), dict):
        config["games"] = {}
    games = config["games"]
    resolutions = [r for r, _ in RESOLUTIONS]
    for game in GAME_KEYS:
        if not isinstance(games.get(game), dict):
            games[game] = {}
        entry = games[game]
        for name, default in GAME_SETTING_DEFAULTS.items():
            entry.setdefault(name, config.get(name, default))
        entry["preset"] = {"balanced": "medium"}.get(entry["preset"], entry["preset"])
        if entry["resolution"] not in resolutions:
            entry["resolution"] = "1920x1080"
        if entry.get("aspect") not in ASPECT_WIDTHS:
            entry["aspect"] = "16:9"

    config["graphics_api"] = normalize_graphics_api(config.get("graphics_api", "dx11"))
    config["frame_pacing_mode"] = normalize_frame_pacer(config.get("frame_pacing_mode", "hybrid"))
    config["vram_budget_mb"] = normalize_vram_cap(
        stored.get("vram_budget_mb"), config.get("texture_quality", "medium")
    )

    # Version 11 (Sic Parvis Magna Master Architecture Migration):
    # 1. Disable default diagnostics=True (which forced GOW3_CAPTURE_ALL_BUFFERS=1 and halved FPS).
    # 2. Enable multithreaded command recording across all three games (u1, u2, u3).
    # 3. Clear stale full_res_textures/hires_shadows when low/medium quality is active.
    # 4. Sync graphics_api, vram_budget_mb, frame_pacer, deferred_readback=1, stale_readback=0 to probe-u*.ini.
    if config.get("version", 1) < 11:
        config["diagnostics"] = False
        for game in GAME_KEYS:
            games[game]["multithreaded"] = True
        if config.get("texture_quality") in ("low", "medium"):
            config["full_res_textures"] = False
        if config.get("shadow_quality") in ("low", "medium"):
            config["hires_shadows"] = False
        for game in GAME_KEYS:
            write_ini(game, {
                "graphics_api": config["graphics_api"],
                "vram_budget_mb": config["vram_budget_mb"],
                "frame_pacer": config["frame_pacing_mode"],
                "deferred_readback": "1",
                "stale_readback": "0",
                "async_shaders": "1",
                "frames_queued": "1",
            })

    for name in GAME_SETTING_DEFAULTS:
        config.pop(name, None)
    config["version"] = 11

    folder = config.get("game_dir")
    if WINE and folder and not config.get("game_dir_unix") and Path(folder).exists():
        config["game_dir_unix"] = unix_path(folder)
    return config


def write_config(config):
    CONFIG_FILE.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(config, indent=2)
    temporary = CONFIG_FILE.with_name(f"{CONFIG_FILE.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(text, encoding="utf-8")
        os.replace(temporary, CONFIG_FILE)
    except OSError:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        try:
            CONFIG_FILE.write_text(text, encoding="utf-8")
        except OSError as error:
            print(f"Could not write {CONFIG_FILE}: {error}", file=sys.stderr, flush=True)


# ---------------------------------------------------------------------------------------------
# Explorer's Desk Custom Tkinter Widgets (Pixel-Exact Typography & Antique Gold Trim)

def round_rect(canvas, x1, y1, x2, y2, r, **kw):
    points = [x1 + r, y1, x2 - r, y1, x2, y1, x2, y1 + r, x2, y2 - r, x2, y2, x2 - r, y2,
              x1 + r, y2, x1, y2, x1, y2 - r, x1, y1 + r, x1, y1]
    return canvas.create_polygon(points, smooth=True, **kw)


class Toggle(tk.Canvas):
    def __init__(self, parent, value, command):
        w, h = 44, 22
        super().__init__(parent, width=w, height=h, bg=parent["bg"], highlightthickness=0, cursor="hand2")
        self.w, self.h, self.value, self.command = w, h, bool(value), command
        self.bind("<Button-1>", lambda e: self.set(not self.value, notify=True))
        self.draw()

    def set(self, value, notify=False):
        self.value = bool(value)
        self.draw()
        if notify:
            self.command(self.value)

    def draw(self):
        self.delete("all")
        track = GOLD if self.value else PANEL
        border = GOLD_HI if self.value else LINE
        round_rect(self, 1, 1, self.w - 1, self.h - 1, self.h // 2, fill=track, outline=border)
        d = self.h - 6
        x = self.w - d - 3 if self.value else 3
        self.create_oval(x, 3, x + d, 3 + d, fill=INK if self.value else MUTED, outline="")


class Chip(tk.Canvas):
    def __init__(self, parent, font, label, value, command):
        self.label, self.command = label, command
        self.value = bool(value)
        self.font = tkfont.Font(font=font)
        w = self.font.measure(label) + 18
        h = self.font.metrics("linespace") + 8
        super().__init__(parent, width=w, height=h, bg=parent["bg"], highlightthickness=0, cursor="hand2")
        self.w, self.h = w, h
        self.bind("<Button-1>", lambda e: self.set(not self.value, notify=True))
        self.draw()

    def set(self, value, notify=False):
        self.value = bool(value)
        self.draw()
        if notify:
            self.command(self.value)

    def draw(self):
        self.delete("all")
        fill = GOLD if self.value else PANEL
        round_rect(self, 1, 1, self.w - 1, self.h - 1, self.h // 2, fill=fill,
                   outline=GOLD_HI if self.value else LINE)
        self.create_text(self.w / 2, self.h / 2, text=self.label, font=self.font,
                         fill=INK if self.value else MUTED)


class Segmented(tk.Frame):
    def __init__(self, parent, options, value, command, font, recommended=None, pad_x=9):
        super().__init__(parent, bg=LINE, padx=1, pady=1)
        self.command, self.labels, self.value, self.recommended = command, {}, value, recommended
        self.enabled = True
        for option, label in options:
            item = tk.Label(self, text=self.text(option, label), font=font, padx=pad_x, pady=4,
                            cursor="hand2", bd=0)
            item.bind("<Button-1>", lambda e, v=option: self.enabled and self.set(v, notify=True))
            item.bind("<Enter>", lambda e, w=item, v=option: self.enabled and v != self.value and w.config(bg=PANEL_HI))
            item.bind("<Leave>", lambda e: self.restyle())
            self.labels[option] = item
        self.show(list(self.labels))
        self.restyle()

    def show(self, options):
        for item in self.labels.values():
            item.grid_forget()
        for i, option in enumerate(options):
            if option in self.labels:
                self.labels[option].grid(row=0, column=i, padx=(0 if i == 0 else 1, 0))

    def set_enabled(self, enabled):
        self.enabled = enabled
        for item in self.labels.values():
            item.config(cursor="hand2" if enabled else "arrow")
        self.restyle()

    def text(self, option, label):
        return f"{label} ★" if option == self.recommended else label

    def relabel(self, options):
        for option, label in options:
            if option in self.labels:
                self.labels[option].config(text=self.text(option, label))

    def set(self, value, notify=False):
        self.value = value
        self.restyle()
        if notify:
            self.command(value)

    def restyle(self):
        for option, item in self.labels.items():
            selected = option == self.value
            if self.enabled:
                item.config(bg=GOLD if selected else PANEL, fg=INK if selected else TEXT)
            else:
                item.config(bg=PANEL_HI if selected else PANEL, fg=DIM)


class Dropdown(tk.Label):
    def __init__(self, parent, options, value, command, font):
        super().__init__(parent, font=font, bg=PANEL, fg=TEXT, padx=10, pady=4, cursor="hand2", anchor="w",
                         highlightthickness=1, highlightbackground=LINE)
        self.options, self.command, self.font = options, command, font
        self.set(value)
        self.bind("<Button-1>", lambda e: self.open())
        self.bind("<Enter>", lambda e: self.config(bg=PANEL_HI))
        self.bind("<Leave>", lambda e: self.config(bg=PANEL))

    def set(self, value, notify=False):
        self.value = value
        label = next((label for option, label in self.options if option == value), str(value))
        self.config(text=f"{label}  \u25be")
        if notify:
            self.command(value)

    def open(self):
        menu = tk.Menu(self, tearoff=0, bg=PANEL, fg=TEXT, activebackground=GOLD, activeforeground=INK,
                       font=self.font, bd=1)
        for option, label in self.options:
            menu.add_command(label=("\u2713  " if option == self.value else "     ") + label,
                             command=lambda v=option: self.set(v, notify=True))
        menu.tk_popup(self.winfo_rootx(), self.winfo_rooty() + self.winfo_height())


class ScrollBar(tk.Canvas):
    def __init__(self, parent, target):
        super().__init__(parent, width=8, bg=parent["bg"], highlightthickness=0)
        self.target, self.first, self.last, self.hover, self.grab = target, 0.0, 1.0, False, None
        self.bind("<Configure>", lambda e: self.draw())
        self.bind("<Enter>", lambda e: self.highlight(True))
        self.bind("<Leave>", lambda e: self.highlight(False))
        self.bind("<Button-1>", self.press)
        self.bind("<B1-Motion>", self.drag)
        self.bind("<ButtonRelease-1>", self.release)

    def set(self, first, last):
        self.first, self.last = float(first), float(last)
        self.draw()

    def highlight(self, hover):
        self.hover = hover
        self.draw()

    def draw(self):
        self.delete("all")
        w, h = self.winfo_width(), self.winfo_height()
        if h <= 1 or self.last - self.first >= 1:
            return
        round_rect(self, 1, 1, w - 1, h - 1, 3, fill=PANEL, outline=PANEL)
        top = min(int(self.first * h), h - 24)
        bottom = min(h, max(int(self.last * h), top + 24))
        color = GOLD if self.hover or self.grab is not None else LINE
        round_rect(self, 1, top + 1, w - 1, bottom - 1, 3, fill=color, outline=color)

    def press(self, event):
        position = event.y / max(1, self.winfo_height())
        if self.first <= position <= self.last:
            self.grab = position - self.first
        else:
            self.grab = (self.last - self.first) / 2
            self.target.yview_moveto(position - self.grab)

    def drag(self, event):
        if self.grab is not None:
            self.target.yview_moveto(event.y / max(1, self.winfo_height()) - self.grab)

    def release(self, event):
        self.grab = None
        self.draw()


class Launcher:
    PANEL_X = 436
    PANEL_Y = 132
    PANEL_W = 796
    PANEL_H = 528

    PANEL_TITLES = {
        "graphics": ("GRAPHICS & VRAM ARCHITECTURE", "DirectX 11 / 12 / Vulkan  •  Strict VRAM Budgets  •  Shadows & LOD"),
        "display": ("DISPLAY, UPSCALING & PACING", "Output Resolution  •  FSR 1 / 3.1 / DLSS  •  Sub-ms Hybrid Frame Pacer"),
        "controls": ("INPUT, MOUSE & CONTROLLERS", "Raw Mouse Look  •  DualShock / DualSense / Xbox  •  Custom Bindings"),
        "telemetry": ("PERFORMANCE TELEMETRY & HUD", "Live API / FPS / Frametime / VRAM / Thermal Overlay  •  Insert Menu"),
        "extras": ("EXPEDITION TOOLS, FILES & EXTRAS", "Game Folder & 1.02 Update  •  Profiles & Languages  •  Steam & Cheats"),
        "multiplayer": ("UNCHARTED 2 LAN MULTIPLAYER", "Host or Join Co-op & Competitive Matches Across Local Network"),
    }

    def __init__(self, root, direct=None):
        self.root = root
        self.direct = direct
        self.config = read_config()
        if self.config.get("game") not in GAME_KEYS:
            self.config["game"] = "u1"
        for key in GAME_KEYS:
            if not controls_from_ini(read_ini(key), self.config["games"][key]):
                write_ini(key, controls_to_ini(self.config["games"][key]))
        self.reload_ini()
        self.sync_global_to_all_inis()

        self.process = None
        self.partner = None
        self.partner_start = None
        self.stopping = False
        self.output = queue.Queue()
        self.last_output = 0.0
        self.game_started = False
        self.widgets = {}
        self.images = {}
        self.pad_images = {}
        self.capturing = None
        self.pad_capture = None
        self.hovered_nav = None
        self.hovered_journal = None
        self.hovered_play = False
        self.hovered_preset_chip = None
        self.nav_pad_prev = set()
        self.gamepads = Gamepads()

        self.update_release = None
        self.update_busy = False
        self.update_result = None

        self.gpu_name = self.config.get("gpu")
        self.recommended = gpu_recommendation(self.gpu_name)
        self.fonts()

        root.title(APP_TITLE)
        root.configure(bg=WINDOW_BG)
        root.geometry(f"{DESK_W}x{DESK_H}")
        root.minsize(DESK_W, DESK_H)
        root.resizable(False, False)
        try:
            root.iconbitmap(default=str(ASSETS / "uncharted.ico"))
        except tk.TclError:
            pass

        self.load_assets()
        self.build()
        root.update_idletasks()
        dark_title_bar(root)
        self.restore_window_position()
        write_config(self.config)

        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(120, self.poll)

        if updater is not None:
            root.after(3000, lambda: updater.cleanup(ROOT))
            if self.config.get("check_updates", False) and not direct:
                root.after(1500, self.check_updates)
        if not direct:
            self.detect_gpu()
        if direct:
            self.select_game(direct, save=False)
            root.after(100, self.play_direct)

    def fonts(self):
        # Negative sizes specify exact pixel heights in Tk so 100% / 125% / 150% DPI never shifts layout!
        self.f_nav = ("Georgia", -18)
        self.f_nav_active = ("Georgia", -18, "bold")
        self.f_panel_title = ("Georgia", -16, "bold")
        self.f_card_title = ("Georgia", -14, "bold")
        self.f_body = ("Segoe UI", -12)
        self.f_body_bold = ("Segoe UI Semibold", -12)
        self.f_small = ("Segoe UI", -11)
        self.f_small_bold = ("Segoe UI Semibold", -11)
        self.f_caps = ("Segoe UI Semibold", -10)
        self.f_play = ("Georgia", -15, "bold")
        self.f_footer = ("Georgia", -12)

    def load_assets(self):
        names = (
            "spm_desk_bg",
            "spm_panel_glass",
            "spm_journal_u1", "spm_journal_u1_active",
            "spm_journal_u2", "spm_journal_u2_active",
            "spm_journal_u3", "spm_journal_u3_active",
        )
        for name in names:
            path = ASSETS / f"{name}.png"
            if path.is_file():
                try:
                    self.images[name] = tk.PhotoImage(file=str(path))
                except tk.TclError:
                    pass

    def restore_window_position(self):
        position = self.config.get("window_xy")
        sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        if isinstance(position, list) and len(position) == 2:
            x, y = int(position[0]), int(position[1])
            if -40 <= x <= max(0, sw - 300) and 0 <= y <= max(0, sh - 200):
                y = min(y, max(0, sh - DESK_H - 48))
                self.root.geometry(f"{DESK_W}x{DESK_H}+{x}+{y}")
                return
        cx = max(0, (sw - DESK_W) // 2)
        cy = max(0, (sh - DESK_H - 40) // 2)
        self.root.geometry(f"{DESK_W}x{DESK_H}+{cx}+{cy}")

    def detect_gpu(self):
        found = []
        threading.Thread(target=lambda: found.append((gpu_log_info(), gpu_names())), daemon=True).start()

        def done():
            if not found:
                self.root.after(200, done)
                return
            info, names = found[0]
            if not names:
                return
            known = [name for name in names if gpu_tier(name)]
            name = max(known, key=lambda n: PRESET_ORDER.index(gpu_tier(n))) if known else names[0]
            integrated = info.get("integrated") if info and info.get("name") == name else None
            self.gpu_name, self.recommended = name, gpu_recommendation(name, integrated)
            if self.config.get("gpu") != name:
                self.config["gpu"] = name
                write_config(self.config)
            self.draw_expeditions_hud()
            if hasattr(self, "gpu_banner_label"):
                rec = PRESETS.get(self.recommended, {}).get("title", "MED (2 GB)")
                self.gpu_banner_label.config(
                    text=f"Detected GPU: {self.gpu_name}   •   Recommended Tier: {rec}"
                )
        self.root.after(200, done)

    def sync_global_to_all_inis(self):
        shared = {
            "graphics_api": normalize_graphics_api(self.config.get("graphics_api", "dx11")),
            "vram_budget_mb": normalize_vram_cap(
                self.config.get("vram_budget_mb"), self.config.get("texture_quality", "medium")
            ),
            "frame_pacer": normalize_frame_pacer(self.config.get("frame_pacing_mode", "hybrid")),
            "deferred_readback": "1",
            "stale_readback": "0",
        }
        for game in GAME_KEYS:
            write_ini(game, shared)
        self.ini.update(shared)
        self.ini_seen = ini_mtime(self.config["game"])

        engine_cfg_content = (
            "# Uncharted Global Engine Configuration (Sic Parvis Magna Collection)\n"
            f"graphics_api={shared['graphics_api']}\n"
            f"vram_budget_mb={shared['vram_budget_mb']}\n"
            f"frame_pacer={shared['frame_pacer']}\n"
            f"deferred_readback=1\nstale_readback=0\n"
        )
        for ep in (ROOT / "EngineConfig.ini", ROOT / "gow3.ini", ROOT.parent / "EngineConfig.ini", user_dir() / "EngineConfig.ini"):
            try:
                ep.write_text(engine_cfg_content, encoding="utf-8")
            except Exception:
                pass

    # -- Explorer's Desk Canvas & Navigation ------------------------------------------------

    def nav_items(self):
        items = [
            ("expeditions", "EXPEDITIONS"),
            ("graphics", "GRAPHICS & VRAM"),
            ("display", "DISPLAY & SCALING"),
            ("controls", "INPUT & CONTROLS"),
            ("telemetry", "TELEMETRY"),
            ("extras", "EXTRAS & TOOLS"),
        ]
        if multiplayer_preview() and self.config.get("game") == "u2":
            items.append(("multiplayer", "MULTIPLAYER"))
        items.append(("quit", "QUIT"))
        return items

    def build(self):
        self.desk = tk.Canvas(self.root, width=DESK_W, height=DESK_H, bg=WINDOW_BG, highlightthickness=0)
        self.desk.pack(fill="both", expand=True)

        if "spm_desk_bg" in self.images:
            self.desk.create_image(0, 0, image=self.images["spm_desk_bg"], anchor="nw", tags="bg")
        else:
            self.desk.create_rectangle(28, 24, DESK_W - 28, DESK_H - 24, outline=GOLD, width=2)
            self.desk.create_text(250, 86, text="UNCHARTED", font=("Arial Black", -38, "bold"), fill=TEXT)
            self.desk.create_text(250, 134, text="SIC PARVIS MAGNA COLLECTION", font=("Georgia", -18), fill=GOLD)

        self.status_text = f"Ready: {self.current_game()['name']}"
        self.substatus_text = ""

        # Glass settings container (shown when current_tab != "expeditions")
        self.panel_shell = tk.Frame(self.desk, bg=BG, width=self.PANEL_W - 24, height=self.PANEL_H - 24)
        self.panel_shell.pack_propagate(False)

        header = tk.Frame(self.panel_shell, bg=HEADER_BG, height=48, highlightthickness=1, highlightbackground=LINE)
        header.pack(side="top", fill="x")
        header.pack_propagate(False)

        title_col = tk.Frame(header, bg=HEADER_BG)
        title_col.pack(side="left", fill="both", expand=True, padx=(14, 8), pady=4)
        self.panel_title_lbl = tk.Label(title_col, text="", font=self.f_panel_title, bg=HEADER_BG, fg=GOLD_HI, anchor="w")
        self.panel_title_lbl.pack(side="top", fill="x")
        self.panel_sub_lbl = tk.Label(title_col, text="", font=self.f_caps, bg=HEADER_BG, fg=MUTED, anchor="w")
        self.panel_sub_lbl.pack(side="top", fill="x")

        # Per-game quick switcher inside the settings panel header
        game_bar = tk.Frame(header, bg=HEADER_BG)
        game_bar.pack(side="right", padx=(0, 12))
        tk.Label(game_bar, text="GAME:", font=self.f_caps, bg=HEADER_BG, fg=DIM).pack(side="left", padx=(0, 6))
        self.header_game_seg = Segmented(
            game_bar, [(g["key"], g["short"]) for g in GAMES],
            self.config.get("game", "u1"), lambda k: self.select_game(k), self.f_small, pad_x=8
        )
        self.header_game_seg.pack(side="left")

        self.body = tk.Frame(self.panel_shell, bg=BG)
        self.body.pack(side="top", fill="both", expand=True, padx=10, pady=(8, 6))

        self.scroll_canvases = set()
        self.root.bind_all("<MouseWheel>", self.wheel)
        self.root.bind("<Up>", lambda e: self.key_nav(-1))
        self.root.bind("<Down>", lambda e: self.key_nav(1))
        self.root.bind("<Left>", lambda e: self.key_journal(-1))
        self.root.bind("<Right>", lambda e: self.key_journal(1))
        self.root.bind("<Return>", lambda e: self.key_activate())
        self.root.bind("<F5>", lambda e: self.play_or_stop())
        self.root.bind("<Escape>", lambda e: self.key_back())

        self.pages = {
            "graphics": self.page_graphics(),
            "display": self.page_display(),
            "controls": self.page_controls(),
            "telemetry": self.page_telemetry(),
            "extras": self.page_extras(),
        }
        if multiplayer_preview():
            self.pages["multiplayer"] = self.page_multiplayer()

        saved_tab = self.config.get("tab", "expeditions")
        legacy_map = {"setup": "expeditions", "performance": "display", "cheats": "extras", "advanced": "extras"}
        saved_tab = legacy_map.get(saved_tab, saved_tab)
        if saved_tab not in self.pages and saved_tab != "expeditions":
            saved_tab = "expeditions"

        self.current_tab = saved_tab
        self.select_game(self.config.get("game", "u1"), save=False)
        self.show_tab(saved_tab, save=False)

    def key_nav(self, delta):
        if self.capturing is not None or self.pad_capture is not None:
            return
        focus = self.root.focus_get()
        if isinstance(focus, tk.Entry):
            return
        keys = [k for k, _ in self.nav_items()]
        cur = self.hovered_nav if self.hovered_nav in keys else self.current_tab
        idx = (keys.index(cur) + delta) % len(keys) if cur in keys else 0
        target = keys[idx]
        if target == "quit":
            self.hovered_nav = "quit"
            self.draw_nav()
        else:
            self.hovered_nav = None
            self.show_tab(target)

    def key_journal(self, delta):
        if self.capturing is not None or self.pad_capture is not None or self.current_tab != "expeditions":
            return
        idx = (GAME_KEYS.index(self.config.get("game", "u1")) + delta) % len(GAME_KEYS)
        self.select_game(GAME_KEYS[idx])

    def key_activate(self):
        if self.capturing is not None or self.pad_capture is not None:
            return
        if isinstance(self.root.focus_get(), tk.Entry):
            return
        if self.hovered_nav == "quit":
            self.close()
        else:
            self.play_or_stop()

    def key_back(self):
        if self.capturing is not None:
            self.stop_capture()
            return
        if self.pad_capture is not None:
            self.stop_pad_capture()
            return
        if self.current_tab != "expeditions":
            self.hovered_nav = None
            self.show_tab("expeditions")

    def draw_nav(self):
        self.desk.delete("nav")
        items = self.nav_items()
        y_start = 216
        step = 39 if len(items) <= 7 else 35
        for idx, (key, label) in enumerate(items):
            y = y_start + idx * step
            active = (key == self.current_tab and self.hovered_nav != "quit") or (key == "quit" and self.hovered_nav == "quit")
            hovered = key == self.hovered_nav
            color = GOLD_HI if (active or hovered) else NAV_IDLE
            font = self.f_nav_active if active else self.f_nav
            tag = f"nav_{key}"

            # Subtle dark shadow for crisp contrast over antique map edge
            self.desk.create_text(90, y + 2, text=label, font=font, fill="#080604", anchor="w", tags=("nav", tag))
            txt_id = self.desk.create_text(88, y, text=label, font=font, fill=color, anchor="w", tags=("nav", tag))

            if active:
                bbox = self.desk.bbox(txt_id) or (88, y - 10, 260, y + 10)
                line_y = y + 16
                left_x = 86
                right_x = max(bbox[2] + 34, 268)
                mid_x = (left_x + right_x) // 2
                self.desk.create_line(left_x, line_y, right_x, line_y, fill=GOLD, width=1, tags="nav")
                self.desk.create_line(left_x + 28, line_y, right_x - 28, line_y, fill=GOLD_HI, width=2, tags="nav")
                self.desk.create_polygon(
                    mid_x, line_y - 4, mid_x + 5, line_y, mid_x, line_y + 4, mid_x - 5, line_y,
                    fill=GOLD_HI, outline=INK, tags="nav"
                )

            hit = self.desk.create_rectangle(78, y - 16, 396, y + 18, fill="", outline="", tags=("nav", tag))
            for item_id in (txt_id, hit):
                self.desk.tag_bind(tag, "<Enter>", lambda e, k=key: self.on_nav_hover(k))
                self.desk.tag_bind(tag, "<Leave>", lambda e: self.on_nav_hover(None))
                self.desk.tag_bind(tag, "<Button-1>", lambda e, k=key: self.on_nav_click(k))
        self.desk.config(cursor="hand2" if self.hovered_nav else "")

    def on_nav_hover(self, key):
        if self.hovered_nav != key:
            self.hovered_nav = key
            self.draw_nav()

    def on_nav_click(self, key):
        if key == "quit":
            self.close()
        else:
            self.hovered_nav = None
            self.show_tab(key)

    def active_spec_summary(self):
        api_label = {"dx11": "D3D11", "dx12": "D3D12", "vulkan": "VULKAN"}.get(
            normalize_graphics_api(self.config.get("graphics_api", "dx11")), "D3D11"
        )
        vram = normalize_vram_cap(self.config.get("vram_budget_mb"), self.config.get("texture_quality", "medium"))
        vram_txt = "Auto VRAM" if vram == "0" else f"{vram} MB Cap"
        settings = self.game_settings()
        render, output = game_sizes(settings, upscaling_choice(self.ini), vram)
        res_txt = f"{render.split('x')[1]}p→{output.split('x')[1]}p" if output else f"{render.split('x')[1]}p"
        fps_txt = f"{self.ini.get('fps_limit', '60')} FPS"
        return f"{api_label}  •  {vram_txt}  •  {res_txt}  •  {fps_txt}"

    def draw_play(self, hover=None):
        if hover is not None:
            self.hovered_play = hover
        self.desk.delete("play_hud")

        # Launch / Stop Button on Left Column below Menu (always visible on every tab!)
        bx1, by1, bx2, by2 = 86, 548, 386, 598
        running = self.process is not None
        fill = STOP_RED if running else (GOLD_HI if self.hovered_play else GOLD)
        rim = "#ff9d8f" if running else "#fff0b8"
        round_rect(self.desk, bx1 - 2, by1 - 2, bx2 + 2, by2 + 2, 8, fill="#090705", outline=LINE, tags="play_hud")
        round_rect(self.desk, bx1, by1, bx2, by2, 7, fill=fill, outline=rim, width=1, tags=("play_hud", "play_btn"))
        label = "■   STOP RUNNING GAME" if running else f"▶   PLAY PART {self.current_game()['number']}"
        self.desk.create_text(
            (bx1 + bx2) // 2, (by1 + by2) // 2, text=label, font=self.f_play,
            fill=TEXT if running else INK, tags=("play_hud", "play_btn")
        )
        self.desk.tag_bind("play_btn", "<Button-1>", lambda e: self.play_or_stop())
        self.desk.tag_bind("play_btn", "<Enter>", lambda e: (self.desk.config(cursor="hand2"), self.draw_play(True)))
        self.desk.tag_bind("play_btn", "<Leave>", lambda e: (self.desk.config(cursor=""), self.draw_play(False)))

        # Update button if a new GitHub release was detected
        if self.update_release and updater is not None:
            ver = updater.version_text(self.update_release["version"])
            round_rect(self.desk, 86, 514, 286, 540, 5, fill=PANEL_HI, outline=GOLD, tags=("play_hud", "upd_btn"))
            self.desk.create_text(186, 527, text=f"⬇ UPDATE TO {ver}", font=self.f_caps, fill=GOLD_HI,
                                  tags=("play_hud", "upd_btn"))
            self.desk.tag_bind("upd_btn", "<Button-1>", lambda e: self.install_update())

        # Status & Active Spec Telemetry Strip below Launch button
        self.desk.create_text(88, 615, text=self.status_text, font=self.f_small_bold, fill=TEXT,
                              anchor="w", tags="play_hud")
        sub = self.substatus_text or self.active_spec_summary()
        self.desk.create_text(88, 634, text=sub, font=self.f_caps, fill=GOLD_HI, anchor="w", tags="play_hud")

        # Bottom Stepped-Frame Hint Bar (matching reference mockup!)
        footer_hint = "SELECT [ENTER]      EXPEDITIONS [◄ / ►]      LAUNCH [F5 / DOUBLE-CLICK]      BACK [ESC]"
        self.desk.create_text(641, 675, text=footer_hint, font=self.f_footer, fill="#080604", tags="play_hud")
        self.desk.create_text(640, 674, text=footer_hint, font=self.f_footer, fill=NAV_IDLE, tags="play_hud")

    def draw_expeditions_hud(self):
        self.desk.delete("expeditions")
        if self.current_tab != "expeditions":
            return

        # Top-Right Hardware Quick-Preset Strip (clear of the brass oil lantern at x=730..920)
        px1, py1, px2, py2 = 930, 44, 1236, 112
        round_rect(self.desk, px1, py1, px2, py2, 8, fill="#15100b", outline=LINE, width=1, tags="expeditions")
        gpu_short = (self.gpu_name or "2015+ DirectX 11 / Vulkan GPU")
        if len(gpu_short) > 36:
            gpu_short = gpu_short[:34] + "…"
        self.desk.create_text(px1 + 12, py1 + 16, text=f"QUICK PRESET  •  {gpu_short}",
                              font=self.f_caps, fill=GOLD_HI, anchor="w", tags="expeditions")

        cur_preset = self.game_settings().get("preset", "medium")
        chip_x = px1 + 10
        for p_key in PRESET_ORDER:
            chip_lbl = PRESETS[p_key]["chip"]
            if p_key == self.recommended:
                chip_lbl += "★"
            cw = max(46, len(chip_lbl) * 7 + 12)
            selected = cur_preset == p_key
            hovered = self.hovered_preset_chip == p_key
            cfill = GOLD if selected else (PANEL_HI if hovered else PANEL)
            cfg = INK if selected else (GOLD_HI if hovered else TEXT)
            ctag = f"pchip_{p_key}"
            round_rect(self.desk, chip_x, py1 + 34, chip_x + cw, py1 + 58, 5,
                       fill=cfill, outline=GOLD_HI if selected else LINE, tags=("expeditions", ctag))
            self.desk.create_text(chip_x + cw // 2, py1 + 46, text=chip_lbl, font=self.f_caps,
                                  fill=cfg, tags=("expeditions", ctag))
            self.desk.tag_bind(ctag, "<Button-1>", lambda e, p=p_key: self.apply_preset(p))
            self.desk.tag_bind(ctag, "<Enter>", lambda e, p=p_key: self.on_preset_chip_hover(p))
            self.desk.tag_bind(ctag, "<Leave>", lambda e: self.on_preset_chip_hover(None))
            chip_x += cw + 5

        # Three Embossed Leather Expedition Journals over the Antique World Map
        selected_game = self.config.get("game", "u1")
        for game in GAMES:
            key = game["key"]
            is_sel = key == selected_game
            is_hov = key == self.hovered_journal
            cx = game["cx"]
            cy = game["cy"] - (6 if (is_sel or is_hov) else 0)
            img_name = f"spm_journal_{key}_active" if (is_sel or is_hov) else f"spm_journal_{key}"
            jtag = f"journal_{key}"

            if img_name in self.images:
                self.desk.create_image(cx, cy, image=self.images[img_name], anchor="center",
                                       tags=("expeditions", jtag))
            else:
                round_rect(self.desk, cx - 98, cy - 136, cx + 98, cy + 136, 8,
                           fill="#251a12", outline=GOLD_HI if is_sel else LINE, width=2,
                           tags=("expeditions", jtag))
                self.desk.create_text(cx, cy + 40, text=f"PART {game['number']}:\n{game['title']}",
                                      font=self.f_card_title, fill=GOLD_HI, justify="center",
                                      tags=("expeditions", jtag))

            if is_sel:
                badge_y = cy - 154
                round_rect(self.desk, cx - 74, badge_y - 10, cx + 74, badge_y + 10, 9,
                           fill="#18120c", outline=GOLD, tags=("expeditions", jtag))
                self.desk.create_text(cx, badge_y, text="◆  ACTIVE EXPEDITION  ◆",
                                      font=self.f_caps, fill=GOLD_HI, tags=("expeditions", jtag))

            self.desk.tag_bind(jtag, "<Button-1>", lambda e, k=key: self.select_game(k))
            self.desk.tag_bind(jtag, "<Double-Button-1>", lambda e, k=key: self.play_game(k))
            self.desk.tag_bind(jtag, "<Enter>", lambda e, k=key: self.on_journal_hover(k))
            self.desk.tag_bind(jtag, "<Leave>", lambda e: self.on_journal_hover(None))

    def on_preset_chip_hover(self, preset_key):
        if self.hovered_preset_chip != preset_key:
            self.hovered_preset_chip = preset_key
            self.desk.config(cursor="hand2" if preset_key else "")
            self.draw_expeditions_hud()

    def on_journal_hover(self, key):
        if self.hovered_journal != key:
            self.hovered_journal = key
            self.desk.config(cursor="hand2" if key else "")
            self.draw_expeditions_hud()

    def show_tab(self, name, save=True):
        if name == "multiplayer" and (not multiplayer_preview() or self.config.get("game") != "u2"):
            name = "expeditions"
        self.current_tab = name
        if name == "multiplayer" and not getattr(self, "net_searched", False):
            self.net_searched = True
            self.root.after(300, self.search_net_games)
        if save and self.config.get("tab") != name:
            self.config["tab"] = name
            write_config(self.config)

        self.desk.delete("glass_panel")
        for page in self.pages.values():
            holder = getattr(page, "holder", page)
            holder.pack_forget()

        if name == "expeditions":
            self.draw_expeditions_hud()
        else:
            self.desk.delete("expeditions")
            if "spm_panel_glass" in self.images:
                self.desk.create_image(
                    self.PANEL_X, self.PANEL_Y, image=self.images["spm_panel_glass"],
                    anchor="nw", tags="glass_panel"
                )
            else:
                round_rect(
                    self.desk, self.PANEL_X, self.PANEL_Y,
                    self.PANEL_X + self.PANEL_W, self.PANEL_Y + self.PANEL_H,
                    8, fill=BG, outline=GOLD, width=2, tags="glass_panel"
                )
            title, sub = self.PANEL_TITLES.get(name, (name.upper(), ""))
            self.panel_title_lbl.config(text=title)
            self.panel_sub_lbl.config(text=sub)
            self.desk.create_window(
                self.PANEL_X + 12, self.PANEL_Y + 12,
                window=self.panel_shell, anchor="nw",
                width=self.PANEL_W - 24, height=self.PANEL_H - 24,
                tags="glass_panel"
            )
            page = self.pages[name]
            getattr(page, "holder", page).pack(fill="both", expand=True)

        self.draw_nav()
        self.draw_play()

    def select_game(self, key, save=True):
        if key not in GAME_KEYS:
            key = "u1"
        changed = key != self.config.get("game")
        self.config["game"] = key
        if hasattr(self, "header_game_seg"):
            self.header_game_seg.set(key)
        if save:
            write_config(self.config)
        if changed:
            self.reload_ini()
            self.refresh_controls()
        if not self.process:
            self.set_status(f"Ready: {self.current_game()['name']}", "")
        if self.current_tab == "multiplayer" and key != "u2":
            self.show_tab("expeditions", save=False)
        else:
            self.draw_nav()
            self.draw_expeditions_hud()
            self.draw_play()

    def current_game(self):
        return next(g for g in GAMES if g["key"] == self.config.get("game", "u1"))

    def game_settings(self):
        return self.config["games"][self.config["game"]]

    def save_ini(self, updates):
        write_ini(self.config["game"], updates)
        self.ini_seen = ini_mtime(self.config["game"])

    def reload_ini(self):
        self.ini = read_ini(self.config["game"])
        self.ini_seen = ini_mtime(self.config["game"])
        if controls_from_ini(self.ini, self.game_settings()):
            write_config(self.config)

    def save_controls(self):
        write_config(self.config)
        self.save_ini(controls_to_ini(self.game_settings()))

    def follow_ini(self):
        """Only reload probe-<game>.ini when the running game's Insert menu modifies it on disk."""
        if self.process is None:
            return
        current_mtime = ini_mtime(self.config["game"])
        if current_mtime is not None and current_mtime != getattr(self, "ini_seen", None):
            self.reload_ini()
            if "graphics_api" in self.ini:
                self.config["graphics_api"] = normalize_graphics_api(self.ini["graphics_api"])
            if "vram_budget_mb" in self.ini:
                self.config["vram_budget_mb"] = normalize_vram_cap(
                    self.ini["vram_budget_mb"], self.config.get("texture_quality", "medium")
                )
            if "frame_pacer" in self.ini:
                self.config["frame_pacing_mode"] = normalize_frame_pacer(self.ini["frame_pacer"])
            write_config(self.config)
            self.refresh_controls()

    def set_status(self, text, detail=None):
        self.status_text = text
        if detail is not None:
            self.substatus_text = detail
        if hasattr(self, "desk"):
            self.draw_play()

    def wheel(self, event):
        if self.capturing is not None or self.current_tab == "expeditions":
            return
        try:
            widget = self.root.winfo_containing(event.x_root, event.y_root)
        except (KeyError, tk.TclError):
            return
        while widget is not None:
            if str(widget) in self.scroll_canvases:
                if tuple(widget.yview()) != (0.0, 1.0):
                    step = int(-event.delta / 120) or (-1 if event.delta > 0 else 1)
                    widget.yview_scroll(step, "units")
                return
            widget = widget.master

    # -- Settings Page Builders -------------------------------------------------------------

    def page(self, scroll=True):
        if not scroll:
            return tk.Frame(self.body, bg=BG)
        holder = tk.Frame(self.body, bg=BG)
        canvas = tk.Canvas(holder, bg=BG, highlightthickness=0)
        bar = ScrollBar(holder, canvas)
        inner = tk.Frame(canvas, bg=BG)
        window = canvas.create_window((0, 0), window=inner, anchor="nw")
        canvas.configure(yscrollcommand=bar.set)
        canvas.pack(side="left", fill="both", expand=True)

        def layout(event=None):
            width, height, need = canvas.winfo_width(), canvas.winfo_height(), inner.winfo_reqheight()
            if height <= 1:
                return
            canvas.itemconfigure(window, width=width)
            canvas.configure(scrollregion=(0, 0, width, max(need, height)))
            if need > height and not bar.winfo_manager():
                bar.pack(side="right", fill="y", padx=(6, 0), before=canvas)
            elif need <= height and bar.winfo_manager():
                bar.pack_forget()
                canvas.yview_moveto(0)

        inner.bind("<Configure>", layout)
        canvas.bind("<Configure>", layout)
        self.scroll_canvases.add(str(canvas))
        inner.holder = holder
        return inner

    def section_divider(self, page, label):
        box = tk.Frame(page, bg=BG)
        box.pack(side="top", fill="x", pady=(8, 4))
        tk.Label(box, text=f"◆  {label}", font=self.f_caps, bg=BG, fg=GOLD_HI, anchor="w").pack(side="left")
        tk.Frame(box, bg=LINE, height=1).pack(side="left", fill="x", expand=True, padx=(10, 0), pady=4)

    def row(self, page, title, hint, control_factory):
        row = tk.Frame(page, bg=BG)
        row.pack(side="top", fill="x", pady=(0, 7))
        text = tk.Frame(row, bg=BG)
        text.pack(side="left", fill="x", expand=True)
        tk.Label(text, text=title, font=self.f_body_bold, bg=BG, fg=TEXT, anchor="w").pack(side="top", fill="x")
        if hint:
            tk.Label(text, text=hint, font=self.f_small, bg=BG, fg=MUTED, anchor="w", justify="left",
                     wraplength=395).pack(side="top", fill="x")
        control = control_factory(row)
        control.pack(side="right", padx=(12, 0))
        return control

    def ini_toggle(self, page, key, title, hint):
        def factory(parent):
            return Toggle(parent, self.ini.get(key, INI_DEFAULTS.get(key, "0")) == "1",
                          lambda v, k=key: self.change(k, "1" if v else "0"))
        self.widgets[key] = self.row(page, title, hint, factory)

    def ini_choice(self, page, key, title, hint, options, recommended=None, pad_x=8):
        def factory(parent):
            return Segmented(
                parent, options, self.ini.get(key, INI_DEFAULTS.get(key)),
                lambda v, k=key: self.change(k, v), self.f_small, recommended, pad_x=pad_x
            )
        self.widgets[key] = self.row(page, title, hint, factory)

    def flat_button(self, parent, label, action):
        button = tk.Label(parent, text=label, font=self.f_small_bold, bg=PANEL, fg=TEXT, padx=10, pady=5,
                          cursor="hand2", highlightthickness=1, highlightbackground=LINE)
        button.bind("<Button-1>", lambda e: action())
        button.bind("<Enter>", lambda e: button.config(bg=PANEL_HI, fg=GOLD_HI))
        button.bind("<Leave>", lambda e: button.config(bg=PANEL, fg=TEXT))
        return button

    def gold_button(self, parent, label, action, small=False):
        button = tk.Label(parent, text=label, font=self.f_small_bold if small else self.f_body_bold,
                          bg=GOLD, fg=INK, padx=12 if small else 18, pady=4 if small else 7, cursor="hand2")
        button.bind("<Button-1>", lambda e: action())
        button.bind("<Enter>", lambda e: button.config(bg=GOLD_HI))
        button.bind("<Leave>", lambda e: button.config(bg=GOLD))
        return button

    def page_graphics(self):
        page = self.page()

        banner = tk.Frame(page, bg=PANEL, highlightthickness=1, highlightbackground=LINE)
        banner.pack(side="top", fill="x", pady=(0, 8), ipadx=8, ipady=5)
        rec = PRESETS.get(self.recommended, {}).get("title", "MED (2 GB)")
        gpu_txt = f"Detected GPU: {self.gpu_name}   •   Recommended Tier: {rec}" if self.gpu_name else \
            "2015+ Hardware Ready: DirectX 11 (FL 11_0) / DirectX 12 / Vulkan 1.3  •  1 GB & 2 GB Strict VRAM Caps"
        self.gpu_banner_label = tk.Label(banner, text=gpu_txt, font=self.f_small_bold, bg=PANEL, fg=GOLD_HI, anchor="w")
        self.gpu_banner_label.pack(side="top", fill="x", padx=8)

        self.section_divider(page, "HARDWARE PRESET & PRESENTATION API")

        def preset_factory(parent):
            opts = [(k, PRESETS[k]["chip"]) for k in PRESET_ORDER] + [("custom", "Custom")]
            cur = self.game_settings().get("preset", "medium")
            return Segmented(parent, opts, cur if cur in PRESETS else "custom",
                             lambda v: v in PRESETS and self.apply_preset(v),
                             self.f_small, recommended=self.recommended, pad_x=7)
        self.widgets["preset_seg"] = self.row(
            page, "Hardware Tier Preset",
            "Configures resolution, VRAM hard cap, textures, shadows and upscaling for 2015 Min (1 GB) to modern 4K GPUs.",
            preset_factory,
        )

        def api_factory(parent):
            return Segmented(
                parent, GRAPHICS_APIS, normalize_graphics_api(self.config.get("graphics_api", "dx11")),
                self.change_graphics_api, self.f_small, recommended="dx11", pad_x=9
            )
        self.widgets["graphics_api"] = self.row(
            page, "Graphics Presentation API",
            "Genuine hardware DXGI Flip-Model SwapChain (D3D11 FL 11_0 / D3D12) with zero-copy NT shared memory, or native Vulkan WSI.",
            api_factory,
        )

        def vram_factory(parent):
            return Segmented(
                parent, VRAM_CAPS,
                normalize_vram_cap(self.config.get("vram_budget_mb"), self.config.get("texture_quality", "medium")),
                self.change_vram_cap, self.f_small, pad_x=6
            )
        self.widgets["vram_budget_mb"] = self.row(
            page, "Strict VRAM Budget Hard Cap",
            "Enforces VMA heap ceiling, 55%/70%/82% LRU texture eviction & mip LOD bias (820 MB = 1 GB GPUs, 1720 MB = 2 GB PS4 target).",
            vram_factory,
        )

        self.section_divider(page, "TEXTURES, ANISOTROPY & SHADOWS")

        def tex_factory(parent):
            opts = [("low", "Low"), ("medium", "Medium"), ("high", "High"), ("ultra", "Ultra")]
            return Segmented(parent, opts, self.config.get("texture_quality", "medium"),
                             self.change_texture_quality, self.f_small, recommended="medium", pad_x=9)
        self.widgets["texture_quality"] = self.row(
            page, "Texture Streaming Quality",
            "Controls resident mipmap streaming pool and arena buffer limits.",
            tex_factory,
        )

        self.widgets["full_res_textures"] = self.row(
            page, "Force Uncompressed Mip 0 (U2 & U3)",
            "Eliminates texture pop-in on 4 GB+ GPUs (automatically disabled under 1 GB Low cap). Requires Update 1.02.",
            lambda p: Toggle(p, self.config.get("full_res_textures", False),
                             lambda v: self.change_config("full_res_textures", v)),
        )

        def aniso_factory(parent):
            opts = [("0", "PS4"), ("4", "4x"), ("8", "8x"), ("16", "16x")]
            return Segmented(parent, opts, str(self.config.get("anisotropy", "16")),
                             lambda v: self.change_config("anisotropy", v), self.f_small, recommended="16", pad_x=9)
        self.widgets["anisotropy"] = self.row(
            page, "Anisotropic Texture Filtering",
            "Sharpens ground, stone and angled surfaces across all three games at virtually zero GPU cost.",
            aniso_factory,
        )

        def shadow_factory(parent):
            opts = [("low", "Low (512)"), ("medium", "Med (1024)"), ("high", "High (2048)"), ("ultra", "Ultra")]
            return Segmented(parent, opts, self.config.get("shadow_quality", "medium"),
                             self.change_shadow_quality, self.f_small, recommended="medium", pad_x=7)
        self.widgets["shadow_quality"] = self.row(
            page, "Cascade Shadow Quality",
            "Sunlight & local shadow resolution: Low saves bandwidth on 2015 GPUs; Medium matches PS4; High/Ultra sharpen cascades.",
            shadow_factory,
        )

        self.widgets["hires_shadows"] = self.row(
            page, "High-Res Torch & Lamp Shadows (U2 & U3)",
            "Dedicated high-resolution dynamic point-light shadow buffers. Requires Update 1.02.",
            lambda p: Toggle(p, self.config.get("hires_shadows", False),
                             lambda v: self.change_config("hires_shadows", v)),
        )

        def shadow_dist_factory(parent):
            cur = self.ini.get("sun_shadow_distance", "100")
            if cur not in {k for k, _ in DISTANCE_SCALES}:
                cur = "100"
            return Segmented(parent, DISTANCE_SCALES, cur, self.change_shadow_distance,
                             self.f_small, recommended="100", pad_x=7)
        self.widgets["shadow_distance"] = self.row(
            page, "Shadow Draw Distance",
            "Scales sun & local shadow cutoff distance (75% boosts FPS on 2015 GPUs; 125–150% extends distant shadows).",
            shadow_dist_factory,
        )

        self.ini_choice(
            page, "fade_distance", "Object LOD & Foliage Distance",
            "Controls geometry & detail fade-out multiplier (100% = PS4 native).",
            DISTANCE_SCALES, recommended="100", pad_x=7,
        )

        self.section_divider(page, "POST-PROCESSING & CPU THREADING")

        def ssao_factory(parent):
            opts = [("off", "Off"), ("half", "Half (2015)"), ("full", "Full (PS4)"), ("hbao", "HBAO+")]
            return Segmented(parent, opts, self.config.get("ssao_quality", "full"),
                             lambda v: self.change_config("ssao_quality", v), self.f_small, recommended="full", pad_x=8)
        self.widgets["ssao_quality"] = self.row(
            page, "Ambient Occlusion (SSAO)",
            "Contact shadowing in crevices and architecture: Half-Res cuts bandwidth in half on 1 GB GPUs.",
            ssao_factory,
        )

        def dof_factory(parent):
            opts = [("enabled", "Cinematic"), ("disabled", "Off (Crisp)")]
            return Segmented(
                parent, opts, self.config.get("dof_mode", "enabled"),
                lambda v: (self.change_config("dof_mode", v), self.change_config("no_dof", v == "disabled")),
                self.f_small, pad_x=9,
            )
        self.widgets["dof_mode"] = self.row(
            page, "Depth of Field (DoF)",
            "Cinematic focal blur in cutscenes and weapon aiming (Uncharted 2 & 3).",
            dof_factory,
        )

        def blur_factory(parent):
            opts = [("enabled", "Full"), ("camera", "Camera"), ("disabled", "Off")]
            return Segmented(
                parent, opts, self.config.get("motion_blur_mode", "enabled"),
                lambda v: (self.change_config("motion_blur_mode", v), self.change_config("no_motion_blur", v == "disabled")),
                self.f_small, pad_x=9,
            )
        self.widgets["motion_blur_mode"] = self.row(
            page, "Motion Blur",
            "Per-object & camera shutter blur; Off gives razor-sharp high-refresh motion.",
            blur_factory,
        )

        self.widgets["multithreaded"] = self.row(
            page, "Multithreaded Command Recording",
            "Parallelizes GPU draw-packet recording across worker cores (recommended On for all 3 games).",
            lambda p: Toggle(p, self.game_settings().get("multithreaded", True),
                             lambda v: self.change_game_setting("multithreaded", v)),
        )

        def job_factory(parent):
            val = str(self.config.get("job_sleep", "auto"))
            return Segmented(parent, [("auto", "Auto"), ("on", "Sleep (Cool)"), ("off", "Spin")],
                             val if val in ("auto", "on", "off") else "auto",
                             lambda v: self.change_config("job_sleep", v), self.f_small, recommended="auto", pad_x=8)
        self.widgets["job_sleep"] = self.row(
            page, "Idle CPU Worker Policy",
            "Sleep lowers CPU temperature & power draw on 4–6 core CPUs; Auto balances frame pacing automatically.",
            job_factory,
        )

        return page

    def page_display(self):
        page = self.page()
        self.section_divider(page, "WINDOW MODE, ASPECT & RESOLUTION")

        self.ini_choice(
            page, "display_mode", "Display Mode",
            "Borderless enables zero-latency DXGI DirectFlip & instant Alt+Tab; Fullscreen gives exclusive control.",
            [("windowed", "Windowed"), ("borderless", "Borderless"), ("fullscreen", "Fullscreen")],
            recommended="borderless", pad_x=9,
        )

        self.widgets["aspect"] = self.row(
            page, "Aspect Ratio",
            "Native hor+ ultrawide (21:9, 32:9) and 16:10 (Steam Deck 800p) with unstretched HUD.",
            lambda p: Segmented(p, ASPECTS, self.game_settings().get("aspect", "16:9"),
                                self.change_aspect, self.f_small, pad_x=9),
        )

        self.widgets["resolution"] = self.row(
            page, "Output Resolution",
            "Target presentation resolution. Under 1 GB / 2 GB VRAM hard caps, internal 3D render scales automatically.",
            lambda p: Segmented(p, resolution_options(self.game_settings().get("aspect", "16:9")),
                                self.game_settings()["resolution"], self.change_resolution, self.f_small, pad_x=6),
        )

        self.render_size_note = tk.Label(page, text="", font=self.f_small_bold, bg=BG, fg=GOLD_HI, anchor="w")
        self.render_size_note.pack(side="top", fill="x", pady=(0, 6))

        self.section_divider(page, "UPSCALING, ANTI-ALIASING & SHARPENING")

        self.widgets["upscaling"] = self.row(
            page, "Upscaler",
            "FSR 1 (spatial, ideal for 2015 GPUs) and FSR 3.1 / FSR 4 / DLSS temporal reconstruction.",
            lambda p: Segmented(p, upscaling_options(), upscaling_choice(self.ini),
                                self.change_upscaling, self.f_small, pad_x=9),
        )

        def upscale_factory(parent):
            return Segmented(parent, UPSCALE_PRESETS, self.game_settings().get("upscale", "off"),
                             self.change_upscale_quality, self.f_small, pad_x=7)
        self.upscale_row = self.row(
            page, "Upscaling Quality",
            "Quality (1.5x), Balanced (1.7x), Performance (2.0x), Ultra Perf (3.0x), or Native AA.",
            upscale_factory,
        ).master

        self.upscale_note = tk.Label(page, text="", font=self.f_small_bold, bg=BG, fg=GOLD_HI, anchor="w")
        self.upscale_note.pack(side="top", fill="x", pady=(0, 6))

        self.widgets["aa"] = self.row(
            page, "Anti-Aliasing",
            "Temporal AA when Upscaler is Off or FSR 1 (temporal upscalers include built-in AA).",
            lambda p: Segmented(p, [("off", "Off"), ("taa", "TAA")],
                                anti_aliasing_choice(self.ini, self.game_settings()),
                                self.change_anti_aliasing, self.f_small, pad_x=10),
        )

        def sharp_factory(parent):
            val = self.ini.get("rcas_strength", "75") if self.ini.get("rcas", "1") == "1" else "off"
            return Segmented(parent, [("off", "Off"), ("25", "25%"), ("50", "50%"), ("75", "75%"), ("100", "100%")],
                             val, self.change_sharpening, self.f_small, pad_x=7)
        self.widgets["sharpening"] = self.row(
            page, "FidelityFX RCAS Sharpening",
            "Contrast-adaptive sharpening pass applied after upscaling/TAA.",
            sharp_factory,
        )

        self.section_divider(page, "FRAME RATE CAP & SUB-MS HYBRID PACING")

        self.ini_toggle(
            page, "vsync", "Vertical Sync (VSync)",
            "Eliminates screen tearing; works seamlessly alongside the Hybrid QPC Frame Pacer.",
        )

        self.ini_choice(
            page, "fps_limit", "Target FPS Cap",
            f"Deterministic hardware cap (monitor: {monitor_refresh_hz()} Hz). 30/45 FPS for 2015 1 GB GPUs; 60 FPS PS4 standard; up to 120 FPS.",
            [(str(v), label) for v, label in FPS_LIMITS], recommended="60", pad_x=5,
        )

        def pacer_factory(parent):
            cur = normalize_frame_pacer(self.config.get("frame_pacing_mode", self.ini.get("frame_pacer", "hybrid")))
            return Segmented(parent, FRAME_PACER_OPTIONS, cur, self.change_frame_pacer,
                             self.f_small, recommended="hybrid", pad_x=8)
        self.widgets["frame_pacer"] = self.row(
            page, "Frame Pacer Engine",
            "Hybrid QPC sleeps via high-res waitable timer (saving CPU power/temp) and spin-locks the final 1.2 ms (<0.25 ms jitter).",
            pacer_factory,
        )

        self.ini_choice(
            page, "frames_queued", "GPU Frames Queued",
            "Keep at 1 for lowest input latency and accurate character grounding in Uncharted 2.",
            [("1", "1"), ("2", "2"), ("0", "Max")], recommended="1", pad_x=10,
        )

        self.ini_toggle(
            page, "async_shaders", "Asynchronous Background Shader Compilation",
            "Compiles pipelines on worker threads to eliminate traversal stutters.",
        )

        self.widgets["movie_stretch"] = self.row(
            page, "Stretch 16:9 Prerecorded Cutscenes",
            "Fills ultrawide displays during pre-rendered movies instead of pillarbox bars.",
            lambda p: Toggle(p, self.config.get("movie_stretch", False),
                             lambda v: self.change_config("movie_stretch", v)),
        )

        self.widgets["u3_exposure"] = self.row(
            page, "Uncharted 3 HDR Exposure Trim",
            "Tames overly bright desert skies/highlights on Uncharted 3 if needed (0 = PS4 original).",
            lambda p: Segmented(p, [("0", "PS4 (0)"), ("-0.3", "-0.3"), ("-0.6", "-0.6"), ("-1.0", "-1.0")],
                                str(self.config.get("u3_exposure", "0")),
                                lambda v: self.change_config("u3_exposure", v), self.f_small, pad_x=8),
        )

        self.fps_note = tk.Label(page, text="", font=self.f_small, bg=BG, fg=GOLD_HI, anchor="w")
        self.fps_note.pack(side="top", fill="x")
        self.refresh_render_size()
        self.update_fps_note()
        return page

    def page_controls(self):
        page = self.page(scroll=False)
        settings = self.game_settings()

        top = tk.Frame(page, bg=BG)
        top.pack(side="top", fill="x", pady=(0, 5))
        tk.Label(top, text="Mouse Look", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["mouse"] = Toggle(top, settings.get("mouse", True), lambda v: self.change_game_setting("mouse", v))
        self.widgets["mouse"].pack(side="left", padx=(6, 14))
        tk.Label(top, text="Sens", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["mouse_sens"] = Segmented(top, MOUSE_SENSITIVITIES, settings.get("mouse_sens", "1.0"),
                                               lambda v: self.change_game_setting("mouse_sens", v), self.f_small, pad_x=5)
        self.widgets["mouse_sens"].pack(side="left", padx=(6, 14))
        tk.Label(top, text="Inv Y", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["mouse_invert"] = Toggle(top, settings.get("mouse_invert", False),
                                              lambda v: self.change_game_setting("mouse_invert", v))
        self.widgets["mouse_invert"].pack(side="left", padx=(6, 0))
        self.flat_button(top, "Reset Defaults", self.reset_controls).pack(side="right")

        mid = tk.Frame(page, bg=BG)
        mid.pack(side="top", fill="x", pady=(0, 5))
        tk.Label(mid, text="Aim", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["mouse_aim_sens"] = Segmented(mid, MOUSE_AIM_FACTORS, settings.get("mouse_aim_sens", "1.0"),
                                                   lambda v: self.change_game_setting("mouse_aim_sens", v), self.f_small, pad_x=5)
        self.widgets["mouse_aim_sens"].pack(side="left", padx=(6, 12))
        tk.Label(mid, text="Vert", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["mouse_vertical"] = Segmented(mid, MOUSE_FACTORS, settings.get("mouse_vertical", "1.0"),
                                                   lambda v: self.change_game_setting("mouse_vertical", v), self.f_small, pad_x=5)
        self.widgets["mouse_vertical"].pack(side="left", padx=(6, 12))
        tk.Label(mid, text="Inv X", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["mouse_invert_x"] = Toggle(mid, settings.get("mouse_invert_x", False),
                                                lambda v: self.change_game_setting("mouse_invert_x", v))
        self.widgets["mouse_invert_x"].pack(side="left", padx=(6, 12))
        tk.Label(mid, text="Rumble", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["vibration"] = Toggle(mid, settings.get("vibration", True),
                                           lambda v: self.change_game_setting("vibration", v))
        self.widgets["vibration"].pack(side="left", padx=(6, 0))

        bot = tk.Frame(page, bg=BG)
        bot.pack(side="top", fill="x", pady=(0, 5))
        tk.Label(bot, text="Prompts", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")
        self.widgets["prompts"] = Segmented(bot, PROMPT_MODES, settings.get("prompts", "auto"),
                                            self.change_prompts, self.f_small, pad_x=7)
        self.widgets["prompts"].pack(side="left", padx=(6, 16))
        tk.Label(bot, text="In-Game Menu", font=self.f_small_bold, bg=BG, fg=TEXT).pack(side="left")

        def save_menu_shortcut(value):
            self.ini["menu_shortcut"] = value
            for g in GAME_KEYS:
                write_ini(g, {"menu_shortcut": value})
            self.ini_seen = ini_mtime(self.config["game"])

        self.widgets["menu_shortcut"] = Segmented(
            bot, [("lt_r3", "L2+R3"), ("view_menu", "Select+Start"), ("keyboard", "Insert Only")],
            self.ini.get("menu_shortcut", "lt_r3"), save_menu_shortcut, self.f_small, pad_x=7
        )
        self.widgets["menu_shortcut"].pack(side="left", padx=(6, 0))

        self.controls_hint = tk.Label(
            page,
            text="Left-click any slot to bind Key / Mouse / Controller; Right-click to clear. Press Insert in-game to free cursor.",
            font=self.f_small, bg=BG, fg=GOLD_HI, anchor="w",
        )
        self.controls_hint.pack(side="top", fill="x", pady=(2, 4))

        holder = tk.Frame(page, bg=BG, highlightthickness=1, highlightbackground=LINE)
        holder.pack(side="top", fill="both", expand=True)
        canvas = tk.Canvas(holder, bg=BG, highlightthickness=0)
        bar = ScrollBar(holder, canvas)
        inner = tk.Frame(canvas, bg=BG)
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=inner, anchor="nw")
        canvas.configure(yscrollcommand=bar.set)
        bar.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True, padx=6, pady=4)
        self.scroll_canvases.add(str(canvas))

        self.binding_slots = {}
        self.pad_slots = {}
        columns = [tk.Frame(inner, bg=BG), tk.Frame(inner, bg=BG)]
        columns[0].grid(row=0, column=0, sticky="n", padx=(0, 18))
        columns[1].grid(row=0, column=1, sticky="n")
        for index, (group, actions) in enumerate(CONTROL_GROUPS):
            column = columns[0 if index < 2 else 1]
            tk.Label(column, text=f"◆ {group}", font=self.f_caps, bg=BG, fg=GOLD_HI, anchor="w").pack(
                side="top", fill="x", pady=(4, 2)
            )
            for action, label in actions:
                r = tk.Frame(column, bg=BG)
                r.pack(side="top", fill="x", pady=1)
                tk.Label(r, text=label, font=self.f_small, bg=BG, fg=TEXT, anchor="w", width=15).pack(side="left")
                for slot in (0, 1):
                    cell = tk.Label(r, text="", font=self.f_small, bg=PANEL, fg=TEXT, width=10, pady=2,
                                    cursor="hand2", highlightthickness=1, highlightbackground=LINE)
                    cell.pack(side="left", padx=(4, 0))
                    cell.bind("<Button-1>", lambda e, a=action, i=slot: self.start_capture(a, i))
                    cell.bind("<Button-3>", lambda e, a=action, i=slot: self.set_binding(a, i, None))
                    self.binding_slots[(action, slot)] = cell
                pad_cell = tk.Label(r, text="", font=self.f_small, bg=PANEL, fg=GOLD_HI, width=9, pady=2,
                                    highlightthickness=1, highlightbackground=LINE, compound="center")
                pad_cell.pack(side="left", padx=(4, 0))
                if action in PAD_DEFAULTS:
                    pad_cell.config(cursor="hand2")
                    pad_cell.bind("<Button-1>", lambda e, a=action: self.start_pad_capture(a))
                    pad_cell.bind("<Button-3>", lambda e, a=action: self.set_pad_binding(a, None))
                self.pad_slots[action] = pad_cell
        self.refresh_bindings()
        self.refresh_pad_slots()
        return page

    OVERLAY_ITEMS = (
        ("overlay_fps", "FPS + API"),
        ("overlay_frametime", "Frame ms"),
        ("overlay_gpu", "GPU"),
        ("overlay_cpu", "CPU"),
        ("overlay_vram", "VRAM"),
        ("overlay_ram", "RAM"),
        ("overlay_upscaler", "Upscaler"),
    )

    def page_telemetry(self):
        page = self.page()
        self.section_divider(page, "IN-GAME PERFORMANCE OVERLAY (SHARED WITH RTSS / AFTERBURNER)")

        self.ini_toggle(
            page, "show_fps", "Show Built-In Performance Telemetry HUD",
            "Displays live FPS, active presentation API (D3D11 / D3D12 / VULKAN), frame time, VRAM & thermals. Toggleable via Insert.",
        )

        def items_factory(parent):
            box = tk.Frame(parent, bg=BG)
            self.overlay_chips = {}
            for key, label in self.OVERLAY_ITEMS:
                chip = Chip(box, self.f_small, label,
                            self.ini.get(key, INI_DEFAULTS[key]) == "1",
                            lambda v, k=key: self.change(k, "1" if v else "0"))
                chip.pack(side="left", padx=(4, 0))
                self.overlay_chips[key] = chip
            return box
        self.row(page, "Visible Telemetry Metrics", "Click any pill to show or hide it on the HUD.", items_factory)

        self.ini_choice(
            page, "overlay_corner", "HUD Screen Corner", "Where the telemetry overlay is anchored.",
            [("0", "Top-Left"), ("1", "Top-Right"), ("2", "Bot-Left"), ("3", "Bot-Right")], pad_x=8,
        )
        self.ini_choice(
            page, "overlay_layout", "HUD Layout", "Compact horizontal bar or vertical multi-row readout.",
            [("0", "Horizontal Bar"), ("1", "Stacked Rows")], pad_x=10,
        )
        self.ini_choice(
            page, "overlay_scale", "HUD Text Scale", "Font size of the in-game telemetry overlay.",
            [("75", "75%"), ("100", "100%"), ("125", "125%"), ("150", "150%"), ("200", "200%")], pad_x=8,
        )
        self.ini_choice(
            page, "overlay_opacity", "HUD Plate Opacity", "Background darkness behind telemetry numbers.",
            [("0", "Transparent"), ("25", "25%"), ("50", "50%"), ("75", "75%"), ("100", "Solid")], pad_x=8,
        )

        self.section_divider(page, "ENGINE DIAGNOSTICS & PROFILING")

        self.widgets["diagnostics"] = self.row(
            page, "Deep Buffer Capture & Diagnostics Log",
            "KEEP OFF FOR NORMAL PLAY! Turning this on captures GPU command buffers for debugging and reduces CPU FPS.",
            lambda p: Toggle(p, self.config.get("diagnostics", False),
                             lambda v: self.change_config("diagnostics", v)),
        )

        info_box = tk.Frame(page, bg=PANEL, highlightthickness=1, highlightbackground=LINE)
        info_box.pack(side="top", fill="x", pady=(10, 4), ipadx=10, ipady=8)
        tk.Label(
            info_box,
            text="MSI Afterburner / RivaTuner (RTSS) Compatibility:\n"
                 "Selecting DirectX 11 or DirectX 12 presents through a hardware IDXGISwapChain3, allowing RTSS, "
                 "PresentMon and Windows DWM DirectFlip to hook natively alongside the built-in HUD.",
            font=self.f_small, bg=PANEL, fg=MUTED, anchor="w", justify="left", wraplength=710,
        ).pack(side="top", fill="x", padx=8)

        return page


    def page_extras(self):
        page = self.page()
        self.section_divider(page, "GAME DIRECTORY & UPDATE 1.02 VERIFICATION")

        info = tk.Frame(page, bg=PANEL, highlightthickness=1, highlightbackground=LINE)
        info.pack(side="top", fill="x", pady=(0, 8), ipadx=8, ipady=6)
        self.game_dir_label = tk.Label(info, text=f"Game Folder: {self.game_dir()}", font=self.f_small_bold,
                                       bg=PANEL, fg=TEXT, anchor="w")
        self.game_dir_label.pack(side="top", fill="x", padx=8)
        self.update_label = tk.Label(info, text=self.update_text(), font=self.f_small,
                                     bg=PANEL, fg=GOLD_HI, anchor="w", justify="left", wraplength=705)
        self.update_label.pack(side="top", fill="x", padx=8, pady=(2, 0))
        self.ini_label = tk.Label(info, text=f"Active INI: {ini_file(self.config['game'])}", font=self.f_caps,
                                  bg=PANEL, fg=DIM, anchor="w")
        self.ini_label.pack(side="top", fill="x", padx=8, pady=(2, 0))

        btn_row1 = tk.Frame(page, bg=BG)
        btn_row1.pack(side="top", fill="x", pady=(0, 6))
        for lbl, act in (
            ("Choose Game Folder", self.choose_game_dir),
            ("Install Base .pkg", self.install_game_pkg),
            ("Choose Update 1.02 Folder", self.choose_update_folder),
            ("Install Update .pkg", self.install_game_update),
            ("Verify Game Files", self.check_game_files),
        ):
            self.flat_button(btn_row1, lbl, act).pack(side="left", padx=(0, 6))

        self.section_divider(page, "PROFILES, LANGUAGE & AUDIO")

        self.widgets["save_profile"] = self.row(
            page, "Save Profile Slot",
            "Separate save progression per PS4 user slot (savedata/1, savedata/2, savedata/3).",
            lambda p: Segmented(p, [("1", "Profile 1"), ("2", "Profile 2"), ("3", "Profile 3")],
                                str(self.game_settings().get("save_profile", "1")),
                                lambda v: self.change_game_setting("save_profile", v), self.f_small, pad_x=9),
        )

        def lang_factory(parent):
            voices = voice_languages(self.game_dir())
            opts = [(code, label + (" (+ voices)" if code in voices else "")) for code, label in LANGUAGES]
            return Dropdown(parent, opts, self.config.get("language", "1"),
                            lambda v: self.change_config("language", v), self.f_small)
        self.widgets["language"] = self.row(
            page, "System & Subtitle Language",
            "Selects the Collection language (press Square in the game's main menu to toggle).",
            lang_factory,
        )

        self.widgets["speakers"] = self.row(
            page, "Audio Speaker Mix",
            "Selects native Naughty Dog Stereo, 5.1 or 7.1 surround mix.",
            lambda p: Segmented(p, [("auto", "Auto"), ("2", "Stereo"), ("6", "5.1"), ("8", "7.1")],
                                str(self.config.get("speakers", "auto")),
                                lambda v: self.change_config("speakers", v), self.f_small, recommended="auto", pad_x=9),
        )

        self.widgets["torch_forward"] = self.row(
            page, "Flashlight / Torch Follows Nate (Uncharted 2)",
            "Points flashlight beam where Nate faces rather than camera center. Requires Update 1.02.",
            lambda p: Toggle(p, self.config.get("torch_forward", True),
                             lambda v: self.change_config("torch_forward", v)),
        )

        self.section_divider(page, "CHAPTER UNLOCKS & DEVELOPER SWITCHES (UPDATE 1.02)")

        self.widgets["unlock"] = self.row(
            page, "Unlock All Chapters & Brutal Difficulty",
            "Opens Chapter Select and Brutal difficulty across all three games without altering saves.",
            lambda p: Toggle(p, self.config.get("unlock", False), lambda v: self.change_config("unlock", v)),
        )
        self.widgets["unlock_rewards"] = self.row(
            page, "Unlock All Bonus Skins, Render Modes & Galleries",
            "Enables all bonus rewards while active.",
            lambda p: Toggle(p, self.config.get("unlock_rewards", False),
                             lambda v: self.change_config("unlock_rewards", v)),
        )
        for cname, ctitle, chint in CHEATS:
            ckey = "cheat_" + cname
            self.widgets[ckey] = self.row(
                page, ctitle, chint,
                lambda p, k=ckey: Toggle(p, self.config.get(k, False), lambda v, kk=k: self.change_config(kk, v)),
            )

        self.section_divider(page, "SHORTCUTS, STEAM INTEGRATION & MAINTENANCE")

        btn_row2 = tk.Frame(page, bg=BG)
        btn_row2.pack(side="top", fill="x", pady=(0, 6))
        for lbl, act in (
            ("Desktop Shortcut", self.desktop_shortcut),
            ("Per-Game Shortcuts", self.game_shortcuts),
            ("Add Collection to Steam", lambda: self.add_to_steam(per_game=False)),
            ("Add 3 Games to Steam", lambda: self.add_to_steam(per_game=True)),
        ):
            self.flat_button(btn_row2, lbl, act).pack(side="left", padx=(0, 6))

        btn_row3 = tk.Frame(page, bg=BG)
        btn_row3.pack(side="top", fill="x", pady=(0, 8))
        for lbl, act in (
            ("Open Logs", self.open_logs),
            ("Open Saves", self.open_saves),
            ("Clear Shader Cache", self.clear_cache),
            ("Check Updates", lambda: self.check_updates(manual=True)),
            ("Reset Settings", self.reset),
        ):
            self.flat_button(btn_row3, lbl, act).pack(side="left", padx=(0, 6))

        tk.Label(page, text=CREDITS, font=self.f_caps, bg=BG, fg=DIM, anchor="w",
                 justify="left", wraplength=720).pack(side="top", fill="x", pady=(4, 4))
        return page

    # -- Multiplayer Preview Page -----------------------------------------------------------

    def text_entry(self, parent, key, default, width=18):
        value = tk.StringVar(value=str(self.config.get(key, default)))
        well = tk.Frame(parent, bg=PANEL, highlightthickness=1, highlightbackground=LINE)
        entry = tk.Entry(well, textvariable=value, width=width, font=self.f_body, bg=PANEL, fg=TEXT,
                         insertbackground=GOLD, relief="flat", highlightthickness=0)
        entry.pack(fill="x", expand=True, padx=6, pady=3)
        entry.bind("<FocusIn>", lambda e: well.config(highlightbackground=GOLD))
        entry.bind("<FocusOut>", lambda e: well.config(highlightbackground=LINE))
        value.trace_add("write", lambda *a: self.change_config(key, value.get().strip()))
        return well

    def page_multiplayer(self):
        page = self.page()
        banner = tk.Frame(page, bg=PANEL, highlightthickness=1, highlightbackground=LINE)
        banner.pack(side="top", fill="x", pady=(0, 8), ipadx=8, ipady=6)
        tk.Label(banner, text="Uncharted 2 Co-op & LAN Multiplayer (Preview)", font=self.f_body_bold,
                 bg=PANEL, fg=GOLD_HI, anchor="w").pack(side="left", padx=8)
        characters = net_characters(self.game_dir())
        skin_val = str(self.config.get("net_skin", ""))
        if skin_val not in {a for a, _ in characters}:
            skin_val = ""
        Dropdown(banner, characters, skin_val, lambda v: self.change_config("net_skin", v),
                 self.f_small).pack(side="right", padx=8)
        self.text_entry(banner, "net_name", "Drake", width=16).pack(side="right", padx=8)

        cols = tk.Frame(page, bg=BG)
        cols.pack(side="top", fill="both", expand=True)
        cols.columnconfigure(0, weight=1, uniform="mp")
        cols.columnconfigure(1, weight=1, uniform="mp")

        host_box = tk.Frame(cols, bg=PANEL, highlightthickness=1, highlightbackground=LINE, padx=12, pady=10)
        host_box.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        tk.Label(host_box, text="HOST A MATCH", font=self.f_body_bold, bg=PANEL, fg=GOLD_HI, anchor="w").pack(fill="x")
        games = net_games()
        g_val = str(self.config.get("net_game", games[0]["index"]))
        if g_val not in {str(g["index"]) for g in games}:
            g_val = str(games[0]["index"])
        Dropdown(host_box, [(str(g["index"]), g["label"]) for g in games], g_val,
                 lambda v: self.change_config("net_game", v), self.f_small).pack(fill="x", pady=(6, 10))
        self.gold_button(host_box, "HOST LAN GAME", self.host_net_game, small=True).pack(anchor="w", pady=(0, 8))
        self.gold_button(host_box, "LOCAL SPLIT CO-OP (2 PADS)", self.local_net_game, small=True).pack(anchor="w")

        join_box = tk.Frame(cols, bg=PANEL, highlightthickness=1, highlightbackground=LINE, padx=12, pady=10)
        join_box.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        jhead = tk.Frame(join_box, bg=PANEL)
        jhead.pack(fill="x")
        tk.Label(jhead, text="JOIN A MATCH", font=self.f_body_bold, bg=PANEL, fg=GOLD_HI).pack(side="left")
        self.flat_button(jhead, "Refresh", self.search_net_games).pack(side="right")
        self.net_list = tk.Frame(join_box, bg=PANEL)
        self.net_list.pack(fill="x", pady=8)
        self.show_net_games(None)
        addr_row = tk.Frame(join_box, bg=PANEL)
        addr_row.pack(fill="x")
        self.text_entry(addr_row, "net_address", "", width=18).pack(side="left", fill="x", expand=True)
        self.gold_button(addr_row, "JOIN", lambda: self.join_net_game(self.config.get("net_address", "")),
                         small=True).pack(side="right", padx=(6, 0))
        return page

    def search_net_games(self):
        if getattr(self, "net_searching", False) or not hasattr(self, "net_list"):
            return
        self.net_searching = True
        self.show_net_games("searching")
        found = queue.Queue()
        threading.Thread(target=lambda: found.put(self._find_net_games()), daemon=True).start()

        def done():
            try:
                games = found.get_nowait()
            except queue.Empty:
                self.root.after(100, done)
                return
            self.net_searching = False
            self.show_net_games(games)
        self.root.after(100, done)

    @staticmethod
    def _find_net_games():
        if net_lan is None:
            return OSError("launcher/net_lan.py is missing")
        try:
            return net_lan.find_games()
        except OSError as error:
            return error

    def show_net_games(self, games):
        for child in self.net_list.winfo_children():
            child.destroy()
        if not isinstance(games, list) or not games:
            msg = ("Searching local network..." if games == "searching"
                   else f"Search error: {games}" if isinstance(games, OSError)
                   else "Press Refresh to scan LAN, or enter Host IP below.")
            tk.Label(self.net_list, text=msg, font=self.f_small, bg=PANEL, fg=MUTED,
                     anchor="w", justify="left", wraplength=310).pack(fill="x", pady=4)
            return
        for g in games:
            line = tk.Frame(self.net_list, bg=BG, highlightthickness=1, highlightbackground=LINE)
            line.pack(fill="x", pady=2)
            address = f"{g['address']}:{g['port']}"
            self.gold_button(line, "JOIN", lambda a=address: self.join_net_game(a), small=True).pack(side="right", padx=6, pady=4)
            tk.Label(line, text=f"{g['title']} ({g['players']}/{g['most']}) • {g['address']}",
                     font=self.f_small, bg=BG, fg=TEXT, anchor="w").pack(side="left", padx=6)

    def host_net_game(self):
        games = net_games()
        index = str(self.config.get("net_game", games[0]["index"]))
        label = next((g["label"] for g in games if str(g["index"]) == index), games[0]["label"])
        self.play(net={"mode": "host", "address": "0.0.0.0:27000", "game": index, "title": label})

    def local_net_game(self):
        games = net_games()
        index = str(self.config.get("net_game", games[0]["index"]))
        label = next((g["label"] for g in games if str(g["index"]) == index), games[0]["label"])
        self.play(net={"mode": "host", "address": "127.0.0.1:27000", "game": index, "title": label, "local": True})

    def join_net_game(self, address):
        address = (address or "").strip()
        if not address:
            messagebox.showinfo(APP_TITLE, "Enter the host's IP address (e.g. 192.168.1.20) or Refresh LAN list.")
            return
        self.play(net={"mode": "join", "address": address})

    # -- Control Binding Capture & Synchronization ------------------------------------------

    def game_controls(self):
        return merged_controls(self.game_settings().get("controls"))

    def change_prompts(self, value):
        self.change_game_setting("prompts", value)
        self.refresh_pad_slots()

    def pad_style(self):
        style = self.game_settings().get("prompts", "auto")
        if style in ("xbox", "playstation"):
            return style
        return self.gamepads.style() or "xbox"

    def refresh_pad_slots(self):
        style = self.pad_style()
        controls = merged_pad_controls(self.game_settings().get("pad_controls"))
        height = 20
        for action, cell in getattr(self, "pad_slots", {}).items():
            if self.pad_capture == action:
                cell.config(image="", text="Press…", fg=GOLD_HI, bg=PANEL_HI, width=9)
                continue
            button = controls.get(action) if action in PAD_DEFAULTS else PAD_FIXED.get(action)
            if not button:
                cell.config(image="", text="—", fg=DIM, bg=PANEL, width=9)
                continue
            if style == "playstation" and button == PAD_DEFAULTS.get(action, PAD_FIXED.get(action)) \
                    and action in ("touchpad_left", "touchpad_right"):
                cell.config(image="", text="TP-L" if action == "touchpad_left" else "TP-R",
                            fg=GOLD_HI, bg=PANEL, width=9)
                continue
            if ImageTk is None or Image is None:
                cell.config(image="", text=pad_button_text(button, style), fg=GOLD_HI, bg=PANEL, width=9)
                continue
            key = (button, style, height)
            if key not in self.pad_images:
                self.pad_images[key] = ImageTk.PhotoImage(pad_icon(button, style, height))
            cell.config(image=self.pad_images[key], text="", bg=PANEL, width=68, height=22)

    def start_pad_capture(self, action):
        self.stop_capture()
        if not self.gamepads.sdl:
            self.controls_hint.config(text="SDL3.dll gamepad support not detected.", fg=STOP_RED)
            return
        self.pad_capture = action
        self.pad_capture_started = time.monotonic()
        self.pad_capture_held = self.gamepads.held()
        if not self.gamepads.pads:
            self.controls_hint.config(text="Connect a controller first, then click the slot again.", fg=STOP_RED)
            self.pad_capture = None
            return
        self.controls_hint.config(text="Press a controller button (Escape cancels)...", fg=GOLD_HI)
        self.refresh_pad_slots()
        self.root.after(30, self.poll_pad_capture)

    def poll_pad_capture(self):
        if self.pad_capture is None:
            return
        held = self.gamepads.held()
        new = [b for b in SDL_BUTTONS if b in held and b not in self.pad_capture_held]
        self.pad_capture_held &= held
        if new:
            self.set_pad_binding(self.pad_capture, new[0])
            return
        if time.monotonic() - self.pad_capture_started > 8:
            self.stop_pad_capture()
            return
        self.root.after(30, self.poll_pad_capture)

    def stop_pad_capture(self):
        self.pad_capture = None
        self.refresh_pad_slots()

    def set_pad_binding(self, action, button):
        settings = self.game_settings()
        pad_controls = dict(settings.get("pad_controls") or {})
        merged = merged_pad_controls(pad_controls)
        if button:
            for other, current in merged.items():
                if current == button and other != action:
                    pad_controls[other] = merged[action]
        pad_controls[action] = button or ""
        settings["pad_controls"] = {
            a: b for a, b in merged_pad_controls(pad_controls).items() if b != PAD_DEFAULTS.get(a)
        }
        self.save_controls()
        self.pad_capture = None
        self.refresh_pad_slots()
        self.controls_hint.config(text=f"Controller binding saved for {self.current_game()['name']}.", fg=GOLD_HI)

    def refresh_bindings(self):
        controls = self.game_controls()
        for (action, slot), cell in getattr(self, "binding_slots", {}).items():
            bindings = controls.get(action, [])
            capturing = self.capturing == (action, slot)
            cell.config(
                text="Press…" if capturing else binding_label(bindings[slot] if slot < len(bindings) else None),
                bg=PANEL_HI if capturing else PANEL,
                fg=GOLD_HI if capturing else TEXT,
            )

    def start_capture(self, action, slot):
        self.stop_capture()
        self.capturing = (action, slot)
        self.refresh_bindings()
        self.root.focus_set()
        self.root.after(180, self.arm_capture)

    def arm_capture(self):
        if self.capturing is None:
            return
        self.root.bind_all("<KeyPress>", self.capture_key)
        self.root.bind_all("<ButtonPress>", self.capture_button)
        self.root.bind_all("<MouseWheel>", self.capture_wheel)

    def stop_capture(self):
        if self.capturing is None:
            return
        self.capturing = None
        for seq in ("<KeyPress>", "<ButtonPress>", "<MouseWheel>"):
            self.root.unbind_all(seq)
        self.root.bind_all("<MouseWheel>", self.wheel)
        self.refresh_bindings()

    def capture_key(self, event):
        keysym = event.keysym
        if keysym in RESERVED_KEYS:
            self.controls_hint.config(text=f"{keysym} is reserved by the runtime.", fg=STOP_RED)
            self.stop_capture()
            return "break"
        name = TK_TO_SDL.get(keysym) or (keysym.upper() if len(keysym) == 1 and keysym.isalnum() else None)
        if not name:
            self.controls_hint.config(text=f"Key {keysym} cannot be bound.", fg=STOP_RED)
            self.stop_capture()
            return "break"
        action, slot = self.capturing
        self.set_binding(action, slot, f"key:{name}")
        return "break"

    def capture_button(self, event):
        names = {1: "left", 2: "middle", 3: "right", 4: "x1", 5: "x2", 8: "x1", 9: "x2"}
        if event.num in names:
            self.capture_mouse(names[event.num])
        return "break"

    def capture_wheel(self, event):
        self.capture_mouse("wheel_up" if event.delta > 0 else "wheel_down")
        return "break"

    def capture_mouse(self, name):
        action, slot = self.capturing
        if action in STICK_ACTIONS:
            self.controls_hint.config(text="Movement/camera stick directions require keyboard keys.", fg=STOP_RED)
            self.stop_capture()
            return
        self.set_binding(action, slot, f"mouse:{name}")

    def set_binding(self, action, slot, binding):
        controls = self.game_controls()
        if binding:
            for other, bindings in controls.items():
                if binding in bindings and (other, bindings.index(binding)) != (action, slot):
                    bindings.remove(binding)
        bindings = controls[action]
        while len(bindings) <= slot:
            bindings.append(None)
        bindings[slot] = binding
        controls[action] = [b for b in bindings if b][:2]
        self.game_settings()["controls"] = controls
        self.save_controls()
        self.controls_hint.config(text=f"Binding saved for {self.current_game()['name']}.", fg=GOLD_HI)
        self.stop_capture()

    def reset_controls(self):
        name = self.current_game()["name"]
        if not messagebox.askyesno(APP_TITLE, f"Reset {name}'s keyboard, mouse and controller bindings to defaults?"):
            return
        settings = self.game_settings()
        settings.update({
            "controls": None, "pad_controls": None, "mouse": True, "mouse_sens": "1.0",
            "mouse_invert": False, "prompts": "auto", "mouse_aim_sens": "1.0",
            "mouse_vertical": "1.0", "mouse_invert_x": False, "vibration": True,
        })
        self.save_controls()
        self.refresh_controls()


    # -- Settings Mutation & Two-Way Persistence -------------------------------------------

    def refresh_controls(self):
        for key in INI_DEFAULTS:
            widget = self.widgets.get(key)
            if isinstance(widget, Toggle):
                widget.set(self.ini.get(key, INI_DEFAULTS[key]) == "1")
            elif isinstance(widget, Segmented):
                widget.set(self.ini.get(key, INI_DEFAULTS[key]))

        for key, widget in getattr(self, "overlay_chips", {}).items():
            if isinstance(widget, Chip):
                widget.set(self.ini.get(key, INI_DEFAULTS[key]) == "1")

        settings = self.game_settings()
        if isinstance(self.widgets.get("preset_seg"), Segmented):
            cur_p = settings.get("preset", "medium")
            self.widgets["preset_seg"].set(cur_p if cur_p in PRESETS else "custom")
        if isinstance(self.widgets.get("graphics_api"), Segmented):
            self.widgets["graphics_api"].set(normalize_graphics_api(self.config.get("graphics_api", "dx11")))
        if isinstance(self.widgets.get("vram_budget_mb"), Segmented):
            self.widgets["vram_budget_mb"].set(
                normalize_vram_cap(self.config.get("vram_budget_mb"), self.config.get("texture_quality", "medium"))
            )
        if isinstance(self.widgets.get("texture_quality"), Segmented):
            self.widgets["texture_quality"].set(self.config.get("texture_quality", "medium"))
        if isinstance(self.widgets.get("shadow_quality"), Segmented):
            self.widgets["shadow_quality"].set(self.config.get("shadow_quality", "medium"))
        if isinstance(self.widgets.get("shadow_distance"), Segmented):
            sd = self.ini.get("sun_shadow_distance", "100")
            self.widgets["shadow_distance"].set(sd if sd in {k for k, _ in DISTANCE_SCALES} else "100")
        if isinstance(self.widgets.get("ssao_quality"), Segmented):
            self.widgets["ssao_quality"].set(self.config.get("ssao_quality", "full"))
        if isinstance(self.widgets.get("anisotropy"), Segmented):
            self.widgets["anisotropy"].set(str(self.config.get("anisotropy", "16")))
        if isinstance(self.widgets.get("dof_mode"), Segmented):
            self.widgets["dof_mode"].set(self.config.get("dof_mode", "enabled"))
        if isinstance(self.widgets.get("motion_blur_mode"), Segmented):
            self.widgets["motion_blur_mode"].set(self.config.get("motion_blur_mode", "enabled"))
        if isinstance(self.widgets.get("frame_pacer"), Segmented):
            self.widgets["frame_pacer"].set(
                normalize_frame_pacer(self.config.get("frame_pacing_mode", self.ini.get("frame_pacer", "hybrid")))
            )
        for tkey in ("full_res_textures", "hires_shadows", "diagnostics", "movie_stretch",
                     "torch_forward", "unlock", "unlock_rewards"):
            if isinstance(self.widgets.get(tkey), Toggle):
                self.widgets[tkey].set(bool(self.config.get(tkey, False)))
        for cname, *_ in CHEATS:
            ckey = "cheat_" + cname
            if isinstance(self.widgets.get(ckey), Toggle):
                self.widgets[ckey].set(bool(self.config.get(ckey, False)))

        if isinstance(self.widgets.get("resolution"), Segmented):
            self.widgets["resolution"].set(settings["resolution"])
        if isinstance(self.widgets.get("aspect"), Segmented):
            self.widgets["aspect"].set(settings.get("aspect", "16:9"))
        if isinstance(self.widgets.get("multithreaded"), Toggle):
            self.widgets["multithreaded"].set(bool(settings.get("multithreaded", True)))
        if isinstance(self.widgets.get("save_profile"), Segmented):
            self.widgets["save_profile"].set(str(settings.get("save_profile", "1")))
        if isinstance(self.widgets.get("mouse"), Toggle):
            self.widgets["mouse"].set(settings.get("mouse", True))
            self.widgets["mouse_sens"].set(settings.get("mouse_sens", "1.0"))
            self.widgets["mouse_invert"].set(settings.get("mouse_invert", False))
            self.widgets["mouse_aim_sens"].set(settings.get("mouse_aim_sens", "1.0"))
            self.widgets["mouse_vertical"].set(settings.get("mouse_vertical", "1.0"))
            self.widgets["mouse_invert_x"].set(settings.get("mouse_invert_x", False))
            self.widgets["vibration"].set(settings.get("vibration", True))
            self.widgets["prompts"].set(settings.get("prompts", "auto"))
            self.refresh_pad_slots()
        if hasattr(self, "binding_slots"):
            self.refresh_bindings()
        self.sync_sharpening()
        self.refresh_render_size()
        self.update_fps_note()
        if hasattr(self, "ini_label"):
            self.ini_label.config(text=f"Active INI: {ini_file(self.config['game'])}")
        self.draw_expeditions_hud()
        self.draw_play()

    def apply_preset(self, name):
        if name not in PRESETS:
            return
        preset = PRESETS[name]
        settings = self.game_settings()
        resolution = preset["resolution"]
        upscale = preset["upscale"]
        values = preset_values(name)

        if name == "igpu":
            kind = igpu_class(self.gpu_name)
            if kind == "weak":
                upscale, values["fps_limit"] = "ultra", "30"
            elif kind == "strong":
                upscale = "quality"

        settings["resolution"] = resolution
        settings["upscale"] = upscale
        settings["multithreaded"] = True
        settings["aa"] = "taa" if values.get("upscaler") == "taa" else "off"
        settings["preset"] = name

        self.config["vram_budget_mb"] = preset["vram_budget_mb"]
        self.config["texture_quality"] = preset["texture_quality"]
        self.config["shadow_quality"] = preset["shadow_quality"]
        self.config["ssao_quality"] = preset["ssao_quality"]
        self.config["full_res_textures"] = preset["full_res_textures"]
        self.config["hires_shadows"] = preset["hires_shadows"]

        values["graphics_api"] = normalize_graphics_api(self.config.get("graphics_api", "dx11"))
        values["vram_budget_mb"] = preset["vram_budget_mb"]
        values["frame_pacer"] = normalize_frame_pacer(self.config.get("frame_pacing_mode", "hybrid"))
        values["deferred_readback"] = "1"
        values["stale_readback"] = "0"

        self.ini.update(values)
        write_config(self.config)
        self.save_ini(values)
        self.sync_global_to_all_inis()
        self.refresh_controls()
        self.set_status(f"Applied {preset['title']} Preset to {self.current_game()['name']}.", "")

    def change_graphics_api(self, value):
        api = normalize_graphics_api(value)
        self.config["graphics_api"] = api
        write_config(self.config)
        self.sync_global_to_all_inis()
        self.draw_play()

    def change_vram_cap(self, value):
        cap = normalize_vram_cap(value, self.config.get("texture_quality", "medium"))
        self.config["vram_budget_mb"] = cap
        if cap == "820":
            self.config["full_res_textures"] = False
            if isinstance(self.widgets.get("full_res_textures"), Toggle):
                self.widgets["full_res_textures"].set(False)
        write_config(self.config)
        self.sync_global_to_all_inis()
        self.mark_custom_preset()
        self.refresh_render_size()
        self.draw_play()

    def change_texture_quality(self, value):
        self.config["texture_quality"] = value
        cap_map = {"low": "820", "medium": "1720", "high": "3200", "ultra": "6144"}
        if value in cap_map:
            self.config["vram_budget_mb"] = cap_map[value]
            if isinstance(self.widgets.get("vram_budget_mb"), Segmented):
                self.widgets["vram_budget_mb"].set(cap_map[value])
        if value in ("low", "medium"):
            self.config["full_res_textures"] = False
            if isinstance(self.widgets.get("full_res_textures"), Toggle):
                self.widgets["full_res_textures"].set(False)
        write_config(self.config)
        self.sync_global_to_all_inis()
        self.mark_custom_preset()
        self.refresh_render_size()
        self.draw_play()

    def change_shadow_quality(self, value):
        self.config["shadow_quality"] = value
        if value in ("low", "medium"):
            self.config["hires_shadows"] = False
            if isinstance(self.widgets.get("hires_shadows"), Toggle):
                self.widgets["hires_shadows"].set(False)
        elif value == "ultra":
            self.config["hires_shadows"] = True
            if isinstance(self.widgets.get("hires_shadows"), Toggle):
                self.widgets["hires_shadows"].set(True)
        write_config(self.config)
        self.mark_custom_preset()

    def change_shadow_distance(self, value):
        self.ini["sun_shadow_distance"] = value
        self.ini["light_shadow_distance"] = value
        self.save_ini({"sun_shadow_distance": value, "light_shadow_distance": value})
        self.mark_custom_preset()

    def change_frame_pacer(self, value):
        mode = normalize_frame_pacer(value)
        self.config["frame_pacing_mode"] = mode
        write_config(self.config)
        self.sync_global_to_all_inis()
        self.draw_play()

    def mark_custom_preset(self):
        if self.game_settings().get("preset") != "custom":
            self.game_settings()["preset"] = "custom"
            write_config(self.config)
            if isinstance(self.widgets.get("preset_seg"), Segmented):
                self.widgets["preset_seg"].set("custom")
            self.draw_expeditions_hud()

    def change(self, key, value):
        self.ini[key] = value
        self.save_ini({key: value})
        if key not in ("show_fps", "overlay_fps", "overlay_frametime", "overlay_gpu", "overlay_cpu",
                       "overlay_ram", "overlay_vram", "overlay_upscaler", "overlay_corner",
                       "overlay_layout", "overlay_scale", "overlay_opacity"):
            self.mark_custom_preset()
        if key == "fps_limit":
            self.update_fps_note()
        if key in ("upscaler", "fsr1"):
            self.refresh_render_size()
        self.draw_play()

    def change_sharpening(self, value):
        if value == "off":
            self.change("rcas", "0")
        else:
            self.ini["rcas"] = "1"
            self.ini["rcas_strength"] = value
            self.save_ini({"rcas": "1", "rcas_strength": value})
            self.mark_custom_preset()

    def sync_sharpening(self):
        widget = self.widgets.get("sharpening")
        if widget:
            widget.set(self.ini.get("rcas_strength", "75") if self.ini.get("rcas", "1") == "1" else "off")

    def update_fps_note(self):
        if not hasattr(self, "fps_note"):
            return
        limit = int(self.ini.get("fps_limit", "60"))
        hz = monitor_refresh_hz()
        if limit > 60:
            self.fps_note.config(text=f"High-refresh mode active: {limit} FPS target (Monitor: {hz} Hz).")
        else:
            self.fps_note.config(text="")

    def change_upscaling(self, value):
        settings = self.game_settings()
        aa = anti_aliasing_choice(self.ini, settings)
        settings["aa"] = aa
        if value == "fsr1" and settings.get("upscale", "off") == "off":
            settings["upscale"] = "quality"
        write_config(self.config)
        self.ini["fsr1"] = "1" if value == "fsr1" else "0"
        self.save_ini({"fsr1": self.ini["fsr1"]})
        self.change("upscaler", value if value in TEMPORAL_UPSCALERS else aa)

    def change_upscale_quality(self, value):
        self.game_settings()["upscale"] = value
        self.mark_custom_preset()
        write_config(self.config)
        self.refresh_render_size()
        self.draw_play()

    def change_anti_aliasing(self, value):
        self.change_game_setting("aa", value)
        self.change("upscaler", value)

    def change_resolution(self, value):
        self.game_settings()["resolution"] = value
        self.mark_custom_preset()
        write_config(self.config)
        self.refresh_render_size()
        self.draw_play()

    def change_aspect(self, value):
        self.change_game_setting("aspect", value)
        self.refresh_render_size()
        self.draw_play()

    def refresh_render_size(self):
        settings = self.game_settings()
        aspect = settings.get("aspect", "16:9")
        vram = normalize_vram_cap(self.config.get("vram_budget_mb"), self.config.get("texture_quality", "medium"))
        if isinstance(self.widgets.get("resolution"), Segmented):
            self.widgets["resolution"].relabel(resolution_options(aspect))
        if hasattr(self, "render_size_note"):
            render, output = game_sizes(settings, upscaling_choice(self.ini), vram)
            if output:
                self.render_size_note.config(
                    text=f"Active Pipeline: Internal 3D {render.replace('x', ' × ')}  →  Output {output.replace('x', ' × ')}"
                )
            else:
                self.render_size_note.config(text=f"Active Pipeline: Native {render.replace('x', ' × ')}")
        self.refresh_upscale()

    def refresh_upscale(self):
        if not hasattr(self, "upscale_row"):
            return
        settings = self.game_settings()
        upscaling = upscaling_choice(self.ini)
        vram = normalize_vram_cap(self.config.get("vram_budget_mb"), self.config.get("texture_quality", "medium"))
        if "upscaling" in self.widgets:
            self.widgets["upscaling"].set(upscaling)
        if upscaling != "off":
            if not self.upscale_row.winfo_manager() and "upscaling" in self.widgets:
                self.upscale_row.pack(side="top", fill="x", pady=(0, 7), after=self.widgets["upscaling"].master)
        elif self.upscale_row.winfo_manager():
            self.upscale_row.pack_forget()

        quality_seg = self.upscale_row.winfo_children()[-1]
        if isinstance(quality_seg, Segmented):
            presets = [opt for opt, _ in UPSCALE_PRESETS]
            quality_seg.show(presets[1:] if upscaling == "fsr1" else presets)
            quality_seg.relabel([("off", "DLAA" if upscaling == "dlss" else "Native AA")])
            quality_seg.set(settings.get("upscale", "off"))

        if "aa" in self.widgets:
            self.widgets["aa"].set(anti_aliasing_choice(self.ini, settings))
            self.widgets["aa"].set_enabled(upscaling not in TEMPORAL_UPSCALERS)
        render, output = game_sizes(settings, upscaling, vram)
        if hasattr(self, "upscale_note"):
            self.upscale_note.config(
                text=f"Draws {render.replace('x', ' × ')} → Upscaled to {output.replace('x', ' × ')}" if output else ""
            )

    def change_game_setting(self, key, value):
        self.game_settings()[key] = value
        if key in ("mouse", "mouse_sens", "mouse_invert", "prompts", "mouse_aim_sens",
                   "mouse_vertical", "mouse_invert_x", "vibration"):
            self.save_controls()
        else:
            write_config(self.config)
        self.draw_play()

    def change_config(self, key, value):
        self.config[key] = value
        write_config(self.config)
        self.draw_play()

    # -- Game Execution, Process Monitoring & Gamepad Navigation ----------------------------

    def game_dir(self):
        folder = self.config.get("game_dir") or ""
        if folder and is_game_dir(folder):
            return Path(folder)
        if WINE and folder and not Path(folder).exists():
            here = windows_path(self.config.get("game_dir_unix"))
            if here and is_game_dir(here):
                return Path(here)

        # Automatic detection from known user paths, logs, and sibling locations
        candidates = [
            Path("D:/D'Arby/Uncharted Game/CUSA02344"),
            DEFAULT_GAME_DIR,
            ROOT / "game" / "CUSA02344",
            ROOT.parent / "game" / "CUSA02344",
        ]
        for log_path in (USER_DIR / "launcher-last-run.log", ROOT / "analysis/uncharted-user/launcher-last-run.log"):
            try:
                if log_path.is_file():
                    first_line = log_path.read_text(encoding="utf-8", errors="replace").splitlines()[0]
                    if "--game" in first_line:
                        parts = first_line.split("--game", 1)[1].strip()
                        extracted = parts.split(" --")[0].strip('"\'')
                        if extracted:
                            candidates.insert(0, Path(extracted))
            except Exception:
                pass

        for drive in ("D:", "E:", "C:"):
            try:
                d_p = Path(f"{drive}/D'Arby/Uncharted Game/CUSA02344")
                if d_p not in candidates:
                    candidates.append(d_p)
            except Exception:
                pass

        for cand in candidates:
            try:
                if cand and is_game_dir(cand):
                    self.config["game_dir"] = str(cand)
                    write_config(self.config)
                    return cand
            except Exception:
                pass

        return Path(folder) if folder else DEFAULT_GAME_DIR

    def choose_game_dir(self):
        from tkinter import filedialog
        folder = filedialog.askdirectory(
            parent=self.root,
            title="Select Uncharted Collection game folder (CUSA02344 with eboot.bin and sce_sys)",
            initialdir=str(self.game_dir() if self.game_dir().exists() else ROOT),
        )
        if not folder:
            return False
        if not is_game_dir(folder):
            messagebox.showerror(
                APP_TITLE,
                "Selected folder does not contain eboot.bin and sce_sys/param.sfo.\n\n"
                "Please choose the root game folder (CUSA02344 / uroot).",
                parent=self.root,
            )
            return False
        self.config["game_dir"] = folder
        if WINE:
            self.config["game_dir_unix"] = unix_path(folder)
        write_config(self.config)
        if hasattr(self, "game_dir_label"):
            self.game_dir_label.config(text=f"Game Folder: {folder}")
            self.update_label.config(text=self.update_text())
        return True

    def chosen_update(self):
        folder = self.config.get("update_dir") or ""
        if folder:
            for candidate in (Path(folder), Path(folder) / "uroot"):
                if is_game_dir(candidate):
                    return candidate
        return None

    def update_dir(self):
        return self.chosen_update() or game_update_dir(self.game_dir())

    def update_text(self):
        update = self.update_dir()
        if update:
            return f"Update {app_version(update) or '1.02'} Active: {update}"
        folder = self.game_dir()
        version = app_version(folder)
        if version and version >= "01.02":
            return f"Game Version {version} (Update 1.02 merged in base folder)."
        return f"Game Version {version or 'Unknown'} — Install Update 1.02 (.pkg or folder) for all stability patches."

    def play_or_stop(self):
        if self.process:
            self.stop()
        else:
            self.play()

    def play_game(self, key):
        self.select_game(key)
        if not self.process:
            self.play()

    def play(self, net=None):
        if net and self.process:
            messagebox.showinfo(APP_TITLE, "A game is already running.", parent=self.root)
            return
        if not RUN_SCRIPT.exists():
            messagebox.showerror(APP_TITLE, f"Launch script missing:\n{RUN_SCRIPT}", parent=self.root)
            return
        if not is_game_dir(self.game_dir()):
            messagebox.showinfo(
                APP_TITLE,
                "Select your Uncharted: The Nathan Drake Collection folder (CUSA02344, containing eboot.bin & sce_sys).",
                parent=self.root,
            )
            if not self.choose_game_dir():
                return
        if not self.confirm_without_update() or not self.confirm_update_files() or not self.confirm_slow_drive():
            return

        # Persist ALL Launcher & INI settings BEFORE spawning probe_uncharted.py
        api = normalize_graphics_api(self.config.get("graphics_api", "dx11"))
        vram_cap = normalize_vram_cap(self.config.get("vram_budget_mb"), self.config.get("texture_quality", "medium"))
        pacer = normalize_frame_pacer(self.config.get("frame_pacing_mode", "hybrid"))
        self.ini["graphics_api"] = api
        self.ini["vram_budget_mb"] = vram_cap
        self.ini["frame_pacer"] = pacer
        self.ini["deferred_readback"] = "1"
        self.ini["stale_readback"] = "0"

        self.save_ini({k: self.ini.get(k, v) for k, v in INI_DEFAULTS.items()})
        self.sync_global_to_all_inis()
        write_config(self.config)

        game = next(g for g in GAMES if g["key"] == "u2") if net else self.current_game()
        game_ini = read_ini(game["key"])
        env = os.environ.copy()

        for key in ("UNCHARTED_NET_MODE", "UNCHARTED_NET_ADDRESS", "UNCHARTED_NET_GAME", "UNCHARTED_NET_TITLE",
                    "UNCHARTED_NET_NAME", "UNCHARTED_NET_SKIN", "UNCHARTED_INSTANCE", "GOW3_PAD_INDEX",
                    "GOW3_WINDOW_HALF"):
            env.pop(key, None)
        if net:
            env["UNCHARTED_NET_MODE"] = net["mode"]
            env["UNCHARTED_NET_ADDRESS"] = net.get("address", "")
            env["UNCHARTED_NET_NAME"] = str(self.config.get("net_name", ""))[:31]
            env["UNCHARTED_NET_SKIN"] = str(self.config.get("net_skin", ""))
            if net.get("local"):
                env.update({"GOW3_PAD_INDEX": "1", "GOW3_WINDOW_HALF": "left", "GOW3_MOUSE": "0"})
            if net["mode"] == "host":
                env["UNCHARTED_NET_GAME"] = net["game"]
                env["UNCHARTED_NET_TITLE"] = net["title"]

        # Performance & Diagnostics (Diagnostics defaults OFF so GOW3_CAPTURE_ALL_BUFFERS never tanks FPS)
        env["UNCHARTED_DIAGNOSTICS"] = "1" if self.config.get("diagnostics", False) else "0"
        env["UNCHARTED_TORCH_FORWARD"] = "1" if self.config.get("torch_forward", True) else "0"
        env["UNCHARTED_JOB_SLEEP"] = {"off": "0", "on": "1"}.get(str(self.config.get("job_sleep", "auto")), "auto")

        active_preset = self.config.get("games", {}).get(game["key"], {}).get("preset", "medium")
        tex_q = self.config.get("texture_quality", "medium")
        shad_q = self.config.get("shadow_quality", "medium")

        # Presentation API, Strict VRAM Cap & Sub-ms Frame Pacer
        engine_cfg_path = str(ROOT / "EngineConfig.ini")
        env["GOW3_CONFIG"] = engine_cfg_path
        env["UNCHARTED_CONFIG_FILE"] = engine_cfg_path
        env["GOW3_GRAPHICS_API"] = api
        env["GOW3_VRAM_BUDGET_MB"] = vram_cap
        env["GOW3_FRAME_PACER"] = pacer
        fps_lim = str(game_ini.get("fps_limit", "60"))
        env["GOW3_FPS_LIMIT"] = fps_lim
        env["GOW3_VBLANK_HZ"] = "60" if fps_lim == "60" else "0"
        env["GOW3_VSYNC"] = str(game_ini.get("vsync", "1"))
        env["GOW3_FADE_DISTANCE"] = str(game_ini.get("fade_distance", "100"))
        env["GOW3_SUN_SHADOW_DISTANCE"] = str(game_ini.get("sun_shadow_distance", "100"))
        env["GOW3_LIGHT_SHADOW_DISTANCE"] = str(game_ini.get("light_shadow_distance", "100"))
        env["GOW3_TEXTURE_QUALITY"] = str(tex_q)
        env["GOW3_SHADOW_QUALITY"] = str(shad_q)
        env["GOW3_SSAO_QUALITY"] = str(self.config.get("ssao_quality", "full"))

        try:
            vram_int = int(vram_cap)
        except ValueError:
            vram_int = 0
        if (0 < vram_int <= 2048) or active_preset in ("low", "igpu"):
            env["GOW3_LOW_VRAM"] = "1"
        else:
            env["GOW3_LOW_VRAM"] = {"on": "1", "auto": "auto"}.get(str(self.config.get("low_vram", "auto")), "0")
        if (0 < vram_int <= 1024) or active_preset in ("low", "igpu"):
            env["GOW3_LOW_RAM"] = "1"

        allow_full_tex = (tex_q == "ultra" or bool(self.config.get("full_res_textures", False))) and not (0 < vram_int <= 1024)
        allow_hi_shad = (shad_q in ("high", "ultra") or bool(self.config.get("hires_shadows", False))) and shad_q != "low"

        env["UNCHARTED_FULL_RES_TEXTURES"] = "1" if allow_full_tex else "0"
        env["UNCHARTED_HIRES_SHADOWS"] = "1" if allow_hi_shad else "0"
        env["UNCHARTED_ANISOTROPY"] = str(self.config.get("anisotropy", "16"))
        env["UNCHARTED_NO_DOF"] = "1" if (self.config.get("dof_mode") == "disabled" or self.config.get("no_dof", False)) else "0"
        env["UNCHARTED_NO_MOTION_BLUR"] = "1" if (self.config.get("motion_blur_mode") == "disabled" or self.config.get("no_motion_blur", False)) else "0"
        env["UNCHARTED_LANGUAGE"] = str(self.config.get("language", "1"))
        env["UNCHARTED_SPEAKERS"] = str(self.config.get("speakers", "auto"))
        env["UNCHARTED_U3_EXPOSURE"] = str(self.config.get("u3_exposure", "0"))
        env["GOW3_MOVIE_STRETCH"] = "1" if self.config.get("movie_stretch", False) else "0"
        env["UNCHARTED_UNLOCK"] = "1" if self.config.get("unlock", False) else "0"
        env["UNCHARTED_UNLOCK_REWARDS"] = "1" if self.config.get("unlock_rewards", False) else "0"
        if self.chosen_update():
            env["UNCHARTED_UPDATE"] = str(self.chosen_update())
        env["UNCHARTED_CHEATS"] = ",".join(name for name, *_ in CHEATS if self.config.get("cheat_" + name, False))

        env.pop("GOW3_VBLANK_HZ", None)
        for key in GAME_KEYS:
            settings = self.config["games"][key]
            render, output = game_sizes(settings, upscaling_choice(read_ini(key)), vram_cap)
            env[f"UNCHARTED_RESOLUTION_{key.upper()}"] = render
            if output:
                env[f"UNCHARTED_OUTPUT_RES_{key.upper()}"] = output
            else:
                env.pop(f"UNCHARTED_OUTPUT_RES_{key.upper()}", None)
            env[f"UNCHARTED_MULTITHREADED_{key.upper()}"] = "1" if settings.get("multithreaded", True) else "0"
            env[f"UNCHARTED_SAVE_PROFILE_{key.upper()}"] = str(settings.get("save_profile", "1"))

        python = Path(sys.executable)
        if python.name.lower() == "pythonw.exe" and python.with_name("python.exe").exists():
            python = python.with_name("python.exe")
        command = [
            str(python), "-u", str(RUN_SCRIPT), "--boot", "--seconds", "0",
            "--executable", game["exe"], "--game", str(self.game_dir()),
        ]
        try:
            ctypes.windll.user32.AllowSetForegroundWindow(-1)
        except (AttributeError, OSError):
            pass
        try:
            self.process = subprocess.Popen(
                command, cwd=str(ROOT), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL, creationflags=NO_WINDOW, text=True, encoding="utf-8", errors="replace",
            )
        except OSError as error:
            messagebox.showerror(APP_TITLE, f"Could not start the game:\n{error}")
            self.process = None
            return

        self.started_at = time.time()
        self.last_output = time.time()
        self.game_started = False
        try:
            USER_DIR.mkdir(parents=True, exist_ok=True)
            self.run_log = open(USER_DIR / "launcher-last-run.log", "w", encoding="utf-8")
            self.run_log.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {' '.join(command)}\n")
        except OSError:
            self.run_log = None
        threading.Thread(target=self.read_output, args=(self.process,), daemon=True).start()

        self.partner = None
        if net and net.get("local"):
            partner_env = dict(env)
            name = str(self.config.get("net_name", "") or "Drake")
            partner_env.update({
                "UNCHARTED_INSTANCE": "2", "UNCHARTED_NET_MODE": "join",
                "UNCHARTED_NET_ADDRESS": "127.0.0.1:27000", "UNCHARTED_NET_NAME": (name + " 2")[:31],
                "UNCHARTED_NET_SKIN": str(self.config.get("net_skin2", "")),
                "GOW3_PAD_INDEX": "2", "GOW3_WINDOW_HALF": "right", "GOW3_MOUSE": "0",
            })
            for key in ("UNCHARTED_NET_GAME", "UNCHARTED_NET_TITLE"):
                partner_env.pop(key, None)
            self.partner_start = (self.process, command, partner_env)
            self.root.after(15000, self.start_partner)

        self.set_status(f"Launching {game['name']} ({api.upper()})...", "Applying binary patches & initializing presenter.")

    def start_partner(self):
        pending = self.partner_start
        self.partner_start = None
        if not pending or not self.process or pending[0] is not self.process or self.process.poll() is not None:
            return
        _, command, env = pending
        try:
            self.partner = subprocess.Popen(
                command, cwd=str(ROOT), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL, creationflags=NO_WINDOW, text=True, encoding="utf-8", errors="replace",
            )
            threading.Thread(target=self.read_partner_output, args=(self.partner,), daemon=True).start()
        except OSError as error:
            self.partner = None
            messagebox.showerror(APP_TITLE, f"Could not start Player 2 window:\n{error}")

    def read_partner_output(self, process):
        try:
            log = open(USER_DIR / "launcher-last-run-2.log", "w", encoding="utf-8")
        except OSError:
            log = None
        for line in process.stdout:
            if log:
                try:
                    log.write(line)
                    log.flush()
                except (OSError, ValueError):
                    log = None
        if log:
            try:
                log.write(f"exit code {process.wait()}\n")
                log.close()
            except (OSError, ValueError):
                pass

    def stop_partner(self):
        partner = self.partner
        self.partner = None
        self.partner_start = None
        if partner and partner.poll() is None:
            subprocess.run(["taskkill", "/PID", str(partner.pid), "/T", "/F"], creationflags=NO_WINDOW,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def read_output(self, process):
        log = getattr(self, "run_log", None)
        for line in process.stdout:
            self.output.put(line.rstrip())
            if log:
                try:
                    log.write(line)
                    log.flush()
                except (OSError, ValueError):
                    log = None
        if log:
            try:
                log.write(f"exit code {process.wait()}\n")
                log.close()
            except (OSError, ValueError):
                pass

    def poll_controller_nav(self):
        if self.process is not None or self.pad_capture is not None or self.capturing is not None:
            self.nav_pad_prev = set()
            return
        held = self.gamepads.held()
        pressed = held - self.nav_pad_prev
        self.nav_pad_prev = held
        if not pressed:
            return
        if "dpup" in pressed:
            self.key_nav(-1)
        elif "dpdown" in pressed:
            self.key_nav(1)
        elif "dpleft" in pressed:
            self.key_journal(-1)
        elif "dpright" in pressed:
            self.key_journal(1)
        elif "a" in pressed or "start" in pressed:
            self.key_activate()
        elif "b" in pressed:
            self.key_back()

    def poll(self):
        while True:
            try:
                line = self.output.get_nowait()
            except queue.Empty:
                break
            self.last_output = time.time()
            if line.startswith("Collection: switching to"):
                target = line.split("switching to", 1)[1].strip().split()[0]
                name = next((g["name"] for g in GAMES if g["exe"] == target), target)
                self.set_status(f"Switching to {name}...", "Collection game transition in progress.")
                self.game_started = False

        self.poll_controller_nav()
        self.poll_count = getattr(self, "poll_count", 0) + 1
        if self.poll_count % 8 == 0:
            self.follow_ini()
        if self.process:
            if self.process.poll() is not None:
                self.finished(self.process.returncode)
            elif not self.game_started and time.time() - self.last_output > 2.5:
                self.game_started = True
                self.set_status(
                    f"Running: {self.current_game()['name']}",
                    f"{self.active_spec_summary()}  •  Insert opens HUD Menu",
                )
        self.root.after(120, self.poll)

    def latest_log(self):
        logs = [ROOT / "analysis" / g["log"] / "boot.log" for g in GAMES]
        logs = [log for log in logs if log.exists()]
        return max(logs, key=lambda p: p.stat().st_mtime) if logs else None

    def crash_reason(self):
        log = self.latest_log()
        if not log or log.stat().st_mtime < self.started_at:
            return None
        try:
            tail = log.read_text(encoding="utf-8", errors="replace").splitlines()[-80:]
        except OSError:
            return None
        for line in reversed(tail):
            if line.startswith("Hang watchdog:") and " report " in line:
                return ("The game froze. Hang report saved to:\n" + line.split(" report ", 1)[-1])
        for i, line in enumerate(tail):
            if line.startswith("Fault:"):
                where = next((l.strip() for l in tail[i + 1:i + 6] if l.lstrip().startswith(("host #0", "#0"))), "")
                return line.strip() + (f"\n{where}" if where else "")
        for i, line in enumerate(tail):
            if line.startswith("STOP:") or "Unreachable code" in line or "Assertion" in line:
                context = [l for l in tail[max(0, i - 2):i + 1] if l.strip()]
                return "\n".join(context)[-600:]
        return None

    def confirm_update_files(self):
        update = self.update_dir()
        if not update:
            return True
        try:
            problems = self.update_installer().check_update(update)
        except Exception:
            return True
        if not problems:
            return True
        return messagebox.askokcancel(
            APP_TITLE,
            "Your game update folder has missing or damaged files:\n\n" + "\n".join(problems[:6]) +
            f"\n\n{update}\n\nPlay anyway?",
            parent=self.root,
        )

    def confirm_without_update(self):
        folder = self.game_dir()
        version = app_version(folder)
        if self.update_dir() or not version or version >= "01.02" or self.config.get("warned_no_update") == str(folder):
            return True
        if not messagebox.askokcancel(
            APP_TITLE,
            f"Your game is version {version.lstrip('0')} without Update 1.02.\n\n"
            "Most stability and crash fixes target Update 1.02. Install Update 1.02 under EXTRAS & TOOLS.\n\n"
            "Launch anyway?",
            parent=self.root,
        ):
            return False
        self.config["warned_no_update"] = str(folder)
        write_config(self.config)
        return True

    def confirm_slow_drive(self):
        places = []
        for label, folder in (("Port folder", ROOT), ("Game folder", self.game_dir())):
            try:
                reason = slow_drive_reason(folder)
            except (OSError, ValueError, ctypes.ArgumentError):
                reason = None
            if reason:
                places.append(f"{label} ({folder}): {reason}.")
        key = f"{ROOT}|{self.game_dir()}"
        if not places or self.config.get("warned_slow_drive") == key:
            return True
        if not messagebox.askokcancel(
            APP_TITLE,
            "\n".join(places) + "\n\nRemovable/exFAT drives may stall shader cache & save writes. Play anyway?",
            parent=self.root,
        ):
            return False
        self.config["warned_slow_drive"] = key
        write_config(self.config)
        return True

    def play_direct(self):
        self.play()
        if not self.process:
            self.root.destroy()

    def finished(self, code):
        self.process = None
        self.stop_partner()
        reason = self.crash_reason()
        froze = bool(reason) and reason.startswith("The game froze")
        if (code in (0, None) or self.stopping) and not froze:
            self.stopping = False
            self.set_status(f"Ready: {self.current_game()['name']}", "")
        else:
            self.stopping = False
            self.set_status(
                "Game froze (report saved)." if froze else f"Stopped unexpectedly (exit {code}).", ""
            )
            if reason and messagebox.askyesno(APP_TITLE, f"The game stopped with an error:\n\n{reason}\n\nOpen log folder?"):
                self.open_logs()
        if self.direct:
            self.root.after(200, self.root.destroy)

    def stop(self):
        if not self.process:
            return
        self.stopping = True
        subprocess.run(["taskkill", "/PID", str(self.process.pid), "/T", "/F"], creationflags=NO_WINDOW,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.stop_partner()
        self.set_status("Stopping game process...", "")

    # -- Maintenance, PKG Installers, Shortcuts & Updates -----------------------------------

    def check_updates(self, manual=False):
        if updater is None:
            if manual:
                messagebox.showinfo(APP_TITLE, "Updater module is not installed.")
            return
        current = updater.installed_version(ROOT)
        if current is None:
            if manual:
                messagebox.showinfo(APP_TITLE, "Running from git workspace — update via git pull.")
            return
        if self.update_busy:
            return
        self.update_busy = True
        if manual:
            self.set_status("Checking for updates...", "")

        def work():
            try:
                self.update_result = (updater.latest_release(current), None)
            except Exception as error:
                self.update_result = (None, str(error) or error.__class__.__name__)
        threading.Thread(target=work, daemon=True).start()
        self.root.after(200, lambda: self.update_checked(manual))

    def update_checked(self, manual):
        if self.update_result is None:
            self.root.after(200, lambda: self.update_checked(manual))
            return
        release, error = self.update_result
        self.update_result = None
        self.update_busy = False
        if release:
            self.update_release = release
            ver = updater.version_text(release["version"])
            self.set_status(f"Update {ver} available!", "Click UPDATE button above PLAY.")
        elif manual:
            cur = updater.version_text(updater.installed_version(ROOT))
            self.set_status("Update check failed." if error else f"Up to date ({cur}).", error or "")

    def install_update(self):
        release = self.update_release
        if not release or self.update_busy or updater is None:
            return
        if self.process:
            messagebox.showinfo(APP_TITLE, "Close the game before updating.")
            return
        version = updater.version_text(release["version"])
        if not messagebox.askyesno(APP_TITLE, f"Download & install version {version}?"):
            return
        self.update_busy = True
        self.update_progress = (0, release["size"])
        self.update_error = None
        self.update_staged = None

        def work():
            try:
                archive = updater.download(release, ROOT, lambda d, t: setattr(self, "update_progress", (d, t)))
                self.update_staged = updater.stage(archive, ROOT, release["version"])
            except Exception as error:
                self.update_error = str(error) or error.__class__.__name__
        threading.Thread(target=work, daemon=True).start()
        self.root.after(200, self.update_tick)

    def update_tick(self):
        if self.update_error:
            self.update_busy = False
            self.set_status("Update failed.", "")
            messagebox.showerror(APP_TITLE, f"Update error:\n{self.update_error}")
            return
        if self.update_staged:
            try:
                updater.start_apply(ROOT, self.update_staged)
                self.config["window_xy"] = [self.root.winfo_x(), self.root.winfo_y()]
                write_config(self.config)
            finally:
                self.root.destroy()
            return
        done, total = self.update_progress
        pct = f"{100 * done / total:.0f}%" if total else f"{done / 1e6:.0f} MB"
        self.set_status(f"Downloading update... {pct}", "Keep launcher open.")
        self.root.after(200, self.update_tick)

    def open_logs(self):
        log = self.latest_log()
        folder = log.parent if log else ROOT / "analysis"
        folder.mkdir(parents=True, exist_ok=True)
        os.startfile(folder)

    def open_saves(self):
        folder = USER_DIR / "savedata"
        os.startfile(folder if folder.exists() else USER_DIR)

    def clear_cache(self):
        cache = USER_DIR / "cache"
        if not cache.exists():
            messagebox.showinfo(APP_TITLE, "Shader cache is already empty.")
            return
        if self.process:
            messagebox.showwarning(APP_TITLE, "Close the game first.")
            return
        if not messagebox.askyesno(APP_TITLE, "Delete compiled shader cache?"):
            return
        shutil.rmtree(cache, ignore_errors=True)
        messagebox.showinfo(APP_TITLE, "Shader cache cleared.")

    def make_shortcut(self, name, description, play=None):
        link = desktop_folder() / f"{name}.lnk"
        pythonw = Path(sys.executable).with_name("pythonw.exe")
        target = pythonw if pythonw.exists() else Path(sys.executable)
        arguments = f"\"{Path(__file__).resolve()}\""
        if STARTER_EXE.exists():
            target, arguments = STARTER_EXE, ""
        if play:
            arguments = f"{arguments} --play {play}".strip()

        def ps(value):
            return "'" + str(value).replace("'", "''") + "'"

        script = (
            "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;"
            f"$s=(New-Object -ComObject WScript.Shell).CreateShortcut({ps(link)});"
            f"$s.TargetPath={ps(target)};$s.Arguments={ps(arguments)};"
            f"$s.WorkingDirectory={ps(ROOT)};$s.IconLocation={ps(ASSETS / 'uncharted.ico')};"
            f"$s.Description={ps(description)};$s.Save()"
        )
        result = subprocess.run(["powershell", "-NoProfile", "-Command", script], creationflags=NO_WINDOW,
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        return link, result

    def desktop_shortcut(self):
        link, result = self.make_shortcut("Uncharted - Sic Parvis Magna Collection", APP_TITLE)
        if result.returncode == 0:
            messagebox.showinfo(APP_TITLE, f"Shortcut created:\n{link}")
        else:
            messagebox.showerror(APP_TITLE, f"Could not create shortcut:\n{result.stderr.strip()}")

    def game_shortcuts(self):
        made, failed = [], []
        for game in GAMES:
            name = game["name"].replace(":", " -")
            link, result = self.make_shortcut(name, f"{game['name']} (direct start)", play=game["key"])
            (made if result.returncode == 0 else failed).append(
                link.name if result.returncode == 0 else result.stderr.strip()
            )
        if failed:
            messagebox.showerror(APP_TITLE, "Could not create shortcuts:\n" + "\n".join(failed))
        else:
            messagebox.showinfo(APP_TITLE, "Desktop shortcuts created:\n" + "\n".join(made))

    def update_installer(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location("install_update", ROOT / "tools" / "install_update.py")
        installer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(installer)
        return installer

    def choose_update_folder(self):
        if self.process:
            messagebox.showwarning(APP_TITLE, "Close the game first.")
            return
        game = self.game_dir()
        if not is_game_dir(game):
            messagebox.showwarning(APP_TITLE, "Choose the base game folder first.")
            return
        from tkinter import filedialog
        folder = filedialog.askdirectory(title="Select extracted Update 1.02 folder", parent=self.root, mustexist=True)
        if not folder:
            return
        installer = self.update_installer()
        try:
            update = installer.check_folder(folder, game)
        except installer.UpdateError as error:
            messagebox.showerror(APP_TITLE, f"Invalid update folder:\n{error}")
            return
        self.config["update_dir"] = str(update)
        write_config(self.config)
        self.update_label.config(text=self.update_text())
        messagebox.showinfo(APP_TITLE, f"Update {app_version(update) or '1.02'} registered:\n{update}")

    def install_game_update(self):
        if self.process:
            messagebox.showwarning(APP_TITLE, "Close the game first.")
            return
        game = self.game_dir()
        if not is_game_dir(game):
            messagebox.showwarning(APP_TITLE, "Choose the base game folder first.")
            return
        from tkinter import filedialog
        pkg = filedialog.askopenfilename(title="Select Update 1.02 (.pkg)", parent=self.root,
                                         filetypes=[("PS4 package", "*.pkg"), ("All files", "*.*")])
        if not pkg:
            return
        installer = self.update_installer()
        existing = installer.existing_update(game)
        if existing and not messagebox.askyesno(APP_TITLE, f"Replace existing update folder?\n{existing}"):
            return
        state = {"step": "Extracting update .pkg..."}

        def work():
            try:
                state["path"] = installer.install(pkg, game, log=lambda t: state.__setitem__("step", t))
            except Exception as error:
                state["error"] = str(error)

        worker = threading.Thread(target=work, daemon=True)
        worker.start()

        def poll_pkg():
            if worker.is_alive():
                self.update_label.config(text=state["step"])
                self.root.after(200, poll_pkg)
                return
            if "path" in state:
                self.config["update_dir"] = str(state["path"])
                write_config(self.config)
            self.update_label.config(text=self.update_text())
            if "error" in state:
                messagebox.showerror(APP_TITLE, f"Update install failed:\n{state['error']}")
            else:
                messagebox.showinfo(APP_TITLE, f"Update 1.02 installed:\n{state['path']}")
        poll_pkg()

    def install_game_pkg(self):
        if self.process:
            messagebox.showwarning(APP_TITLE, "Close the game first.")
            return
        from tkinter import filedialog
        pkg = filedialog.askopenfilename(title="Select Base Game (.pkg)", parent=self.root,
                                         filetypes=[("PS4 package", "*.pkg"), ("All files", "*.*")])
        if not pkg:
            return
        parent = filedialog.askdirectory(title="Select destination folder (~45 GB free)", parent=self.root,
                                         initialdir=str(Path(pkg).parent))
        if not parent:
            return
        installer = self.update_installer()
        state = {"step": "Checking base game package..."}

        def work():
            try:
                state["path"] = installer.install_game(pkg, parent, log=lambda t: state.__setitem__("step", t))
            except Exception as error:
                state["error"] = str(error)

        worker = threading.Thread(target=work, daemon=True)
        worker.start()

        def poll_base():
            if worker.is_alive():
                self.game_dir_label.config(text=state["step"])
                self.root.after(200, poll_base)
                return
            if "path" in state:
                self.config["game_dir"] = str(state["path"])
                if WINE:
                    self.config["game_dir_unix"] = unix_path(str(state["path"]))
                write_config(self.config)
            self.game_dir_label.config(text=f"Game Folder: {self.game_dir()}")
            self.update_label.config(text=self.update_text())
            if "error" in state:
                messagebox.showerror(APP_TITLE, f"Base game install failed:\n{state['error']}")
            else:
                messagebox.showinfo(APP_TITLE, f"Base game extracted:\n{state['path']}")
        poll_base()

    def check_game_files(self):
        folder = self.game_dir()
        if not folder or not folder.exists():
            messagebox.showwarning(APP_TITLE, "Choose the game folder first.")
            return
        try:
            result = subprocess.run(
                [sys.executable, "-I", str(ROOT / "tools" / "check_game_files.py"), "check", str(folder)],
                capture_output=True, text=True, timeout=600, creationflags=NO_WINDOW,
            )
        except (OSError, subprocess.SubprocessError) as error:
            messagebox.showerror(APP_TITLE, f"Verification could not run:\n{error}")
            return
        lines = result.stdout.splitlines()
        if result.returncode == 0:
            messagebox.showinfo(APP_TITLE, "All game files verified intact!\n\n" + (lines[0] if lines else ""))
            return
        problems = lines[1:]
        shown = "\n".join(problems[:15]) + (f"\n... and {len(problems) - 15} more" if len(problems) > 15 else "")
        messagebox.showerror(APP_TITLE, "Incomplete or corrupted game files detected:\n\n" +
                             (lines[0] if lines else "") + "\n\n" + shown)

    def add_to_steam(self, per_game=False):
        try:
            import steam_library
        except ImportError as error:
            messagebox.showerror(APP_TITLE, f"Missing steam_library.py:\n{error}")
            return
        while steam_library.steam_running():
            if not messagebox.askretrycancel(
                APP_TITLE, "Please exit Steam completely first so shortcuts are not overwritten, then click Retry."
            ):
                return
        games = [(g["key"], g["name"]) for g in GAMES] if per_game else None
        targets = steam_library.launch_targets(ROOT, STARTER_EXE, sys.executable, Path(__file__).resolve(), games)
        names = [steam_library.COLLECTION_NAME] + [g["name"] for g in GAMES]
        gdir = self.game_dir() if is_game_dir(self.game_dir()) else None
        try:
            results = steam_library.add_to_steam(targets, ASSETS / "uncharted.ico", gdir, all_names=names)
        except Exception as error:
            messagebox.showerror(APP_TITLE, f"Could not add to Steam:\n{error}")
            return
        summary = "\n".join(f"{name or folder.name}: added {r['added']}, updated {r['updated']}"
                            for folder, name, r in results)
        messagebox.showinfo(APP_TITLE, f"Added to Steam Library:\n{summary}")

    def reset(self):
        name = self.current_game()["name"]
        if not messagebox.askyesno(APP_TITLE, f"Reset {name} settings to the Recommended 2015 Medium (2 GB) preset?"):
            return
        self.game_settings().update(GAME_SETTING_DEFAULTS)
        self.config.update({
            "graphics_api": "dx11", "vram_budget_mb": "0", "texture_quality": "medium",
            "shadow_quality": "medium", "ssao_quality": "full", "frame_pacing_mode": "hybrid",
            "diagnostics": False, "full_res_textures": False, "hires_shadows": False,
        })
        self.ini.update(INI_DEFAULTS)
        self.apply_preset("medium")

    def close(self):
        if self.process and not messagebox.askyesno(
            APP_TITLE, "A game is currently running. Close the Launcher anyway? (The game will keep running.)"
        ):
            return
        try:
            self.config["window_xy"] = [self.root.winfo_x(), self.root.winfo_y()]
            write_config(self.config)
        except Exception:
            pass
        self.root.destroy()


def log_launcher_error(kind, value, trace):
    import traceback
    text = "".join(traceback.format_exception(kind, value, trace))
    path = USER_DIR / "launcher-error.log"
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "a", encoding="utf-8") as log:
            log.write(time.strftime("%Y-%m-%d %H:%M:%S ") + APP_TITLE + "\n" + text + "\n")
    except OSError:
        pass
    return text, path


def main():
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("UnchartedCollection.SicParvisMagna.Launcher")
    except Exception:
        pass
    direct = None
    if "--play" in sys.argv[1:]:
        at = sys.argv.index("--play")
        value = sys.argv[at + 1].lower() if at + 1 < len(sys.argv) else ""
        direct = value if value in GAME_KEYS else f"u{value}" if f"u{value}" in GAME_KEYS else None
    root = tk.Tk()
    if direct:
        root.withdraw()
    root.report_callback_exception = lambda kind, value, trace: log_launcher_error(kind, value, trace)
    Launcher(root, direct)
    root.mainloop()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        text, path = log_launcher_error(*sys.exc_info())
        try:
            fallback = tk.Tk()
            fallback.withdraw()
            messagebox.showerror(APP_TITLE, f"The launcher could not start:\n\n{text[-1500:]}\nSaved to:\n{path}")
            fallback.destroy()
        except Exception:
            pass
        raise
