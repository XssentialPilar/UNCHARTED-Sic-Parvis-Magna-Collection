using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace UnchartedLauncher;

public static class NativeBridge
{
    private const string DllName = "launcher_native.dll";

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr LoadLibrary(string dllToLoad);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void Launcher_InitTimerResolution();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void Launcher_AllowSetForegroundWindow(uint processId);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int Launcher_DetectGpus(byte[] outBuffer, int maxLen);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int Launcher_GetProcessMemory(uint processId, out ulong outWorkingSet, out ulong outCommit);

    private static bool _nativeLoaded = false;

    static NativeBridge()
    {
        try
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string[] candidates = {
                Path.Combine(baseDir, DllName),
                Path.Combine(baseDir, "core", DllName),
                Path.Combine(baseDir, "bin", DllName)
            };
            foreach (var cand in candidates)
            {
                if (File.Exists(cand))
                {
                    LoadLibrary(cand);
                    _nativeLoaded = true;
                    break;
                }
            }
        }
        catch
        {
            _nativeLoaded = false;
        }
    }

    public static void InitTimerResolution()
    {
        if (_nativeLoaded)
        {
            try { Launcher_InitTimerResolution(); } catch { }
        }
    }

    public static void AllowSetForeground(uint pid = 0)
    {
        if (_nativeLoaded)
        {
            try { Launcher_AllowSetForegroundWindow(pid); } catch { }
        }
    }

    public static string DetectGpus()
    {
        if (_nativeLoaded)
        {
            try
            {
                byte[] buf = new byte[4096];
                int count = Launcher_DetectGpus(buf, buf.Length);
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
