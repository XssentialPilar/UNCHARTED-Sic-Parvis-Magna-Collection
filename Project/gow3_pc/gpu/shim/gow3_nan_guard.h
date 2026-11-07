// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from SuperMedo/uncharted_pc 0.3.13 (bbport_nan_guard.h): the NaN guard on colour outputs,
// shared by the shader recompiler and the shader cache.
#pragma once

#include <cstdlib>

namespace Gow3NanGuard {

/// Colour outputs to float render targets: NaN becomes 0, so one bad pixel cannot grow into a
/// black 16x16 block through the games' tiled post-processing (Uncharted 2 on the Steam Deck).
/// GOW3_NAN_GUARD (or BB_NAN_GUARD): 1 (default) NaN only, 2 also clamps +-Inf to the half-float
/// range (that broke Uncharted 1's lighting and fog: blue trees), 0 off.
inline int NanGuardMode() {
    static const int mode = [] {
        const char* env = std::getenv("GOW3_NAN_GUARD");
        if (!env || !*env) {
            env = std::getenv("BB_NAN_GUARD");
        }
        return env && (env[0] == '0' || env[0] == '2') ? env[0] - '0' : 1;
    }();
    return mode;
}

inline bool ExportNanGuard() {
    return NanGuardMode() != 0;
}

/// Non-default modes get their own shader cache suffix; default mode (1) uses "" to preserve
/// compatibility with existing cached shader names.
inline const char* ShaderCacheSuffix() {
    return NanGuardMode() == 0 ? "_nan0" : NanGuardMode() == 2 ? "_nan2" : "";
}

} // namespace Gow3NanGuard
