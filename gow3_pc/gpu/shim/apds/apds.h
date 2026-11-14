// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Orchestrator Interface
#pragma once

#include "apds_types.h"

namespace APDS {

void Initialize(TelemetryMode mode = TelemetryMode::Standard, const std::string& session_name = "");
void Shutdown();

bool IsEnabled() noexcept;
TelemetryMode GetMode() noexcept;
const std::string& GetSessionDirectory() noexcept;

void RecordFrameBegin(uint64_t frame_index);
void RecordFramePresent(uint64_t frame_index, uint32_t draw_calls = 0, uint32_t compute_dispatches = 0);

void RecordResourceUpload(const std::string& type, const std::string& id, uint64_t size_bytes,
                          uint32_t mips, const std::string& details = "");
void RecordResourceEvict(const std::string& type, const std::string& id, uint64_t size_bytes,
                         const std::string& details = "");

void RecordEvent(EventCategory category, EventSeverity severity, const std::string& code,
                 const std::string& message, const std::string& context_json = "{}");

void RecordIo(const std::string& op, const std::string& path, uint64_t bytes, double duration_us,
              bool success = true);

void Flush();

} // namespace APDS
