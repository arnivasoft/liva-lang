#pragma once

#include "liva/Plugin/PluginAPI.h"
#include <memory>
#include <string>
#include <vector>

namespace liva {

class TranslationUnit;
class DiagnosticsEngine;
struct TOMLDocument;

/// Registry that manages compiler plugins and runs their hooks.
class PluginRegistry {
public:
    void registerPlugin(std::unique_ptr<CompilerPlugin> plugin);
    CompilerPlugin *getPlugin(const std::string &name) const;
    size_t size() const { return plugins_.size(); }

    /// Run all enabled plugins' afterParse hooks. Returns false if any fails.
    bool runAfterParse(TranslationUnit &tu, DiagnosticsEngine &diag);

    /// Run all enabled plugins' afterSema hooks. Returns false if any fails.
    bool runAfterSema(TranslationUnit &tu, DiagnosticsEngine &diag);

    /// Configure plugins from TOML [plugins] section.
    void configureFromTOML(const TOMLDocument &doc);

    void clear() { plugins_.clear(); }

    /// Create a registry pre-loaded with built-in plugins.
    static PluginRegistry createWithBuiltins();

private:
    std::vector<std::unique_ptr<CompilerPlugin>> plugins_;
};

} // namespace liva
