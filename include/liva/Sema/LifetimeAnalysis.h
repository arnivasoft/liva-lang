#pragma once

#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace liva {

/// Lifetime identifier (e.g., @a)
struct Lifetime {
    std::string name;
    bool isStatic = false;

    static Lifetime staticLifetime() { return {"static", true}; }
    static Lifetime anonymous() { return {"_", false}; }
};

/// Performs scope-based lifetime analysis.
/// Detects when a reference outlives the value it borrows.
class LifetimeAnalysis {
public:
    LifetimeAnalysis(DiagnosticsEngine &diag);

    /// TU'daki HER fonksiyon gövdesini analiz eder — top-level `func`'lar,
    /// `impl`/`class` metotları ve protokol default gövdeleri dahil.
    ///
    /// Faz 3 eskiden Sema.cpp'de kendi döngüsünü yazıyor ve yalnız top-level
    /// FuncDecl'leri geziyordu; faz 1 ve 2 ise TU'yu alıyordu. Bu giriş o
    /// asimetriyi hem davranışta hem imzada kapatıyor.
    void check(TranslationUnit &tu);

    /// Analyze lifetimes in a function
    void analyzeFunction(FuncDecl *func);

    bool hasErrors() const { return diag_.hasErrors(); }

private:
    DiagnosticsEngine &diag_;

    /// Variable tracking info
    struct VarInfo {
        int scopeDepth;
        SourceLocation declLoc;
        std::string refTarget; // non-empty if this var is a reference to another var
    };

    int currentDepth_ = 0;
    std::unordered_map<std::string, VarInfo> variables_;

    /// analyzeFunction'ın eskiden kendi içinde açık yazdığı "durumu sıfırla,
    /// başlangıç bağlamalarını (varsa parametreler) depth 0'a kaydet, gövdeyi
    /// gez" üçlüsü. TestDecl ve ClosureExpr gövdeleri de aynı üçlüye ihtiyaç
    /// duyduğu için buraya ayrıldı.
    ///
    /// `params` bilerek variables_'a ÖNCEDEN yazılmış bir kayıt olarak değil,
    /// bir argüman olarak alınıyor: variables_.clear() bu fonksiyonun İÇİNDE
    /// çalışıyor, dolayısıyla çağrılmadan önce variables_'a yazılan hiçbir şey
    /// hayatta kalamazdı (parametreler sessizce kaybolurdu). Parametreleri
    /// argüman olarak taşımak clear()'dan SONRA kaydedilmelerini garanti eder.
    void analyzeBody(const BlockStmt *body,
                      const std::vector<std::pair<std::string, SourceLocation>> &params = {});

    void visitNode(ASTNode *node);
    void visitBlockStmt(BlockStmt *node);
    void visitVarDecl(VarDecl *node);
    void visitAssignExpr(AssignExpr *node);
    void visitIfStmt(IfStmt *node);
    void visitWhileStmt(WhileStmt *node);
    void visitForStmt(ForStmt *node);

    /// Check for outlives violations when scope exits
    void checkScopeExit(int exitingDepth);

    /// Extract ref target name from a RefExpr
    std::string getRefTarget(const Expr *expr) const;
};

} // namespace liva
