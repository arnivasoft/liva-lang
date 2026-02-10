#include "liva/Sema/ModuleLoader.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <fstream>
#include <sstream>

namespace liva {

void ModuleLoader::registerSource(const std::string &name, const std::string &source) {
    testSources_[name] = source;
}

std::string ModuleLoader::resolveModuleName(const std::vector<std::string> &path) {
    std::string result;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            result += "::";
        result += path[i];
    }
    return result;
}

std::string ModuleLoader::resolveFilePath(const std::vector<std::string> &path) {
    std::string filePath = basePath_;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            filePath += "/";
        filePath += path[i];
    }
    filePath += ".liva";
    return filePath;
}

void ModuleLoader::collectExportedSymbols(TranslationUnit &tu, std::vector<Symbol> &out) {
    for (auto &decl : tu.getDeclarations()) {
        if (auto *f = dynamic_cast<FuncDecl *>(decl.get())) {
            if (f->isPublic()) {
                Symbol sym;
                sym.name = f->getName();
                sym.kind = Symbol::Kind::Function;
                sym.funcDecl = f;
                sym.type = f->getReturnType();
                sym.isPublic = true;
                out.push_back(sym);
            }
        } else if (auto *s = dynamic_cast<StructDecl *>(decl.get())) {
            if (s->isPublic()) {
                Symbol sym;
                sym.name = s->getName();
                sym.kind = Symbol::Kind::StructType;
                sym.structDecl = s;
                sym.isPublic = true;
                out.push_back(sym);
            }
        } else if (auto *e = dynamic_cast<EnumDecl *>(decl.get())) {
            if (e->isPublic()) {
                Symbol sym;
                sym.name = e->getName();
                sym.kind = Symbol::Kind::EnumType;
                sym.enumDecl = e;
                sym.isPublic = true;
                out.push_back(sym);
            }
        } else if (auto *p = dynamic_cast<ProtocolDecl *>(decl.get())) {
            if (p->isPublic()) {
                Symbol sym;
                sym.name = p->getName();
                sym.kind = Symbol::Kind::ProtocolType;
                sym.protocolDecl = p;
                sym.isPublic = true;
                out.push_back(sym);
            }
        }
    }
}

ModuleLoader::Module *ModuleLoader::loadModule(
    const std::vector<std::string> &importPath,
    DiagnosticsEngine &callerDiag,
    SourceLocation loc) {

    std::string moduleName = resolveModuleName(importPath);

    // Check cache
    auto cacheIt = cache_.find(moduleName);
    if (cacheIt != cache_.end())
        return cacheIt->second.get();

    // Circular import detection
    if (loading_.count(moduleName)) {
        callerDiag.report(loc, DiagID::err_circular_import, moduleName);
        return nullptr;
    }
    loading_.insert(moduleName);

    // Find source code
    std::string source;
    std::string filename;
    auto testIt = testSources_.find(moduleName);
    if (testIt != testSources_.end()) {
        source = testIt->second;
        filename = moduleName + ".liva";
    } else {
        filename = resolveFilePath(importPath);
        std::ifstream file(filename);
        if (!file.is_open()) {
            loading_.erase(moduleName);
            callerDiag.report(loc, DiagID::err_module_not_found, moduleName);
            return nullptr;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        source = ss.str();
    }

    // Parse
    auto mod = std::make_unique<Module>();
    mod->name = moduleName;
    mod->sm = std::make_unique<SourceManager>(filename, source);
    mod->diag.setSourceManager(mod->sm.get());

    Lexer lexer(*mod->sm, mod->diag);
    Parser parser(lexer, mod->diag);
    mod->tu = parser.parseTranslationUnit();

    if (mod->diag.hasErrors()) {
        loading_.erase(moduleName);
        callerDiag.report(loc, DiagID::err_module_not_found, moduleName);
        return nullptr;
    }

    // Type-check the module (with this loader for recursive imports)
    Sema sema(mod->diag, this);
    sema.analyze(*mod->tu);

    if (mod->diag.hasErrors()) {
        loading_.erase(moduleName);
        callerDiag.report(loc, DiagID::err_module_not_found, moduleName);
        return nullptr;
    }

    // Collect exported symbols
    collectExportedSymbols(*mod->tu, mod->exportedSymbols);

    loading_.erase(moduleName);
    auto *ptr = mod.get();
    cache_[moduleName] = std::move(mod);
    return ptr;
}

} // namespace liva
