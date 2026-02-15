#pragma once

#include "liva/Plugin/PluginAPI.h"

namespace liva {

/// Checks that type names use PascalCase and function names use camelCase.
class NamingConventionPlugin : public CompilerPlugin {
public:
    std::string getName() const override { return "naming-convention"; }
    std::string getDescription() const override {
        return "Checks PascalCase for types and camelCase for functions";
    }
    bool afterParse(TranslationUnit &tu, DiagnosticsEngine &diag) override;
};

/// Detects top-level functions that are defined but never called.
class UnusedFunctionPlugin : public CompilerPlugin {
public:
    std::string getName() const override { return "unused-function"; }
    std::string getDescription() const override {
        return "Detects functions that are defined but never called";
    }
    bool afterSema(TranslationUnit &tu, DiagnosticsEngine &diag) override;
};

} // namespace liva
