#pragma once

#include "liva/AST/ASTNode.h"

#include <memory>
#include <string>
#include <vector>

namespace liva {

struct LastUseResult {
    /// Ödüncün bırakılabileceği deyim indeksi. shortenable false iken
    /// anlamsızdır.
    size_t stmtIndex = 0;
    /// false → hiç kısaltma yapma; ödünç kapsam çıkışına kadar yaşamalı.
    bool shortenable = false;
};

/// `stmts` içinde `name` adının son geçtiği deyimi bulur.
///
/// Tarama aralığı: [declIndex + 1, stmts.size()) — bildirim deyiminden sonraki
/// her deyimin TÜM alt ağacı gezilir. Aralıkta hiç kullanım yoksa
/// stmtIndex == declIndex döner, yani ödünç bildirim deyimi bittiği anda
/// bırakılabilir.
///
/// Dört durumda shortenable = false döner (hepsi muhafazakâr ret yönünde):
///   1. walkSubtree tanınmayan bir düğüm bildirdi.
///   2. Ad bir ClosureExpr alt ağacında geçiyor — closure saklanıp sonra
///      çağrılabilir.
///   3. Aralıkta GENİŞLETİLMEMİŞ bir MacroInvokeExpr var — token'ları henüz
///      AST değil.
///   4. Ad bir RefExpr'in operandı olarak geçiyor (`let s = ref r`) — geçişli
///      yeniden ödünç.
LastUseResult findLastUse(const std::vector<std::unique_ptr<ASTNode>> &stmts,
                          size_t declIndex, const std::string &name);

} // namespace liva
