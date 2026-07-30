#include "liva/Sema/BorrowLastUse.h"

#include "liva/AST/ASTWalk.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"

namespace liva {

namespace {

struct ScanState {
    const std::string &name;
    bool used = false;    // ad bu deyimde geçiyor
    bool blocked = false; // bir geri-çekilme kuralı tetiklendi
};

/// Adın bir kullanımı mı, yoksa deyim sırasıyla sınırlanamayan bir erişim mi
/// olduğunu ayırır.
void inspect(const ASTNode *n, ScanState &st) {
    switch (n->getKind()) {
    case ASTNode::NodeKind::IdentifierExpr:
        if (static_cast<const IdentifierExpr *>(n)->getName() == st.name)
            st.used = true;
        return;

    case ASTNode::NodeKind::RefExpr: {
        // Kural 4: `let s = ref r` ödüncü zincirliyor; r'nin ödüncünü bırakmak
        // s üzerinden erişilebilirliği görmezden gelirdi.
        auto *re = static_cast<const RefExpr *>(n);
        const Expr *inner = re->getExpr();
        if (inner && inner->getKind() == ASTNode::NodeKind::IdentifierExpr &&
            static_cast<const IdentifierExpr *>(inner)->getName() == st.name)
            st.blocked = true;
        return;
    }

    case ASTNode::NodeKind::ClosureExpr: {
        // Kural 2: closure gövdesindeki kullanım deyim sırasıyla sınırlı değil.
        // İç tarama, bayrağın YALNIZ bu ad için tetiklenmesini sağlıyor.
        const std::string &target = st.name;
        bool found = false;
        walkSubtree(n, [&](const ASTNode *inner) {
            if (inner->getKind() == ASTNode::NodeKind::IdentifierExpr &&
                static_cast<const IdentifierExpr *>(inner)->getName() == target)
                found = true;
        });
        if (found)
            st.blocked = true;
        return;
    }

    case ASTNode::NodeKind::MacroInvokeExpr:
        // Kural 3: genişletilmemiş makronun token'ları AST değil.
        if (!static_cast<const MacroInvokeExpr *>(n)->getExpanded())
            st.blocked = true;
        return;

    default:
        return;
    }
}

} // namespace

LastUseResult findLastUse(const std::vector<std::unique_ptr<ASTNode>> &stmts,
                          size_t declIndex, const std::string &name) {
    LastUseResult result;
    // Aralıkta kullanım yoksa ödünç bildirim deyimi bittiği anda düşer.
    result.stmtIndex = declIndex;
    result.shortenable = true;

    for (size_t i = declIndex + 1; i < stmts.size(); ++i) {
        ScanState st{name};
        bool known = walkSubtree(stmts[i].get(),
                                 [&](const ASTNode *n) { inspect(n, st); });

        // Kural 1 ya da 2/3/4: tamamen vazgeç, kapsam çıkışı bırakır.
        if (!known || st.blocked) {
            result.shortenable = false;
            return result;
        }

        if (st.used)
            result.stmtIndex = i;
    }

    return result;
}

} // namespace liva
