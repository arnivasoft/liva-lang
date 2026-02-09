#pragma once

#include <string>

namespace liva {

/// Target platform information
struct TargetInfo {
    std::string triple;      // e.g., x86_64-pc-windows-msvc
    std::string cpu;         // e.g., generic
    std::string features;    // e.g., +avx2

    /// Get the default target info for the host machine
    static TargetInfo getHostTarget();
};

} // namespace liva
