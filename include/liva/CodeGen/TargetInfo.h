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

    /// Create a TargetInfo from a target triple string
    static TargetInfo fromTriple(const std::string &triple,
                                 const std::string &cpu = "generic",
                                 const std::string &features = "");

    /// Returns true if this target differs from the host
    bool isCrossCompiling() const;
};

} // namespace liva
