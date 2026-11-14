// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Explorer's Desk Interface
#ifndef SPM_EXPLORER_DESK_HPP
#define SPM_EXPLORER_DESK_HPP

#include <string>
#include <vector>
#include "spm/spm_types.h"

namespace spm {

class ExplorerDesk {
public:
    ExplorerDesk();
    ~ExplorerDesk();

    void Initialize(const std::string& workspaceRoot);
    void Render();
    bool ShouldClose() const { return shouldClose_; }
    void RequestClose() { shouldClose_ = true; }

    const SPMConfig& GetConfig() const { return config_; }

private:
    void RenderHeader();
    void RenderSidebar();
    void RenderTabExpeditions();
    void RenderTabGraphics();
    void RenderTabDisplay();
    void RenderTabControls();
    void RenderTabTelemetry();
    void RenderTabUpdates();
    void RenderFooter();

    void ApplyPreset(int presetIndex);
    const char* Tr(const char* key, const char* fallback) const;

    std::string workspaceRoot_;
    SPMConfig config_{};
    std::vector<SPMGpuInfo> gpus_;
    int recommendedGpuIndex_{0};
    int activeGpuIndex_{0};

    int activeTab_{0}; // 0 = Expediciones, 1 = Gráficos, 2 = Pantalla, 3 = Controles, 4 = Telemetría, 5 = Actualizaciones
    bool shouldClose_{false};

    // Telemetry sampling buffer
    float frameTimes_[60]{};
    int frameTimeHead_{0};
    uint64_t workingSetMb_{0};
    uint64_t commitMb_{0};

    // Update checker state
    SPMUpdateInfo updateInfo_{};
    bool updateChecked_{false};
    std::string updateStatusMsg_;
    bool isCheckingUpdate_{false};
};

} // namespace spm

#endif // SPM_EXPLORER_DESK_HPP
