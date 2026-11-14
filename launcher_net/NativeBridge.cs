using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace UnchartedLauncher;

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct SPMGpuInfoNative
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string Name;
    public uint VendorId;
    public uint DeviceId;
    public ulong DedicatedVramBytes;
    public ulong SharedSystemMemoryBytes;
    public uint IsDiscrete;
    public uint SupportsD3D11;
    public uint SupportsVulkan;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
    public string StableLuid;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct SpmHardwareCapabilitiesNative
{
    public uint CpuLogicalCores;
    public uint CpuPhysicalCores;
    public byte CpuHasAvx2;
    public byte CpuHasAvx512;
    public byte CpuHasFma;
    public ulong TotalPhysicalRamMb;
    public ulong AvailableRamMb;
    public int StorageType;
    public float StorageEstimatedBandwidthMBps;
    public byte StorageDirectStorageSupported;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 8)]
    public string StorageDriveLetter;
    public int NpuVendor;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string NpuName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string NpuDriverVersion;
    public byte NpuSupported;
    public byte DirectMlSupported;
    public byte OnnxRuntimeSupported;
}

public static class NativeBridge
{
    private const string SPMCoreDll = "SPMCore.dll";
    private const string LegacyDll = "launcher_native.dll";

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr LoadLibrary(string dllToLoad);

    // SPMCore C API
    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_InitTimerResolution")]
    private static extern void SPM_InitTimerResolution();

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_AllowSetForegroundWindow")]
    private static extern void SPM_AllowSetForegroundWindow(uint processId);

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_DetectGpus")]
    private static extern int SPM_DetectGpus([Out] SPMGpuInfoNative[] outGpus, int maxGpus, out int outCount);

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_GetRecommendedGpuIndex")]
    private static extern int SPM_GetRecommendedGpuIndex(SPMGpuInfoNative[] gpus, int count);

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_GetPlatformIdString")]
    private static extern IntPtr SPM_GetPlatformIdString();

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_GetCoreVersion")]
    private static extern IntPtr SPM_GetCoreVersion();

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_DetectSystemLanguage")]
    private static extern IntPtr SPM_DetectSystemLanguage();

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_GetProcessMemory")]
    private static extern int SPM_GetProcessMemory(uint processId, out ulong outWorkingSet, out ulong outCommit);

    [DllImport(SPMCoreDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SPM_DetectHardwareCapabilities", CharSet = CharSet.Ansi)]
    private static extern int SPM_DetectHardwareCapabilities(string? targetPath, out SpmHardwareCapabilitiesNative outCaps);

    // Legacy Fallbacks
    [DllImport(LegacyDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "Launcher_InitTimerResolution")]
    private static extern void Legacy_InitTimerResolution();

    [DllImport(LegacyDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "Launcher_AllowSetForegroundWindow")]
    private static extern void Legacy_AllowSetForegroundWindow(uint processId);

    [DllImport(LegacyDll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "Launcher_DetectGpus")]
    private static extern int Legacy_DetectGpus(byte[] outBuffer, int maxLen);

    private static bool _spmCoreLoaded = false;
    private static bool _legacyLoaded = false;

    public static bool IsSPMCoreLoaded => _spmCoreLoaded;

    static NativeBridge()
    {
        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string[] searchPaths = {
            baseDir,
            Path.Combine(baseDir, "core"),
            Path.Combine(baseDir, "bin"),
            Path.Combine(baseDir, "..", "core"),
            LauncherConfig.ResolveWorkspaceRoot(),
            Path.Combine(LauncherConfig.ResolveWorkspaceRoot(), "Build", "spm_core_x64"),
            Path.Combine(LauncherConfig.ResolveWorkspaceRoot(), "Project", "spm_core", "build-release")
        };

        // Try loading SPMCore.dll first
        foreach (var p in searchPaths)
        {
            string spmPath = Path.Combine(p, "SPMCore.dll");
            string libSpmPath = Path.Combine(p, "libSPMCore.dll");
            if (File.Exists(spmPath))
            {
                if (LoadLibrary(spmPath) != IntPtr.Zero)
                {
                    _spmCoreLoaded = true;
                    break;
                }
            }
            else if (File.Exists(libSpmPath))
            {
                if (LoadLibrary(libSpmPath) != IntPtr.Zero)
                {
                    _spmCoreLoaded = true;
                    break;
                }
            }
        }

        // Fallback to legacy launcher_native.dll if SPMCore is not found
        if (!_spmCoreLoaded)
        {
            foreach (var p in searchPaths)
            {
                string legPath = Path.Combine(p, LegacyDll);
                if (File.Exists(legPath))
                {
                    if (LoadLibrary(legPath) != IntPtr.Zero)
                    {
                        _legacyLoaded = true;
                        break;
                    }
                }
            }
        }
    }

    public static void InitTimerResolution()
    {
        if (_spmCoreLoaded)
        {
            try { SPM_InitTimerResolution(); return; } catch { }
        }
        if (_legacyLoaded)
        {
            try { Legacy_InitTimerResolution(); } catch { }
        }
    }

    public static void AllowSetForeground(uint pid = 0)
    {
        if (_spmCoreLoaded)
        {
            try { SPM_AllowSetForegroundWindow(pid); return; } catch { }
        }
        if (_legacyLoaded)
        {
            try { Legacy_AllowSetForegroundWindow(pid); } catch { }
        }
    }

    public static string GetPlatformId()
    {
        if (_spmCoreLoaded)
        {
            try
            {
                IntPtr ptr = SPM_GetPlatformIdString();
                if (ptr != IntPtr.Zero) return Marshal.PtrToStringAnsi(ptr) ?? "win10-x64";
            }
            catch { }
        }
        return Environment.Is64BitOperatingSystem ? "win10-x64" : "win10-x86";
    }

    public static string GetCoreVersion()
    {
        if (_spmCoreLoaded)
        {
            try
            {
                IntPtr ptr = SPM_GetCoreVersion();
                if (ptr != IntPtr.Zero) return Marshal.PtrToStringAnsi(ptr) ?? "unknown";
            }
            catch { }
        }
        return "unknown";
    }

    public static bool TryDetectHardwareCapabilities(out SpmHardwareCapabilitiesNative caps)
    {
        caps = default;
        if (!_spmCoreLoaded)
        {
            return false;
        }
        try
        {
            return SPM_DetectHardwareCapabilities(null, out caps) == 1;
        }
        catch
        {
            return false;
        }
    }

    public static List<SPMGpuInfoNative> DetectGpusDirect(out int recommendedIndex)
    {
        recommendedIndex = -1;
        var list = new List<SPMGpuInfoNative>();
        if (_spmCoreLoaded)
        {
            try
            {
                var buffer = new SPMGpuInfoNative[8];
                int count = 0;
                int res = SPM_DetectGpus(buffer, 8, out count);
                if (count > 0)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        list.Add(buffer[i]);
                    }
                    recommendedIndex = SPM_GetRecommendedGpuIndex(buffer, count);
                    return list;
                }
            }
            catch { }
        }
        return list;
    }

    public static string DetectGpus()
    {
        if (_spmCoreLoaded)
        {
            var gpus = DetectGpusDirect(out int rec);
            if (gpus.Count > 0)
            {
                var sb = new StringBuilder();
                foreach (var g in gpus)
                {
                    sb.AppendLine($"{g.Name}|{(g.IsDiscrete == 1 ? "discrete" : "integrated")}|{g.DedicatedVramBytes / (1024 * 1024)}MB");
                }
                return sb.ToString().TrimEnd();
            }
        }
        if (_legacyLoaded)
        {
            try
            {
                byte[] buf = new byte[4096];
                int count = Legacy_DetectGpus(buf, buf.Length);
                if (count > 0)
                {
                    return Encoding.UTF8.GetString(buf).TrimEnd('\0');
                }
            }
            catch { }
        }
        return string.Empty;
    }
}
