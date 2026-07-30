#pragma once

#include "liva/AST/ASTVisitor.h"
#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace liva {

/// Tracks the ownership state of a value
enum class OwnershipState : uint8_t {
    Owned,            // Value is owned and valid
    Moved,            // Value has been moved
    BorrowedImmutable,// Value has an active immutable borrow
    BorrowedMutable,  // Value has an active mutable borrow
    Dropped,          // Value has been dropped
};

/// Information about a tracked variable
struct OwnershipInfo {
    std::string name;
    OwnershipState state = OwnershipState::Owned;
    bool isMutable = false;
    bool isCopyType = false;  // Primitive types are Copy
    // Drop-conforming NAMED struct (conservative move-semantics scope).
    // NOT the negation of isCopyType — isCopyType is false for ALL named
    // structs (Drop or not); isDropType singles out only the Drop subset so
    // move semantics don't leak onto plain (copy-by-value) structs.
    bool isDropType = false;
    SourceLocation declLocation;
    SourceLocation lastMoveLocation;
    SourceLocation lastBorrowLocation;
    int borrowCount = 0;
    bool hasMutableBorrow = false;
    // `let r = ref x` — writing to this name targets the REFERENT, not the
    // binding, so the binding's own let/var mutability does not govern it.
    // TypeChecker owns that judgement (it knows whether the borrow is shared
    // or mutable); recording it here only stops a second, misleading
    // "declare with 'var'" complaint from firing alongside.
    bool isRefBinding = false;
    // For a reference binding, the borrow it HOLDS: the referent's name and
    // whether the borrow is mutable. The borrow itself is recorded on the
    // referent's OwnershipInfo, which usually lives in an OUTER scope, so
    // this is what lets scope exit give it back.
    std::string borrowsName;
    bool borrowsMutable = false;
};

/// Performs ownership and borrow checking on the AST
class OwnershipChecker : public ASTVisitor<OwnershipChecker> {
public:
    OwnershipChecker(DiagnosticsEngine &diag);

    /// Check ownership for a translation unit
    void check(TranslationUnit &tu);

    /// Provide the set of class type names. Classes are reference types, so
    /// they are treated as Copy (passing a class value does not move it).
    void setClassNames(std::unordered_set<std::string> names) {
        classNames_ = std::move(names);
    }

    /// Provide the set of Drop-conforming struct type names (from
    /// TypeChecker::getDropTypeNames(), same source IRGen's dropImplementors_
    /// mirrors). Only these types get move semantics on `let b = a` / `b = a`.
    void setDropTypeNames(std::unordered_set<std::string> names) {
        dropTypeNames_ = std::move(names);
    }

    void visitFuncDecl(FuncDecl *node);
    void visitClassDecl(ClassDecl *node);
    void visitTestDecl(TestDecl *node);
    void visitVarDecl(VarDecl *node);
    void visitBlockStmt(BlockStmt *node);
    void visitReturnStmt(ReturnStmt *node);
    void visitExprStmt(ExprStmt *node);
    void visitIfStmt(IfStmt *node);
    void visitWhileStmt(WhileStmt *node);
    void visitForStmt(ForStmt *node);
    void visitIfLetStmt(IfLetStmt *node);
    void visitWhileLetStmt(WhileLetStmt *node);

    void visitIdentifierExpr(IdentifierExpr *node);
    void visitAssignExpr(AssignExpr *node);
    void visitCallExpr(CallExpr *node);
    void visitBinaryExpr(BinaryExpr *node);
    void visitRefExpr(RefExpr *node);

    // Gezinti boşlukları: aşağıdaki düğüm türleri override EDİLMEDİĞİ için
    // ASTVisitor'ın no-op varsayılanına düşüyordu ve altlarındaki hiçbir
    // kullanım görülmüyordu. Hepsi yalnızca çocuklarını ziyaret eder —
    // ownership semantiği eklemezler, var olan denetimlerin alt ağaca
    // ulaşmasını sağlarlar.
    void visitUnaryExpr(UnaryExpr *node);
    void visitMemberExpr(MemberExpr *node);
    void visitIndexExpr(IndexExpr *node);
    void visitStructLiteralExpr(StructLiteralExpr *node);
    void visitMatchExpr(MatchExpr *node);
    void visitArrayLiteralExpr(ArrayLiteralExpr *node);
    void visitTupleLiteralExpr(TupleLiteralExpr *node);
    void visitCastExpr(CastExpr *node);
    void visitIsExpr(IsExpr *node);
    void visitGroupExpr(GroupExpr *node);
    void visitRangeExpr(RangeExpr *node);
    void visitUnwrapExpr(UnwrapExpr *node);
    void visitClosureExpr(ClosureExpr *node);
    void visitTryExpr(TryExpr *node);
    void visitTernaryExpr(TernaryExpr *node);
    void visitAwaitExpr(AwaitExpr *node);
    void visitYieldExpr(YieldExpr *node);
    void visitComptimeExpr(ComptimeExpr *node);
    void visitMacroInvokeExpr(MacroInvokeExpr *node);

    void visitImplDecl(ImplDecl *node);
    void visitProtocolDecl(ProtocolDecl *node);
    void visitStructDecl(StructDecl *node);
    void visitFieldDecl(FieldDecl *node);

    bool hasErrors() const { return diag_.hasErrors(); }

private:
    /// Düğümün çocuklarını kaynak sırasında ziyaret eder. Ownership semantiği
    /// eklemez — var olan kullanım/taşıma/ödünç denetimlerinin alt ağaca
    /// ulaşmasını sağlar.
    void visitChildren(ASTNode *node);

    /// Track a new variable
    void trackVariable(const std::string &name, bool isMutable, bool isCopyType,
                       bool isDropType, SourceLocation loc);

    /// Mark a variable as moved
    void markMoved(const std::string &name, SourceLocation loc);

    /// Check if a variable can be used (not moved/dropped)
    bool checkUse(const std::string &name, SourceLocation loc);

    /// Check if a variable can be mutated
    bool checkMutation(const std::string &name, SourceLocation loc);

    /// Add a borrow
    bool addBorrow(const std::string &name, bool isMutable, SourceLocation loc);

    /// Release ALL borrows on a variable. Scope-exit only — the variable is
    /// going away, so precision does not matter there.
    void releaseBorrows(const std::string &name);

    /// Release exactly ONE borrow, the counterpart of a single addBorrow.
    /// Used for `ref`/`ref mut` call ARGUMENTS, whose borrow ends with the
    /// call: a blanket releaseBorrows would also clear borrows held by live
    /// `let r = ref x` bindings on the same variable.
    void releaseBorrow(const std::string &name, bool isMutable);

    /// Set by visitRefExpr when it actually registered a borrow, so
    /// visitCallExpr can release precisely that borrow once the call is done.
    /// Empty name means "no borrow was taken" (untracked variable, a
    /// non-identifier operand, or a rejected borrow).
    std::pair<std::string, bool> lastRefBorrow_;

    /// Check if a type is a Copy type (primitives)
    bool isCopyType(const TypeRepr *type) const;

    /// Bir adın kapsamdaki (henüz çözülmemiş) bir generik tip parametresi
    /// olup olmadığı. `impl Stream<T>` içindeki `T` gibi.
    bool isTypeParamInScope(const std::string &name) const;

    /// Bir bildirimin tip parametrelerini kapsama it / kapsamdan çıkar.
    /// İç içe generic'ler (generik impl içinde generik metot) üst üste yığılır.
    void pushTypeParams(const std::vector<std::string> &params);
    void popTypeParams();

    /// check()'e verilen AST'deki fonksiyon/metot bildirimlerini ada göre
    /// indeksler. Yalnız `dyn Protocol` parametrelerini tanımak için — başka
    /// bir fazdan gelen bir bilgi DEĞİL.
    void collectFuncDecls(TranslationUnit &tu);

    /// `name` adlı çağrılanın `argIndex`'inci ARGÜMANINA karşılık gelen
    /// parametresi `dyn Protocol` mu. `isMemberCall` çağrı BİÇİMİDİR ve
    /// adayları eler: üye çağrısı yalnız `self`'li adaylarla, serbest çağrı
    /// yalnız `self`'siz adaylarla eşleşir. Biçim eşleşmesinden geçen TÜM
    /// adaylar hemfikirse true — biri bile değilse ya da hiç aday yoksa
    /// muhafazakâr yön (taşıma) korunur.
    bool paramIsDynProtocol(const std::string &name, bool isMemberCall,
                            size_t argIndex) const;

    /// Check if a type is a Drop-conforming NAMED struct (dropTypeNames_).
    bool isDropType(const TypeRepr *type) const;

    /// Drop all owned values at scope exit
    void dropScopeVariables();

    /// Push/pop scope for ownership tracking
    void pushOwnershipScope();
    void popOwnershipScope();

    /// Get ownership info for a variable
    OwnershipInfo *getInfo(const std::string &name);

    DiagnosticsEngine &diag_;

    /// Stack of scopes, each containing variable ownership info
    /// Using deque so that emplace_back() never invalidates pointers
    /// stored in allVariables_ (vector reallocation would dangle them).
    std::deque<std::unordered_map<std::string, OwnershipInfo>> scopeStack_;

    /// All tracked variables (flat lookup)
    std::unordered_map<std::string, OwnershipInfo *> allVariables_;

    /// Class type names — treated as Copy (reference types, not moved).
    std::unordered_set<std::string> classNames_;

    /// Drop-conforming struct type names — get move semantics on `let b = a`
    /// / `b = a` (see setDropTypeNames()).
    std::unordered_set<std::string> dropTypeNames_;

    /// Kapsamdaki generik tip parametresi adları, bildirim düzeyi başına bir
    /// giriş. Yığın olması `impl Stream<T>` içindeki `func map<U>()`'nun hem
    /// T'yi hem U'yu görmesini, metot bittiğinde ise yalnız U'nun düşmesini
    /// sağlar. Derinlik tek haneli olduğu için doğrusal tarama yeterli.
    std::vector<std::vector<std::string>> typeParamScopes_;

    /// Ada göre fonksiyon/metot bildirimleri (aşırı yükleme ve aynı adlı
    /// metotlar için birden çok aday olabilir). Bkz. collectFuncDecls().
    std::unordered_map<std::string, std::vector<const FuncDecl *>> funcsByName_;
};

} // namespace liva
