// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Game Launcher & Process Coordinator
#include "game_launcher.hpp"
#include "spm/spm_core.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <filesystem>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #include <fcntl.h>
#endif

namespace fs = std::filesystem;

namespace spm {

static std::string CurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S");
    return ss.str();
}

GameLauncher& GameLauncher::Instance() {
    static GameLauncher s_instance;
    return s_instance;
}

GameLauncher::GameLauncher() = default;

GameLauncher::~GameLauncher() {
    StopGame();
}

ProcessState GameLauncher::GetState() const {
    return state_.load(std::memory_order_relaxed);
}

uint32_t GameLauncher::GetProcessId() const {
    return processId_.load(std::memory_order_relaxed);
}

int GameLauncher::GetLastExitCode() const {
    return lastExitCode_.load(std::memory_order_relaxed);
}

void GameLauncher::AppendLog(const std::string& msg, bool isError) {
    std::lock_guard<std::mutex> lock(logMutex_);
    logs_.push_back({CurrentTimestamp(), msg, isError});
    if (logs_.size() > 1000) {
        logs_.erase(logs_.begin(), logs_.begin() + 200);
    }
}

std::vector<LogEntry> GameLauncher::GetLogs(size_t maxCount) {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logs_.size() <= maxCount) {
        return logs_;
    }
    return std::vector<LogEntry>(logs_.end() - maxCount, logs_.end());
}

void GameLauncher::ClearLogs() {
    std::lock_guard<std::mutex> lock(logMutex_);
    logs_.clear();
}

void GameLauncher::GetMemoryStats(uint64_t& outWorkingSet, uint64_t& outCommit) {
    outWorkingSet = 0;
    outCommit = 0;
    uint32_t pid = processId_.load(std::memory_order_relaxed);
    if (pid > 0 && state_.load(std::memory_order_relaxed) == ProcessState::Running) {
        SPM_GetProcessMemory(pid, &outWorkingSet, &outCommit);
    }
}

bool GameLauncher::StartGame(const SPMConfig& config, const std::string& workspaceRoot) {
    if (state_ == ProcessState::Running || state_ == ProcessState::Starting) {
        AppendLog("El juego ya se encuentra en ejecución.", true);
        return false;
    }

    state_ = ProcessState::Starting;
    AppendLog("Inicializando resolución de temporizadores de alta precisión (1ms)...");
    SPM_InitTimerResolution();

    AppendLog("Configurando variables de entorno e INI para Uncharted: " + std::string(config.selectedGame));
    if (!SPM_PrepareEnvironment(&config, workspaceRoot.c_str())) {
        AppendLog("Error al preparar el entorno de ejecución del juego.", true);
        state_ = ProcessState::Idle;
        return false;
    }

    SPM_SyncConfigToIni(workspaceRoot.c_str(), &config);

    // Locate runtime executable
    std::vector<fs::path> candidates = {
        fs::path(workspaceRoot) / "Uncharted.exe",
        fs::path(workspaceRoot) / "core" / "Uncharted.exe",
        fs::path(workspaceRoot) / "Distribution" / "Uncharted" / "Uncharted.exe",
        fs::path(workspaceRoot) / "core" / "gow3-probe.exe",
        fs::path(workspaceRoot) / "Project" / "gow3_pc" / "out" / "Uncharted.exe",
        fs::path(workspaceRoot) / "Distribution" / "Uncharted" / "gow3_pc" / "out" / "Uncharted.exe",
        fs::path(workspaceRoot) / "gow3-probe.exe",
        fs::path(workspaceRoot) / "Project" / "gow3_pc" / "out" / "gow3-probe.exe"
    };

    fs::path targetExe;
    for (const auto& p : candidates) {
        if (fs::exists(p)) {
            targetExe = p;
            break;
        }
    }

    if (targetExe.empty()) {
        AppendLog("No se encontró el ejecutable principal del juego (Uncharted.exe).", true);
        state_ = ProcessState::Idle;
        return false;
    }

    AppendLog("Ejecutable detectado: " + targetExe.string());

#ifdef _WIN32
    fs::path workingDir = targetExe.parent_path();
    std::wstring wExe = targetExe.wstring();
    std::wstring wCmd = L"\"" + wExe + L"\"";
    std::wstring wDir = workingDir.wstring();

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    BOOL success = CreateProcessW(
        wExe.c_str(),
        &wCmd[0],
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        wDir.c_str(),
        &si,
        &pi
    );

    if (!success) {
        DWORD err = GetLastError();
        AppendLog("Error al iniciar el proceso (Win32 Code " + std::to_string(err) + ")", true);
        state_ = ProcessState::Idle;
        return false;
    }

    hProcess_ = pi.hProcess;
    CloseHandle(pi.hThread);
    processId_ = (uint32_t)pi.dwProcessId;
    state_ = ProcessState::Running;

    SPM_AllowSetForegroundWindow(processId_);
    AppendLog("Proceso iniciado con éxito [PID: " + std::to_string(processId_) + "]");

    // Monitor thread
    std::thread([this]() {
        WaitForSingleObject((HANDLE)hProcess_, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess((HANDLE)hProcess_, &exitCode);
        CloseHandle((HANDLE)hProcess_);
        hProcess_ = nullptr;
        processId_ = 0;
        lastExitCode_ = (int)exitCode;
        state_ = ProcessState::Terminated;
        AppendLog("El proceso ha finalizado con código de salida: " + std::to_string(exitCode));
    }).detach();

    return true;
#else
    AppendLog("Iniciando entorno Linux nativo para el ejecutable...");
    pid_t pid = fork();
    if (pid == 0) {
        chdir(targetExe.parent_path().c_str());
        execl(targetExe.c_str(), targetExe.filename().c_str(), (char*)nullptr);
        _exit(127);
    } else if (pid > 0) {
        processId_ = (uint32_t)pid;
        childPid_ = pid;
        state_ = ProcessState::Running;
        AppendLog("Proceso Linux iniciado con éxito [PID: " + std::to_string(pid) + "]");

        std::thread([this, pid]() {
            int status = 0;
            waitpid(pid, &status, 0);
            processId_ = 0;
            childPid_ = 0;
            lastExitCode_ = WEXITSTATUS(status);
            state_ = ProcessState::Terminated;
            AppendLog("Proceso Linux terminado con código: " + std::to_string(lastExitCode_));
        }).detach();

        return true;
    } else {
        AppendLog("Fallo al crear proceso (fork failed)", true);
        state_ = ProcessState::Idle;
        return false;
    }
#endif
}

void GameLauncher::StopGame() {
    if (state_ != ProcessState::Running) return;

#ifdef _WIN32
    if (hProcess_) {
        TerminateProcess((HANDLE)hProcess_, 1);
        AppendLog("Terminación solicitada por el usuario.");
    }
#else
    if (childPid_ > 0) {
        kill(childPid_, SIGTERM);
        AppendLog("SIGTERM enviado al proceso del juego.");
    }
#endif
}

} // namespace spm
