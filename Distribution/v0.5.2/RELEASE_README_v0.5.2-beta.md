# UNCHARTED: Sic Parvis Magna Collection — v0.5.2 Beta

*Sic Parvis Magna — Greatness from small beginnings.*

Welcome to the next step in **UNCHARTED: Sic Parvis Magna Collection**, an independent PC runtime and launcher project focused on a more accessible, customizable, and responsive experience.

This Beta brings a reorganized native runtime, a new C++ launcher foundation, improved platform-specific packaging, and continued work on graphics performance and compatibility.

## What's New

- **Explorer's Desk, moving forward:** A native C++/SDL3 launcher is now included for Windows x64, while the existing Windows launcher remains available as a fallback.
- **A more streamlined foundation:** The new `SPMCore` library brings shared launcher and runtime services together.
- **Hardware-aware controls:** Expanded GPU detection, graphics configuration, input options, and language selection.
- **Performance-focused development:** Further work on shader handling, memory management, and frame pacing. Actual performance depends on your hardware and game content.
- **Safer, platform-specific updates:** Update checks are designed to match your launcher’s operating system and architecture while preserving personal settings.

## Choose Your Download

| Package | Intended Use |
| --- | --- |
| `SPM-v0.5.2-beta-win11-x64.zip` | Windows 11 (64-bit) — primary Beta testing package |
| `SPM-v0.5.2-beta-win10-x64.zip` | Windows 10 (64-bit) — primary Beta testing package |
| `SPM-v0.5.2-beta-win7-x64.zip` | Windows 7/8/8.1 — **experimental; compatibility validation pending** |
| `SPM-v0.5.2-beta-win-x86.zip` | 32-bit Windows — **launcher tools only; gameplay is not supported** |
| `SPM-v0.5.2-beta-linux-x64.zip` | Linux — **experimental development package; not a complete playable release** |

Please select the package intended for your system. Do not mix files from different packages.

## Getting Started on Windows

1. Download the appropriate ZIP and extract it to a folder of your choice.
2. Open **`Start Uncharted.exe`** to launch the native Explorer's Desk interface. An alternative Windows launcher is included where available.
3. Select the folder containing your own compatible UNCHARTED game files.
4. Adjust your display, graphics, language, and controls, then choose the game you wish to launch.

**Game files are not included.** You must supply your own compatible files. The launcher does not automatically provide or download the games.

## Beta Notes

This is an actively developed Beta. Successful packaging and automated startup tests do not guarantee trouble-free gameplay on every system. Legacy Windows and Linux support require additional real-world validation; the 32-bit package cannot run the 64-bit game runtime.

If you encounter a launch problem, visual issue, unexpected performance drop, or translation error, please report it through [GitHub Issues](https://github.com/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/issues). Your feedback helps guide future improvements.

Thank you for testing and supporting the project.

---

*This is an independent community project and is not affiliated with or endorsed by Sony Interactive Entertainment, PlayStation, or Naughty Dog.*
