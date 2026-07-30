#pragma once

#include "liva/AST/ASTNode.h"

#include <functional>

namespace liva {

/// node'un DOĞRUDAN çocuklarını kaynak sırasıyla fn'e verir. Null çocuklar
/// (değersiz return, else'siz if) atlanır.
///
/// Dönüş: false = bu düğüm türü tabloda yok, çağıran muhafazakâr davranmalı.
/// switch'te `default:` dalı OLMADIĞI için yeni bir NodeKind eklendiğinde
/// -Wswitch derleme hatası verir; bu yüzden false dönüşü pratikte ulaşılamaz
/// ve yalnızca API sözleşmesi olarak duruyor. `default:` EKLEMEYİN — garantiyi
/// yok eder.
bool forEachChild(const ASTNode *node,
                  const std::function<void(const ASTNode *)> &fn);

/// node ve tüm alt ağacı üzerinde pre-order gezinti — node'un KENDİSİ de fn'e
/// verilir. Ziyaret edilen HERHANGİ bir düğümde tanınmama olursa false döner.
bool walkSubtree(const ASTNode *node,
                 const std::function<void(const ASTNode *)> &fn);

} // namespace liva
