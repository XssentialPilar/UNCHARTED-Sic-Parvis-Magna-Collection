/* Uncharted - Sic Parvis Magna Collection.exe: native Windows GUI launcher starter.
 * Starts launcher\uncharted_launcher.pyw without a console window, searching bundled Python first
 * and falling back to MSYS2/system pythonw.exe when running directly from the workspace or Project/. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

static int file_exists_w(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static int resolve_pythonw(const wchar_t *base_dir, wchar_t *out_python, size_t cap)
{
    const wchar_t *candidates[] = {
        L"%ls\\python\\pythonw.exe",
        L"%ls\\Project\\python\\pythonw.exe",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        swprintf(out_python, cap, candidates[i], base_dir);
        if (file_exists_w(out_python))
            return 1;
    }
    DWORD n = SearchPathW(NULL, L"pythonw.exe", NULL, (DWORD)cap, out_python, NULL);
    if (n > 0 && n < cap && file_exists_w(out_python))
        return 1;
    const wchar_t *env_candidates[] = {
        L"%LOCALAPPDATA%\\Programs\\Python\\Python312\\pythonw.exe",
        L"%LOCALAPPDATA%\\Programs\\Python\\Python313\\pythonw.exe",
        L"%LOCALAPPDATA%\\Programs\\Python\\Python311\\pythonw.exe",
        L"%LOCALAPPDATA%\\Programs\\Python\\Python310\\pythonw.exe",
        L"C:\\Python312\\pythonw.exe",
        L"C:\\Python311\\pythonw.exe",
        L"C:\\msys64\\clang64\\bin\\pythonw.exe",
    };
    for (size_t i = 0; i < sizeof(env_candidates) / sizeof(env_candidates[0]); ++i) {
        wchar_t expanded[32768];
        DWORD exp_len = ExpandEnvironmentStringsW(env_candidates[i], expanded, 32768);
        if (exp_len > 0 && exp_len < 32768 && file_exists_w(expanded)) {
            wcsncpy(out_python, expanded, cap - 1);
            out_python[cap - 1] = 0;
            return 1;
        }
    }
    return 0;
}

static int resolve_launcher_script(const wchar_t *base_dir, wchar_t *out_script,
                                   wchar_t *out_work_dir, size_t cap)
{
    swprintf(out_script, cap, L"%ls\\launcher\\uncharted_launcher.pyw", base_dir);
    if (file_exists_w(out_script)) {
        wcsncpy(out_work_dir, base_dir, cap - 1);
        out_work_dir[cap - 1] = 0;
        return 1;
    }
    swprintf(out_script, cap, L"%ls\\Project\\launcher\\uncharted_launcher.pyw", base_dir);
    if (file_exists_w(out_script)) {
        swprintf(out_work_dir, cap, L"%ls\\Project", base_dir);
        return 1;
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show)
{
    (void)instance;
    (void)previous;
    (void)show;
    static wchar_t dir[32768], python[32768], script[32768], work_dir[32768], line[32768];
    DWORD length = GetModuleFileNameW(NULL, dir, 32768);
    if (length == 0 || length >= 32768)
        return 1;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;

    if (!resolve_launcher_script(dir, script, work_dir, 32768)) {
        MessageBoxW(NULL,
                    L"launcher\\uncharted_launcher.pyw was not found next to this executable.\n"
                    L"Please keep the launcher executable inside the Uncharted: Sic Parvis Magna Collection folder.",
                    L"Uncharted: Sic Parvis Magna Collection", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!resolve_pythonw(dir, python, 32768)) {
        MessageBoxW(NULL,
                    L"pythonw.exe was not found (checked python\\pythonw.exe and system PATH).\n"
                    L"Please extract the full Uncharted: Sic Parvis Magna Collection package.",
                    L"Uncharted: Sic Parvis Magna Collection", MB_OK | MB_ICONERROR);
        return 1;
    }

    swprintf(line, 32768, L"\"%ls\" \"%ls\" %ls", python, script, arguments ? arguments : L"");

    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(python, line, NULL, NULL, FALSE, 0, NULL, work_dir, &startup, &process)) {
        MessageBoxW(NULL,
                    L"Could not start Uncharted: Sic Parvis Magna Collection Launcher.",
                    L"Uncharted: Sic Parvis Magna Collection", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return (int)code;
}
