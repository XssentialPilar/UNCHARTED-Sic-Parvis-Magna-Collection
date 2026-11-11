// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Game Launcher & Process Coordinator
#ifndef SPM_GAME_LAUNCHER_HPP
#define SPM_GAME_LAUNCHER_HPP

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <atomic>
#include "spm/spm_types.h"

namespace spm {

enum class ProcessState {
    Idle,
    Starting,
    Running,
    Terminated
};

struct LogEntry {
    std::string timestamp;
    std::string message;
    bool isError{false};
};

class GameLauncher {
public:
    static GameLauncher& Instance();

    bool StartGame(const SPMConfig& config, const std::string& workspaceRoot);
    void StopGame();

    ProcessState GetState() const;
    uint32_t GetProcessId() const;
    int GetLastExitCode() const;
    void GetMemoryStats(uint64_t& outWorkingSet, uint64_t& outCommit);

    std::vector<LogEntry> GetLogs(size_t maxCount = 200);
    void AppendLog(const std::string& msg, bool isError = false);
    void ClearLogs();

private:
    GameLauncher();
    ~GameLauncher();

    GameLauncher(const GameLauncher&) = delete;
    GameLauncher& operator=(const GameLauncher&) = delete;

    std::atomic<ProcessState> state_{ProcessState::Idle};
    std::atomic<uint32_t> processId_{0};
    std::atomic<int> lastExitCode_{0};

    mutable std::mutex logMutex_;
    std::vector<LogEntry> logs_;

#ifdef _WIN32
    void* hProcess_{nullptr};
    void* hStdOutRead_{nullptr};
#else
    int childPid_{0};
    int pipeFd_{0};
#endif
};

} // namespace spm

#endif // SPM_GAME_LAUNCHER_HPP
