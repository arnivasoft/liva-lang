#include "liva/Sema/LifetimeAnalysis.h"
#include "liva/AST/ASTWalk.h"

#include <cassert>

namespace liva {

LifetimeAnalysis::LifetimeAnalysis(DiagnosticsEngine &diag) : diag_(diag) {}

// walkSubtree her fonksiyonun gövdesindeki deyimleri de gezer (bir FuncDecl
// ararken) ve analyzeFunction sonra aynı gövdeyi kendi mantığıyla tekrar gezer.
// Bu KASITLI ve zararsız: dış gezinti yalnız düğüm türüne bakıyor, gerçek iş
// analyzeFunction içinde. Dış gezintiyi "optimizasyon" için FuncDecl görünce
// durduracak biçimde yazmak, iç içe bildirim eklendiği gün sessizce kapsam
// kaybettirir.
//
// analyzeFunction idempotent DEĞİL — aynı fonksiyonu iki kez analiz etmek
// tanıları iki kez üretir. Bugün blok içinde yerel `func` parse edilmediği için
// iç içe FuncDecl yok, dolayısıyla her gövde tam bir kez veriliyor.
void LifetimeAnalysis::check(TranslationUnit &tu) {
    for (auto &decl : tu.getDeclarations()) {
        bool known = walkSubtree(decl.get(), [&](const ASTNode *node) {
            if (node->getKind() == ASTNode::NodeKind::FuncDecl)
                analyzeFunction(
                    const_cast<FuncDecl *>(static_cast<const FuncDecl *>(node)));
        });

        // walkSubtree sözleşmesi (ASTWalk.h): false = forEachChild tablosunda
        // bir NodeKind eksik ve o düğümün ÇOCUKLARI hiç gezilmedi. Bunu
        // sessizce yutmak, bu görevin kapattığı boşluk sınıfını (bir
        // fonksiyon gövdesinin ömür analizinden tamamen kaçması) FARKLI bir
        // NodeKind üzerinden yeniden açar — `BorrowLastUse.cpp:96-103`'ün
        // aynı dönüşü tüketip TAMAMEN vazgeçtiği (muhafazakâr) durumun
        // ikizi; burada "vazgeçilecek" bir optimizasyon yok, check()'in tek
        // işi keşif olduğu için karşılığı gürültülü bir hata sinyalidir.
        //
        // Yeni bir DiagID eklemek (DiagnosticKinds.def) bu görevin dosya
        // kapsamının (yalnız LifetimeAnalysis.cpp/.h) dışında, dolayısıyla
        // diag_ üzerinden gerçek bir tanı basılamıyor; assert() burada kalan
        // tek araç. `popTypeParams` (OwnershipChecker.cpp) örneğinden farklı
        // olarak assert'in Release'te (NDEBUG) düşmesinin bir UB riski YOK —
        // düşse de döngü bugünkü (bu satırdan önceki) davranışla aynı şekilde
        // devam eder, yalnızca eksik gövdeler yine sessiz kalır.
        //
        // Bu assert'in "getOrPanic" (IRGen.h) tuzağıyla aynı kaderi
        // paylaşmadığını doğrulamak önemliydi: yerel `build_clang.bat`
        // Release/NDEBUG derliyor, yani orada bu satır gerçekten ölü kod.
        // Ama .github/workflows/ci.yml'deki `sanitizer` (ASan+UBSan) ve
        // `coverage` job'ları `-DCMAKE_BUILD_TYPE=Debug` ile derleyip
        // `ownership_test`'i (dolayısıyla bu OwnershipTest gövdelerinin
        // TAMAMINI Sema üzerinden geçiren) ctest ile ÇALIŞTIRIYOR — yani
        // assert orada gerçekten canlı ve CI'da fiilen tetiklenebilir bir
        // denetim. Ayrıca -DLIVA_WERROR=ON olan hedeflerde forEachChild'ın
        // switch'i eksik bir case'te -Wswitch->-Werror ile zaten hiç
        // DERLENMEZ — bu iki katman birlikte, yalnızca yerel WERROR'siz
        // Release derlemesini korumasız bırakıyor.
        assert(known &&
               "walkSubtree bir NodeKind'i tanımadı: forEachChild'a yeni bir "
               "düğüm türü eklenip case eklenmesi unutuldu — altındaki "
               "gövdeler ömür analizinden kaçıyor olabilir");
        (void)known; // Release'te (NDEBUG) assert atılır, değişken kullanılmaz kalmasın
    }
}

void LifetimeAnalysis::analyzeFunction(FuncDecl *func) {
    if (!func->hasBody()) return;

    currentDepth_ = 0;
    variables_.clear();

    // Register parameters at depth 0
    for (auto &param : func->getParams()) {
        variables_[param.name] = {0, param.location, ""};
    }

    visitBlockStmt(const_cast<BlockStmt *>(func->getBody()));
}

void LifetimeAnalysis::visitNode(ASTNode *node) {
    if (!node) return;

    switch (node->getKind()) {
    case ASTNode::NodeKind::BlockStmt:
        visitBlockStmt(static_cast<BlockStmt *>(node));
        break;
    case ASTNode::NodeKind::VarDecl:
        visitVarDecl(static_cast<VarDecl *>(node));
        break;
    case ASTNode::NodeKind::ExprStmt: {
        auto *es = static_cast<ExprStmt *>(node);
        if (es->getExpr()->getKind() == ASTNode::NodeKind::AssignExpr) {
            visitAssignExpr(static_cast<AssignExpr *>(es->getExpr()));
        }
        break;
    }
    case ASTNode::NodeKind::IfStmt:
        visitIfStmt(static_cast<IfStmt *>(node));
        break;
    case ASTNode::NodeKind::WhileStmt:
        visitWhileStmt(static_cast<WhileStmt *>(node));
        break;
    case ASTNode::NodeKind::ForStmt:
        visitForStmt(static_cast<ForStmt *>(node));
        break;
    case ASTNode::NodeKind::ReturnStmt:
        // No lifetime concerns for return in this simple analysis
        break;
    // if-let/while-let gövdeleri BlockStmt ve VarDecl tutabiliyor, yani
    // `var p = ref x` orada bildirilebiliyor — default: break dalına düştükleri
    // için hiç görülmüyorlardı.
    //
    // MatchExpr BİLİNÇLİ olarak yok: MatchArm::body bir Expr ve NodeKind'da
    // BlockExpr olmadığı için `1 => { … }` parse edilmiyor; arm gövdesinde
    // `var p = ref x` bildirilemez, dolayısıyla bir MatchExpr dalı asla
    // tetiklenemezdi.
    case ASTNode::NodeKind::IfLetStmt: {
        auto *s = static_cast<IfLetStmt *>(node);
        visitNode(s->getThenBody());
        if (s->hasElse())
            visitNode(s->getElseBody());
        break;
    }
    case ASTNode::NodeKind::WhileLetStmt:
        visitNode(static_cast<WhileLetStmt *>(node)->getBody());
        break;
    default:
        break;
    }
}

void LifetimeAnalysis::visitBlockStmt(BlockStmt *node) {
    currentDepth_++;

    for (auto &stmt : node->getStatements()) {
        visitNode(stmt.get());
    }

    checkScopeExit(currentDepth_);

    // Remove variables at current depth
    for (auto it = variables_.begin(); it != variables_.end(); ) {
        if (it->second.scopeDepth == currentDepth_)
            it = variables_.erase(it);
        else
            ++it;
    }

    currentDepth_--;
}

void LifetimeAnalysis::visitVarDecl(VarDecl *node) {
    // Record the variable at current scope depth
    variables_[node->getName()] = {currentDepth_, node->getStartLoc(), ""};

    // Check if init is a ref expression: let r = ref x
    if (node->hasInit()) {
        std::string target = getRefTarget(node->getInit());
        if (!target.empty()) {
            variables_[node->getName()].refTarget = target;

            // Immediate check: if referenced var is at deeper scope, it's an error
            auto it = variables_.find(target);
            if (it != variables_.end() && it->second.scopeDepth > currentDepth_) {
                diag_.report(node->getStartLoc(), DiagID::err_borrow_outlives_value, target);
            }
        }
    }
}

void LifetimeAnalysis::visitAssignExpr(AssignExpr *node) {
    // Check: r = ref x (reassigning a reference variable)
    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        std::string target = getRefTarget(node->getValue());
        if (!target.empty()) {
            auto refIt = variables_.find(ident->getName());
            auto valIt = variables_.find(target);
            if (refIt != variables_.end() && valIt != variables_.end()) {
                refIt->second.refTarget = target;
                // If referenced var is at deeper scope than ref var, error
                if (valIt->second.scopeDepth > refIt->second.scopeDepth) {
                    diag_.report(node->getStartLoc(), DiagID::err_borrow_outlives_value, target);
                }
            }
        }
    }
}

void LifetimeAnalysis::visitIfStmt(IfStmt *node) {
    visitNode(node->getThenBody());
    if (node->hasElse()) {
        visitNode(node->getElseBody());
    }
}

void LifetimeAnalysis::visitWhileStmt(WhileStmt *node) {
    visitNode(const_cast<ASTNode *>(node->getBody()));
}

void LifetimeAnalysis::visitForStmt(ForStmt *node) {
    visitNode(const_cast<ASTNode *>(node->getBody()));
}

void LifetimeAnalysis::checkScopeExit(int exitingDepth) {
    // For each ref variable at a shallower depth,
    // check if it references a variable at the exiting depth
    for (auto &[name, info] : variables_) {
        if (info.refTarget.empty()) continue;
        if (info.scopeDepth >= exitingDepth) continue; // ref is also dying, no problem

        auto targetIt = variables_.find(info.refTarget);
        if (targetIt != variables_.end() && targetIt->second.scopeDepth == exitingDepth) {
            diag_.report(info.declLoc, DiagID::err_borrow_outlives_value, info.refTarget);
        }
    }
}

std::string LifetimeAnalysis::getRefTarget(const Expr *expr) const {
    if (!expr) return "";
    if (expr->getKind() == ASTNode::NodeKind::RefExpr) {
        auto *refExpr = static_cast<const RefExpr *>(expr);
        if (refExpr->getExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<const IdentifierExpr *>(refExpr->getExpr());
            return ident->getName();
        }
    }
    return "";
}

} // namespace liva
