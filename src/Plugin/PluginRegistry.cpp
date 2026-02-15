#include "liva/Plugin/PluginRegistry.h"
#include "liva/Plugin/BuiltinPlugins.h"
#include "liva/Driver/ProjectConfig.h"

namespace liva {

// === CompilerPlugin defaults ===

bool CompilerPlugin::afterParse(TranslationUnit &, DiagnosticsEngine &) {
    return true;
}

bool CompilerPlugin::afterSema(TranslationUnit &, DiagnosticsEngine &) {
    return true;
}

void CompilerPlugin::configure(const std::map<std::string, std::string> &) {}

// === PluginRegistry ===

void PluginRegistry::registerPlugin(std::unique_ptr<CompilerPlugin> plugin) {
    plugins_.push_back(std::move(plugin));
}

CompilerPlugin *PluginRegistry::getPlugin(const std::string &name) const {
    for (const auto &p : plugins_) {
        if (p->getName() == name)
            return p.get();
    }
    return nullptr;
}

bool PluginRegistry::runAfterParse(TranslationUnit &tu, DiagnosticsEngine &diag) {
    for (const auto &p : plugins_) {
        if (p->isEnabled()) {
            if (!p->afterParse(tu, diag))
                return false;
        }
    }
    return true;
}

bool PluginRegistry::runAfterSema(TranslationUnit &tu, DiagnosticsEngine &diag) {
    for (const auto &p : plugins_) {
        if (p->isEnabled()) {
            if (!p->afterSema(tu, diag))
                return false;
        }
    }
    return true;
}

void PluginRegistry::configureFromTOML(const TOMLDocument &doc) {
    auto it = doc.sections.find("plugins");
    if (it == doc.sections.end())
        return;

    for (const auto &kv : it->second) {
        CompilerPlugin *plugin = getPlugin(kv.first);
        if (!plugin)
            continue;
        if (kv.second.kind == TOMLValue::Boolean)
            plugin->setEnabled(kv.second.boolVal);
    }
}

PluginRegistry PluginRegistry::createWithBuiltins() {
    PluginRegistry reg;
    reg.registerPlugin(std::make_unique<NamingConventionPlugin>());
    reg.registerPlugin(std::make_unique<UnusedFunctionPlugin>());
    return reg;
}

} // namespace liva
