#pragma once

#include <map>
#include <string>

namespace liva {

class TranslationUnit;
class DiagnosticsEngine;

/// Abstract base class for compiler plugins.
/// Plugins can hook into the compilation pipeline at two points:
///   afterParse  — called after parsing, before semantic analysis
///   afterSema   — called after semantic analysis, before IR generation
class CompilerPlugin {
public:
    virtual ~CompilerPlugin() = default;

    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;

    /// Called after parsing. Return true to continue, false to abort.
    virtual bool afterParse(TranslationUnit &tu, DiagnosticsEngine &diag);

    /// Called after semantic analysis. Return true to continue, false to abort.
    virtual bool afterSema(TranslationUnit &tu, DiagnosticsEngine &diag);

    /// Configure plugin from key-value options (from TOML).
    virtual void configure(const std::map<std::string, std::string> &options);

    bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }

private:
    bool enabled_ = true;
};

} // namespace liva
