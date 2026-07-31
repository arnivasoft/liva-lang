#include "liva/Sema/OwnershipChecker.h"
#include "liva/AST/ASTWalk.h"
#include "liva/Sema/BorrowLastUse.h"
#include <cassert>

namespace liva {

namespace {

/// Bir atama hedefi zincirinin kök değişkenini soyar:
///   w.a.b = x   ->  "w"
///   arr[i] = x  ->  "arr"
///   (w).id = x  ->  "w"
/// Kök bir IdentifierExpr değilse (ör. `f().x = 1`) nullptr döner ve çağıran
/// denetimi atlar — izlenmeyen hedefte susmak, getInfo'nun izlenmeyen
/// değişkenlerde sessizce geçmesiyle tutarlı.
const IdentifierExpr *rootIdentifier(const Expr *target) {
    const Expr *cur = target;
    while (cur) {
        switch (cur->getKind()) {
        case ASTNode::NodeKind::IdentifierExpr:
            return static_cast<const IdentifierExpr *>(cur);
        case ASTNode::NodeKind::MemberExpr:
            cur = static_cast<const MemberExpr *>(cur)->getObject();
            break;
        case ASTNode::NodeKind::IndexExpr:
            cur = static_cast<const IndexExpr *>(cur)->getBase();
            break;
        case ASTNode::NodeKind::GroupExpr:
            cur = static_cast<const GroupExpr *>(cur)->getExpr();
            break;
        default:
            return nullptr;
        }
    }
    return nullptr;
}

/// `target`, `rootIdentifier`in çözdüğü kökü bir MemberExpr/IndexExpr
/// katmanından geçerek mi işaret ediyor, yoksa (parantezler bir yana) kökün
/// KENDİSİ mi? Ayrım önemli çünkü sınıf-alanı muafiyeti yalnız BİLEŞİK
/// hedeflerde (`a.field = x`, `arr[i].field = x`) uygulanmalı — çıplak-ad
/// hedefi (`a = başkaNesne`, `(a) = başkaNesne`) referansı YENİDEN BAĞLIYOR ve
/// bu, class için de struct için de her zaman `let`/`var` denetimine tabi.
/// Yalnız baştaki GroupExpr katmanları soyulur (parantez gruplamadır, bir
/// üye/indeks erişimi değil); bir MemberExpr/IndexExpr'e ulaşmadan doğrudan
/// IdentifierExpr'e inilirse bileşik SAYILMAZ.
bool isCompositeAssignTarget(const Expr *target) {
    const Expr *cur = target;
    while (cur && cur->getKind() == ASTNode::NodeKind::GroupExpr) {
        cur = static_cast<const GroupExpr *>(cur)->getExpr();
    }
    return cur && (cur->getKind() == ASTNode::NodeKind::MemberExpr ||
                  cur->getKind() == ASTNode::NodeKind::IndexExpr);
}

/// Bileşik bir atama hedefinin bir ÜST katmanındaki taban ifadesi:
///   arr[0].name = x  ->  arr[0]
///   a.name = x       ->  a
///   w.a.b = x        ->  w.a
/// `rootIdentifier` en dibe (`arr`) iner; bu ise YAZILAN NESNEnin ifadesini
/// verir, ki class muafiyeti için gereken tam olarak odur — `arr` bir dizi
/// ama `arr[0]` bir class örneği. Hedef bileşik değilse nullptr.
const Expr *compositeAssignBase(const Expr *target) {
    const Expr *cur = target;
    while (cur && cur->getKind() == ASTNode::NodeKind::GroupExpr) {
        cur = static_cast<const GroupExpr *>(cur)->getExpr();
    }
    if (!cur)
        return nullptr;
    if (cur->getKind() == ASTNode::NodeKind::MemberExpr)
        return static_cast<const MemberExpr *>(cur)->getObject();
    if (cur->getKind() == ASTNode::NodeKind::IndexExpr)
        return static_cast<const IndexExpr *>(cur)->getBase();
    return nullptr;
}

/// Bir dizi/Optional tipinin taşıdığı eleman tipi (yoksa nullptr). for-loop ve
/// if-let/while-let bağlamalarının statik tipini bulmak için — bağlama
/// KAPSAYICININ değil, ELEMANIN tipini alır.
const TypeRepr *containerElementType(const TypeRepr *type) {
    if (!type)
        return nullptr;
    if (type->getKind() == TypeRepr::Kind::Array)
        return static_cast<const ArrayTypeRepr *>(type)->getElement();
    if (type->getKind() == TypeRepr::Kind::Optional)
        return static_cast<const OptionalTypeRepr *>(type)->getInner();
    return nullptr;
}

} // namespace

OwnershipChecker::OwnershipChecker(DiagnosticsEngine &diag) : diag_(diag) {}

void OwnershipChecker::check(TranslationUnit &tu) {
    // TU'ya özel her durum girişte sıfırlanır — push/pop dengeli olduğu için
    // typeParamScopes_ zaten boş dönüyor, ama simetriyi burada kurmak, ileride
    // bir dengesizlik olursa onun sessizce BİR SONRAKİ TU'ya taşınmasını engelliyor.
    collectFuncDecls(tu);
    typeParamScopes_.clear();
    pushOwnershipScope();

    for (auto &decl : tu.getDeclarations()) {
        visit(decl.get());
    }

    popOwnershipScope();
}

void OwnershipChecker::visitFuncDecl(FuncDecl *node) {
    pushTypeParams(node->getTypeParams());
    pushOwnershipScope();

    // Track parameters
    for (auto &param : node->getParams()) {
        bool copyType = param.type ? isCopyType(param.type.get()) : true;
        bool dropType = param.type ? isDropType(param.type.get()) : false;
        bool classType = param.type ? isClassType(param.type.get()) : false;
        trackVariable(param.name, param.isMutRef, copyType, dropType,
                     param.location, classType);
    }

    if (node->getBody()) {
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    }

    dropScopeVariables();
    popOwnershipScope();
    popTypeParams();
}

void OwnershipChecker::visitTestDecl(TestDecl *node) {
    pushOwnershipScope();
    if (node->getBody())
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    popOwnershipScope();
}

void OwnershipChecker::visitClassDecl(ClassDecl *node) {
    pushTypeParams(node->getTypeParams());
    for (auto &m : node->getMembers()) {
        // Alanlar atlanıyordu, dolayısıyla computed property getter/setter'ları,
        // willSet/didSet gözlemcileri ve lazy init'ler denetim dışı kalıyordu.
        // Struct'lar aynı kapsamı StructDecl -> FieldDecl yoluyla alıyor.
        if (m.field) {
            visit(m.field.get());
        }
        if (m.method) {
            visitFuncDecl(const_cast<FuncDecl *>(m.method.get()));
        }
    }
    popTypeParams();
}

void OwnershipChecker::visitVarDecl(VarDecl *node) {
    // Visit initializer first
    if (node->hasInit()) {
        visit(const_cast<Expr *>(node->getInit()));
    }

    bool copyType;
    bool dropType = false;
    bool classType = false;
    const TypeRepr *type = node->getType();
    if (type && !type->isInferred()) {
        // Explicit type annotation — use it directly
        copyType = isCopyType(type);
        dropType = isDropType(type);
        classType = isClassType(type);
    } else if (node->hasInit() && node->getInit()->getResolvedType()) {
        // Inferred type — use the init expression's resolved type
        copyType = isCopyType(node->getInit()->getResolvedType());
        dropType = isDropType(node->getInit()->getResolvedType());
        classType = isClassType(node->getInit()->getResolvedType());
    } else {
        // No type info available — default to Copy
        copyType = true;
    }

    // Move semantics: `let b = a` where `a` is a Drop-conforming struct moves
    // `a` (conservative scope — only Drop types; plain structs keep copy
    // behavior unchanged). Use the SOURCE variable's own tracked isDropType
    // flag rather than recomputing from `b`'s type, since that's the value
    // actually being consumed.
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *initIdent = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getInit()));
        auto *srcInfo = getInfo(initIdent->getName());
        if (srcInfo && srcInfo->isDropType) {
            markMoved(initIdent->getName(), node->getStartLoc());
        }
    }

    trackVariable(node->getName(), node->isMutable(), copyType, dropType,
                 node->getStartLoc(), classType);

    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::RefExpr) {
        if (auto *info = getInfo(node->getName())) {
            info->isRefBinding = true;
            // visitRefExpr ran while the initializer was visited above and
            // left the borrow it registered here, so the binding can give
            // that exact borrow back when its scope ends.
            info->borrowsName = lastRefBorrow_.first;
            info->borrowsMutable = lastRefBorrow_.second;
        }
    }
}

// Bir `ref` bağlamasının tuttuğu ödünç, bağlamanın SON KULLANIMINDAN sonra
// bırakılıyor — kapsam çıkışını beklemiyor (roadmap 134 (b)).
//
// Neden sağlam: bırakma noktası bağlamanın bildirildiği deyim listesinde ve
// bildirimden sonra. Deyim listeleri sırasaldır ve içlerinde geri kenar yoktur
// — döngüler tek bir deyimdir ve biz bir döngü deyimini TAMAMEN bittikten
// sonra bırakırız. Ad-tabanlı taramanın fazla saydığı durumlar (gölgeleme,
// dallar) bırakmayı yalnızca GECİKTİRİR, asla öne almaz.
//
// Erken çıkışlar (return/break/continue) bu bırakma noktasını ATLAMAZ: statik
// AST gezintisinde döngü, aşağıdaki `for (i = 0; i < stmts.size(); ++i)`
// deyim listesindeki HER indekse koşulsuz ulaşır — çalışma zamanında bir
// `return`'ün atladığı bir sonraki deyime Sema hiç bakmaz diye bir şey yok,
// çünkü burada çalışma zamanı yürütmesi değil, deyim listesinin SIRALI
// gezintisi var. Asıl yedek yol şu: `findLastUse` `shortenable == false`
// döndüğünde ya da `getInfo` `nullptr` olduğunda kayıt pendingRelease'e hiç
// girmiyor ve ödünç `dropScopeVariables`'a, yani kapsam çıkışına düşüyor.
void OwnershipChecker::visitBlockStmt(BlockStmt *node) {
    pushOwnershipScope();

    const auto &stmts = node->getStatements();
    // {bırakmanın yapılacağı deyim indeksi, bağlama adı}
    std::vector<std::pair<size_t, std::string>> pendingRelease;

    for (size_t i = 0; i < stmts.size(); ++i) {
        visit(stmts[i].get());

        // Bu deyim bir `ref` bağlaması ürettiyse, ödüncünü ne zaman geri
        // vereceğini şimdi hesapla. borrowsName boşsa (referent izlenmiyor:
        // global, alan, ya da reddedilmiş ödünç) bırakılacak bir şey yok.
        if (stmts[i]->getKind() == ASTNode::NodeKind::VarDecl) {
            auto *varDecl = static_cast<VarDecl *>(stmts[i].get());
            auto *info = getInfo(varDecl->getName());
            if (info && info->isRefBinding && !info->borrowsName.empty()) {
                LastUseResult lastUse =
                    findLastUse(stmts, i, varDecl->getName());
                if (lastUse.shortenable)
                    pendingRelease.emplace_back(lastUse.stmtIndex,
                                                varDecl->getName());
            }
        }

        // Bu deyimde ölen bağlamaların ödüncünü geri ver. Kullanımı olmayan
        // bir bağlama için indeks bildirim deyiminin kendisidir, o yüzden
        // kayıt aynı turda hem eklenip hem işlenebilir.
        for (auto it = pendingRelease.begin(); it != pendingRelease.end();) {
            if (it->first != i) {
                ++it;
                continue;
            }
            if (auto *bindingInfo = getInfo(it->second)) {
                releaseBorrow(bindingInfo->borrowsName,
                              bindingInfo->borrowsMutable);
                // ZORUNLU: kapsam çıkışının aynı ödüncü ikinci kez
                // düşürmesini, ve referent sonradan BAŞKA bir ödünç aldıysa
                // kapsam çıkışının o yeni ödüncü silmesini engelliyor.
                bindingInfo->borrowsName.clear();
            }
            it = pendingRelease.erase(it);
        }
    }

    dropScopeVariables();
    popOwnershipScope();
}

void OwnershipChecker::visitReturnStmt(ReturnStmt *node) {
    if (node->hasValue()) {
        visit(node->getValue());
    }
}

void OwnershipChecker::visitExprStmt(ExprStmt *node) { visit(node->getExpr()); }

void OwnershipChecker::visitIfStmt(IfStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));
    visit(node->getThenBody());
    if (node->hasElse()) {
        visit(node->getElseBody());
    }
}

void OwnershipChecker::visitWhileStmt(WhileStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));
    visit(const_cast<ASTNode *>(node->getBody()));
}

void OwnershipChecker::visitForStmt(ForStmt *node) {
    visit(const_cast<Expr *>(node->getIterable()));
    pushOwnershipScope();
    // Bağlamanın statik tipi = kapsayıcının ELEMAN tipi. Class ise bağlama da
    // class'tır ve `a.name = "Max"` yazımı muafiyeti almalıdır: `for var a in
    // arr` diye bir sözdizimi YOK, yani bağlamayı `var` yapmanın yolu da yok —
    // muafiyet olmadan class alanını döngü içinde yazmak imkânsız olurdu.
    bool elemIsClass =
        isClassType(containerElementType(node->getIterable()->getResolvedType()));
    trackVariable(node->getVarName(), false, true, false, node->getStartLoc(),
                  elemIsClass, /*isPatternBinding=*/true);
    visit(const_cast<ASTNode *>(node->getBody()));
    dropScopeVariables();
    popOwnershipScope();
}

// Task 2 (Drop/Move Tracking): if-let/while-let ownership transfer. Neither
// statement had an override before (ASTVisitor's default is a no-op), so
// nothing inside them was tracked at all. The binding takes ownership of a
// Drop-conforming payload; the source Optional identifier (if any) is marked
// moved — same conservative scope as `let b = a` (Task 1), extended to
// Optional<Drop-struct> via isCopyType/isDropType's new Optional-recursion.
//
// Decision (#2, repeated if-let): visiting the optional expr FIRST (before
// markMoved) routes an identifier source through the normal
// visitIdentifierExpr -> checkUse dispatch, so a SECOND if-let (or any other
// use) over an already-consumed Optional<Drop-struct> reports
// err_use_after_move — the conservative v1 chosen for this task (no
// re-entrant if-let over the same Drop-payload optional).
void OwnershipChecker::visitIfLetStmt(IfLetStmt *node) {
    visit(node->getOptionalExpr());

    bool payloadIsDrop = false;
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *srcInfo = getInfo(ident->getName());
        if (srcInfo && srcInfo->isDropType) {
            payloadIsDrop = true;
            markMoved(ident->getName(), node->getStartLoc());
        }
    }

    pushOwnershipScope();
    trackVariable(node->getBindingName(), /*isMutable=*/false,
                  /*isCopyType=*/!payloadIsDrop, /*isDropType=*/payloadIsDrop,
                  node->getStartLoc(),
                  isClassType(containerElementType(
                      node->getOptionalExpr()->getResolvedType())),
                  /*isPatternBinding=*/true);
    visit(node->getThenBody());
    dropScopeVariables();
    popOwnershipScope();

    if (node->hasElse()) {
        visit(node->getElseBody());
    }
}

// Decision (#3, while-let): same ownership-transfer shape as if-let, applied
// once at compile time (the AST node is visited once regardless of how many
// runtime iterations occur) — mirrors if-let's "mark source moved" exactly.
void OwnershipChecker::visitWhileLetStmt(WhileLetStmt *node) {
    visit(node->getOptionalExpr());

    bool payloadIsDrop = false;
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *srcInfo = getInfo(ident->getName());
        if (srcInfo && srcInfo->isDropType) {
            payloadIsDrop = true;
            markMoved(ident->getName(), node->getStartLoc());
        }
    }

    pushOwnershipScope();
    trackVariable(node->getBindingName(), /*isMutable=*/false,
                  /*isCopyType=*/!payloadIsDrop, /*isDropType=*/payloadIsDrop,
                  node->getStartLoc(),
                  isClassType(containerElementType(
                      node->getOptionalExpr()->getResolvedType())),
                  /*isPatternBinding=*/true);
    visit(node->getBody());
    dropScopeVariables();
    popOwnershipScope();
}

void OwnershipChecker::visitIdentifierExpr(IdentifierExpr *node) {
    checkUse(node->getName(), node->getStartLoc());
}

void OwnershipChecker::visitAssignExpr(AssignExpr *node) {
    // Visit the value first (may move a value)
    visit(node->getValue());

    // Move semantics: `b = a` where `a` is a Drop-conforming struct moves `a`
    // (same conservative scope as the `let b = a` case above). Note: `b`'s
    // OVERWRITTEN old value is not dropped here — that's a documented,
    // double-free-safe leak (see spec point 2), out of scope for this task.
    //
    // Gate mirrors IRGen's suppression EXACTLY (IRGenCall.cpp's plain-
    // identifier-target branch, itself gated on `Op::Assign`): IRGen only
    // suppresses the source's drop when the op is plain `=` AND the
    // assignment TARGET is a bare identifier. For any other target shape
    // (`x.f = a`, `arr[i] = a`) or any compound op (`+=` etc.), IRGen does
    // NOT suppress the drop — `a` still gets dropped normally at scope exit
    // — so marking it moved here would make Sema reject programs that
    // compile and run correctly (spurious err_use_after_move).
    if (node->getOp() == AssignExpr::Op::Assign &&
        node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr &&
        node->getValue()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *valIdent = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getValue()));
        auto *srcInfo = getInfo(valIdent->getName());
        if (srcInfo && srcInfo->isDropType) {
            markMoved(valIdent->getName(), node->getStartLoc());
        }
    }

    // Check target is mutable.
    //
    // Hedef çıplak bir ad olmak zorunda değil: `w.id = 9` ve `arr[i] = x` de
    // kök değişkeni YAZIYOR, dolayısıyla aynı değişebilirlik ve ödünç kuralına
    // tabidir. Bunlar önceden hiç denetlenmiyordu çünkü koşul yalnız
    // IdentifierExpr hedeflerini kabul ediyordu. Kök çözülemezse
    // (ör. `f().x = 1`) denetim atlanır.
    if (const IdentifierExpr *root = rootIdentifier(node->getTarget())) {
        auto *ident = const_cast<IdentifierExpr *>(root);
        // Writing THROUGH a reference binding mutates the referent, so the
        // binding's own let/var does not govern it — the borrow does, and
        // TypeChecker owns that judgement (err_assign_through_shared_ref).
        // Complaining here as well would attach a "declare with 'var'"
        // suggestion that does not describe the problem. Re-BINDING the
        // reference (`r = ref y`) is a different operation and still needs a
        // `var` binding, so it keeps the normal check.
        //
        // KAPSAM NOTU (bilinçli, tasarım belgesinden SAPMA): tasarım bu
        // istisnayı yalnız ÇIPLAK-AD hedefleri için tarif ediyordu ("bileşik
        // hedeflerde geçerli değil"), gerekçesi de `isRefBinding`'in bileşik
        // yolda hiç set edilmediği varsayımıydı. Varsayım YANLIŞ: `let r =
        // ref w` bayrağı tam olarak set eder ve `r.id = 99` kökü `r` olan
        // bileşik bir hedeftir, yani istisna orada da devreye girer ve o
        // deyim sıfır ownership tanısı üretir. Bu, uygulamanın BİLİNÇLİ
        // seçimi olarak korunuyor: gerekçe her iki hedef biçiminde de aynı —
        // yazılan şey referent'tir, dolayısıyla bağlamanın kendi `let`/`var`'ı
        // yargı mercii değildir; doğru tanıyı (paylaşımlı ödünç üzerinden
        // yazma) TypeChecker üretir. `r.id = 99`'un çalışma zamanında da
        // mutasyon yapmaması AYRI bir IRGen kusurudur (roadmap 134 KALAN).
        auto *targetInfo = getInfo(ident->getName());
        bool writeThroughRef =
            targetInfo && targetInfo->isRefBinding &&
            node->getValue()->getKind() != ASTNode::NodeKind::RefExpr;

        // Sınıflar REFERANS tipidir (LANGUAGE-REFERENCE.md: "Reference type
        // (shared)"): `let a = Animal(...)` içindeki `let`, REFERANSIN
        // KENDİSİNİ yeniden bağlamayı yönetir — işaret ettiği nesnenin
        // alanlarını değil. Dil zaten `a.deposit(50.0)` gibi `ref mut self`
        // alan bir metot üzerinden aynı mutasyona izin veriyordu; düz alan
        // yazımını (`a.name = "Max"`) reddetmek bu iki eşdeğer yol arasında
        // tutarsız bir yanlış-pozitif yaratırdı (bkz. isCopyType — sınıflar
        // zaten "referans, Copy sayılır" mantığıyla işleniyor). Muafiyet
        // YALNIZ bileşik hedeflerde (isCompositeAssignTarget) uygulanır:
        // çıplak-ad yeniden bağlaması (`a = başkaNesne`) hâlâ normal denetime
        // tabi, çünkü o gerçekten referansın KENDİSİNİ değiştiriyor ve `let`
        // tam olarak onu engellemeli. struct'lar (değer tipi) bu muafiyetten
        // ETKİLENMEZ — isClassType yalnız classNames_'teki tipler için true.
        //
        // Muafiyet iki KÖKTEN birine bakar, çünkü yalnız kökün DOĞRUDAN tipine
        // bakmak `arr[0].name = "Max"` (arr: `[Animal]`) gibi bileşik kökleri
        // dışarıda bırakıyordu — orada kök `arr` bir DİZİ, class değil. Yazılan
        // NESNE ise `arr[0]`, yani bir class örneği; onun çözülmüş tipine
        // bakmak muafiyeti dilin "class = referans tipi" tanımıyla hizalıyor.
        // Ölçüt hâlâ dar: taban ifadesinin tipi class DEĞİLSE (dizi, struct,
        // primitive) muafiyet yok, ve muafiyet yalnız BİLEŞİK hedeflerde.
        bool exemptClassFieldWrite = false;
        if (targetInfo && isCompositeAssignTarget(node->getTarget())) {
            if (targetInfo->isClassType) {
                exemptClassFieldWrite = true;
            } else if (const Expr *base =
                           compositeAssignBase(node->getTarget())) {
                exemptClassFieldWrite = isClassType(base->getResolvedType());
            }
        }

        // Değişebilirlik reddedilirse ödünç kontrolünü ATLA (eskisiyle aynı
        // davranış — iki ayrı tanının aynı deyimde çakışmasını önler), ama
        // `visit(node->getTarget())`'a HER ZAMAN ulaş: eski kod burada erken
        // `return` ediyordu ve bu, hedefin alt-ifadelerindeki (ör.
        // `arr[moved_var].x = 9`'daki `moved_var`) kullanım-sonrası-taşıma
        // denetimini o turda atlıyordu. Derleme sonucu değişmiyor (zaten
        // reddediliyor) ama tanı eksiksizliği düzeliyor.
        if (writeThroughRef || exemptClassFieldWrite ||
            checkMutation(ident->getName(), node->getStartLoc())) {
            // Ödünçlü bir değişkene doğrudan atama, ödüncün türünden
            // bağımsız olarak reddedilir. Yalnız BorrowedImmutable'a bakmak
            // DEĞİŞEBİLİR ödüncü sessizce geçiriyordu: `let r = ref mut k`
            // canlıyken `k = 42` hiç tanı üretmiyordu (Rust: E0506).
            //
            // Referente YAZMA (`r = 99`) bu kontrole girmiyor: hedef `r`'dir
            // ve ödünç `k` üzerinde kayıtlı, dolayısıyla r'nin kendi durumu
            // Owned. Class alanı muafiyeti bu kontrolü ETKİLEMEZ: canlı bir
            // ödünç varken class alanına yazmak yine reddedilir.
            auto *info = getInfo(ident->getName());
            if (info && (info->state == OwnershipState::BorrowedImmutable ||
                         info->state == OwnershipState::BorrowedMutable)) {
                diag_.report(node->getStartLoc(),
                             DiagID::err_move_while_borrowed, ident->getName());
            }
        }
    }

    visit(node->getTarget());
}

void OwnershipChecker::visitCallExpr(CallExpr *node) {
    visit(node->getCallee());

    // Borrows taken by `ref`/`ref mut` ARGUMENTS last only as long as the
    // call. Collected here and released once every argument is visited (not
    // immediately, so two arguments borrowing the same variable still
    // conflict with each other). Before this, the only path to a release was
    // dropScopeVariables, so an argument borrow lived to the end of the
    // enclosing scope and a variable could be borrowed mutably just once per
    // scope — `take(ref mut k)` twice in a row was rejected.
    std::vector<std::pair<std::string, bool>> argBorrows;

    // Çağrılanın adı VE çağrı BİÇİMİ: `f(x)` serbest-fonksiyon biçimi,
    // `o.m(x)` üye biçimi. İkisi de yalnız `dyn Protocol` parametrelerini
    // tanımak için kullanılır. Biçim, adayları elemek için ZORUNLU: ad
    // eşleşmesi tek başına `arr.push(p)` çağrısını, TU'da bulunan alakasız
    // bir `func push(g: dyn Greeter)` ile eşleştiriyordu ve `push`/`get`/
    // `close` gibi yaygın adlarda tüm TU'da tanı sessizce kayboluyordu.
    std::string calleeName;
    bool calleeIsMemberCall = false;
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        calleeName =
            static_cast<const IdentifierExpr *>(node->getCallee())->getName();
    } else if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        calleeName =
            static_cast<const MemberExpr *>(node->getCallee())->getMember();
        calleeIsMemberCall = true;
    }

    // Each argument is either copied (if Copy type) or moved
    const auto &args = node->getArgs();
    for (size_t ai = 0; ai < args.size(); ++ai) {
        const auto &arg = args[ai];
        // Check if it's a ref expression
        if (arg->getKind() == ASTNode::NodeKind::RefExpr) {
            visit(arg.get());
            if (!lastRefBorrow_.first.empty())
                argBorrows.push_back(lastRefBorrow_);
            continue;
        }

        visit(arg.get());

        // If argument is a simple identifier and non-Copy, it's moved
        if (arg->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(arg.get());
            auto *info = getInfo(ident->getName());
            // `dyn Protocol` parametresine değer geçişi TAŞIMA DEĞİL, ödünç:
            // IRGen (IRGenCall.cpp) fat pointer'a değişkenin KENDİ
            // alloca'sının ADRESİNİ yazar — derin kopya da yok, tüketme de.
            // Değişken çağrıdan sonra hâlâ geçerli, o yüzden `dump(db);
            // db.close()` çalışma zamanında doğru ve reddedilmemeli.
            bool dynBorrow =
                !calleeName.empty() &&
                paramIsDynProtocol(calleeName, calleeIsMemberCall, ai);
            if (info && !info->isCopyType && !dynBorrow) {
                markMoved(ident->getName(), arg->getStartLoc());
            }
        }
    }

    for (const auto &[name, isMutable] : argBorrows)
        releaseBorrow(name, isMutable);
}

void OwnershipChecker::visitBinaryExpr(BinaryExpr *node) {
    visit(node->getLHS());
    visit(node->getRHS());
}

void OwnershipChecker::visitRefExpr(RefExpr *node) {
    lastRefBorrow_ = {};
    if (node->getExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getExpr()));
        if (!addBorrow(ident->getName(), node->isMutable(), node->getStartLoc())) {
            return;
        }
        // addBorrow also returns true for a variable it does not track, in
        // which case nothing was recorded and nothing may be released.
        if (getInfo(ident->getName()))
            lastRefBorrow_ = {ident->getName(), node->isMutable()};

        // Check mutable ref to immutable variable
        if (node->isMutable()) {
            auto *info = getInfo(ident->getName());
            if (info && !info->isMutable) {
                diag_.report(node->getStartLoc(), DiagID::err_mut_ref_to_immutable,
                             ident->getName());
                diag_.report(node->getStartLoc(), DiagID::note_use_var_for_mutable);
            }
        }
    }

    visit(const_cast<Expr *>(node->getExpr()));
}

// === Gezinti boşlukları ===
//
// ASTVisitor'ın varsayılanları no-op olduğu için, override edilmeyen her düğüm
// türü ziyaret zincirini KESİYORDU: `send(pkt); println(pkt.size)` sessizce
// derleniyor, aynı programın `send(pkt); send(pkt)` yazımı ise reddediliyordu.
// Aşağıdakiler yeni bir ownership kuralı getirmez — yalnız var olan
// denetimlerin (checkUse/markMoved/addBorrow) alt ağaca ulaşmasını sağlar.
// Doğru okuma semantiği bedavaya gelir: pkt.size -> visitIdentifierExpr(pkt)
// -> checkUse(pkt).

void OwnershipChecker::visitChildren(ASTNode *node) {
    forEachChild(node, [&](const ASTNode *child) {
        visit(const_cast<ASTNode *>(child));
    });
}

void OwnershipChecker::visitUnaryExpr(UnaryExpr *node) { visitChildren(node); }
void OwnershipChecker::visitMemberExpr(MemberExpr *node) { visitChildren(node); }
void OwnershipChecker::visitIndexExpr(IndexExpr *node) { visitChildren(node); }
void OwnershipChecker::visitStructLiteralExpr(StructLiteralExpr *node) {
    visitChildren(node);
}
// Match arm'ları KENDİ kapsamlarını alır. `visitChildren`'a bırakmak yetmiyordu:
// `forEachChild(MatchExpr)` yalnız subject/guard/body veriyor, arm KALIPLARININ
// bağladığı adlar hiç izlenmiyordu — dolayısıyla `Shape.Circle(pkt) =>`
// arm'ındaki `pkt`, DIŞTAKİ `pkt` değişkenine çözülüyordu. Dıştaki taşınmışsa
// arm gövdesi haksız yere reddediliyor; arm gövdesi taşıyorsa dıştaki bozuluyor
// ve hata match'ten SONRAKİ bir satırda çıkıyordu.
void OwnershipChecker::visitMatchExpr(MatchExpr *node) {
    visit(const_cast<Expr *>(node->getSubject()));
    for (const auto &arm : node->getArms()) {
        pushOwnershipScope();
        trackPatternBindings(arm.patternNode.get(), node->getStartLoc());
        if (arm.guard)
            visit(arm.guard.get());
        if (arm.body)
            visit(arm.body.get());
        dropScopeVariables();
        popOwnershipScope();
    }
}
void OwnershipChecker::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    visitChildren(node);
}
void OwnershipChecker::visitTupleLiteralExpr(TupleLiteralExpr *node) {
    visitChildren(node);
}
void OwnershipChecker::visitCastExpr(CastExpr *node) { visitChildren(node); }
void OwnershipChecker::visitIsExpr(IsExpr *node) { visitChildren(node); }
void OwnershipChecker::visitGroupExpr(GroupExpr *node) { visitChildren(node); }
void OwnershipChecker::visitRangeExpr(RangeExpr *node) { visitChildren(node); }
void OwnershipChecker::visitUnwrapExpr(UnwrapExpr *node) { visitChildren(node); }
// Closure GÖVDESİ ziyaret edilir: closure tanımlanmadan ÖNCE taşınmış bir
// değişkenin yakalanması yakalanır. Closure tanımlandıktan SONRA taşınan bir
// değişkenin closure ÇAĞRISINDA kullanılması yakalanmaz — bu bir kaçırma
// (muhafazakâr), yanlış-pozitif değil; gerçek çözümü CFG ister.
//
// Closure'ın KENDİ parametreleri de izlenir ve gövde kendi kapsamında gezilir
// (visitFuncDecl'in kalıbı). `forEachChild(ClosureExpr)` yalnız gövdeyi verdiği
// için bu olmadan `|pkt: Packet| { ... pkt ... }` içindeki `pkt`, dıştaki aynı
// adlı değişkene çözülüyordu — hem yanlış-pozitif hem de dış değişkenin
// durumunu bozan (hata uzakta çıkan) bir kayıt.
void OwnershipChecker::visitClosureExpr(ClosureExpr *node) {
    pushOwnershipScope();
    for (const auto &p : node->getParams()) {
        trackVariable(p.name, /*isMutable=*/false,
                      p.type ? isCopyType(p.type.get()) : true,
                      p.type ? isDropType(p.type.get()) : false,
                      node->getStartLoc(),
                      p.type ? isClassType(p.type.get()) : false,
                      /*isPatternBinding=*/true);
    }
    visitChildren(node);
    dropScopeVariables();
    popOwnershipScope();
}
void OwnershipChecker::visitTryExpr(TryExpr *node) { visitChildren(node); }
void OwnershipChecker::visitTernaryExpr(TernaryExpr *node) { visitChildren(node); }
void OwnershipChecker::visitAwaitExpr(AwaitExpr *node) { visitChildren(node); }
void OwnershipChecker::visitYieldExpr(YieldExpr *node) { visitChildren(node); }
void OwnershipChecker::visitComptimeExpr(ComptimeExpr *node) { visitChildren(node); }
// Genişletilmemiş bir makronun argüman token'ları henüz AST değil, o yüzden
// forEachChild hiçbir çocuk vermez ve bu no-op'a düşer.
void OwnershipChecker::visitMacroInvokeExpr(MacroInvokeExpr *node) {
    visitChildren(node);
}

// impl metot gövdeleri HİÇ ownership denetimi görmüyordu — top-level'da
// reddedilen çift taşıma burada sessizce derleniyordu.
void OwnershipChecker::visitImplDecl(ImplDecl *node) {
    // `impl Stream<T>`'in T'si blok içindeki tüm metotlarda geçerli.
    pushTypeParams(node->getTypeParams());
    visitChildren(node);
    popTypeParams();
}
// Protokol DEFAULT metot gövdeleri için aynısı. İLİŞKİLİ TİPLER (`type Item`)
// de kapsama itilir: kardeş bildirimler (StructDecl/ImplDecl/ClassDecl) tip
// parametrelerini zaten itiyor ve bir protokol default gövdesindeki `Item`
// tipli yerel, o olmadan çözülmemiş-generik yanlış-pozitifinin (Kök Neden A)
// ikizini üretirdi.
void OwnershipChecker::visitProtocolDecl(ProtocolDecl *node) {
    pushTypeParams(node->getAssociatedTypes());
    visitChildren(node);
    popTypeParams();
}
// StructDecl -> FieldDecl -> computed property getter/setter, willSet/didSet
// ve lazy init gövdeleri.
void OwnershipChecker::visitStructDecl(StructDecl *node) {
    pushTypeParams(node->getTypeParams());
    visitChildren(node);
    popTypeParams();
}
void OwnershipChecker::visitFieldDecl(FieldDecl *node) { visitChildren(node); }

// === Private helpers ===

void OwnershipChecker::trackVariable(const std::string &name, bool isMutable,
                                      bool isCopyType, bool isDropType,
                                      SourceLocation loc, bool isClassType,
                                      bool isPatternBinding) {
    OwnershipInfo info;
    info.name = name;
    info.state = OwnershipState::Owned;
    info.isMutable = isMutable;
    info.isCopyType = isCopyType;
    info.isDropType = isDropType;
    info.isClassType = isClassType;
    info.isPatternBinding = isPatternBinding;
    info.declLocation = loc;

    if (!scopeStack_.empty()) {
        scopeStack_.back()[name] = info;
        allVariables_[name] = &scopeStack_.back()[name];
    }
}

void OwnershipChecker::trackPatternBindings(const Pattern *pattern,
                                            SourceLocation loc) {
    if (!pattern)
        return;

    auto track = [&](const std::string &n) {
        trackVariable(n, /*isMutable=*/false, /*isCopyType=*/true,
                      /*isDropType=*/false, loc, /*isClassType=*/false,
                      /*isPatternBinding=*/true);
    };

    switch (pattern->getKind()) {
    case Pattern::Kind::Identifier:
        // Çıplak bir ad ya bir BAĞLAMA'dır ya da yüksüz bir enum case'i —
        // ayrımı parse zamanında yapılamıyor. İkisinde de izlemek güvenli:
        // Copy olduğu için taşıma tetiklemez, tek etkisi gölgeleme.
        track(static_cast<const IdentifierPattern *>(pattern)->getName());
        break;
    case Pattern::Kind::Binding: {
        auto *b = static_cast<const BindingPattern *>(pattern);
        track(b->getName());
        trackPatternBindings(b->getSub(), loc);
        break;
    }
    case Pattern::Kind::EnumCase:
        for (const auto &sub :
             static_cast<const EnumCasePattern *>(pattern)->getSubpatterns())
            trackPatternBindings(sub.get(), loc);
        break;
    case Pattern::Kind::Tuple:
        for (const auto &el :
             static_cast<const TuplePattern *>(pattern)->getElements())
            trackPatternBindings(el.get(), loc);
        break;
    case Pattern::Kind::Or:
        // Alternatifler bağlama getiremez (Sema: err_pattern_or_binding), ama
        // içlerindeki çıplak adlar enum case'i olarak geçebiliyor — aynı
        // zararsız gölgeleme.
        for (const auto &alt :
             static_cast<const OrPattern *>(pattern)->getAlternatives())
            trackPatternBindings(alt.get(), loc);
        break;
    case Pattern::Kind::Wildcard:
    case Pattern::Kind::IntLiteral:
    case Pattern::Kind::BoolLiteral:
    case Pattern::Kind::StringLiteral:
    case Pattern::Kind::FloatLiteral:
    case Pattern::Kind::Range:
        // Ad bağlamazlar.
        break;
    }
}

void OwnershipChecker::markMoved(const std::string &name, SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return;

    if (info->isCopyType)
        return; // Copy types don't move

    if (info->state == OwnershipState::Moved) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_double_move, name);
        diag_.report(info->lastMoveLocation, DiagID::note_moved_here, name);
        return;
    }

    if (info->borrowCount > 0 || info->hasMutableBorrow) {
        diag_.report(loc, DiagID::err_move_while_borrowed, name);
        return;
    }

    info->state = OwnershipState::Moved;
    info->lastMoveLocation = loc;
}

bool OwnershipChecker::checkUse(const std::string &name, SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true; // Unknown variable, let TypeChecker handle it

    if (info->state == OwnershipState::Moved) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_use_after_move, name);
        diag_.report(info->lastMoveLocation, DiagID::note_moved_here, name);
        diag_.report(loc, DiagID::note_consider_ref, name);
        return false;
    }

    if (info->state == OwnershipState::Dropped) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_use_after_move, name);
        return false;
    }

    return true;
}

bool OwnershipChecker::checkMutation(const std::string &name, SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true; // Not tracked (e.g. global, field, or non-owned) — allow silently

    if (!info->isMutable) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_assign_to_immutable, name);
        // Yardım metni bağlama göre seçilir. Gezinti açıldığından beri bu
        // tanı `self` ve kalıp/iterasyon bağlamalarında da çıkıyor; ikisinde
        // de "declare with 'var' instead of 'let'" UYGULANAMAZ bir öneri —
        // ne `self`'in bir `let` bildirimi var, ne de `for var a in arr`
        // diye bir sözdizimi. Yeni DiagID eklenmiyor, yalnız metin değişiyor.
        if (name == "self") {
            diag_.reportHelp(
                loc, static_cast<uint32_t>(name.size()),
                "declare the method's receiver as 'ref mut self' to mutate it",
                "", DiagID::note_use_var_for_mutable);
        } else if (info->isPatternBinding) {
            diag_.reportHelp(
                loc, static_cast<uint32_t>(name.size()),
                "this binding has no 'var' form; mutate the underlying value "
                "instead",
                "", DiagID::note_use_var_for_mutable);
        } else {
            diag_.reportHelp(
                loc, static_cast<uint32_t>(name.size()),
                "declare with 'var' instead of 'let' to make it mutable", "",
                DiagID::note_use_var_for_mutable);
        }
        return false;
    }

    return true;
}

bool OwnershipChecker::addBorrow(const std::string &name, bool isMutable,
                                  SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true; // Not tracked (e.g. global, field, or non-owned) — allow silently

    if (info->state == OwnershipState::Moved) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_use_after_move, name);
        return false;
    }

    if (isMutable) {
        // Mutable borrow: no other borrows allowed
        if (info->borrowCount > 0 || info->hasMutableBorrow) {
            diag_.report(loc, DiagID::err_mut_borrow_conflict, name);
            if (info->lastBorrowLocation.isValid()) {
                diag_.report(info->lastBorrowLocation, DiagID::note_borrowed_here, name);
            }
            return false;
        }
        info->hasMutableBorrow = true;
        info->state = OwnershipState::BorrowedMutable;
    } else {
        // Immutable borrow: no mutable borrow allowed
        if (info->hasMutableBorrow) {
            diag_.report(loc, DiagID::err_immut_borrow_conflict, name);
            return false;
        }
        info->borrowCount++;
        info->state = OwnershipState::BorrowedImmutable;
    }

    info->lastBorrowLocation = loc;
    return true;
}

void OwnershipChecker::releaseBorrows(const std::string &name) {
    auto *info = getInfo(name);
    if (!info)
        return;

    info->borrowCount = 0;
    info->hasMutableBorrow = false;
    if (info->state == OwnershipState::BorrowedImmutable ||
        info->state == OwnershipState::BorrowedMutable) {
        info->state = OwnershipState::Owned;
    }
}

void OwnershipChecker::releaseBorrow(const std::string &name, bool isMutable) {
    auto *info = getInfo(name);
    if (!info)
        return;

    if (isMutable)
        info->hasMutableBorrow = false;
    else if (info->borrowCount > 0)
        info->borrowCount--;

    // addBorrow never lets a mutable and an immutable borrow coexist, so the
    // variable is unborrowed exactly when both counters are clear. Any other
    // state (Moved, Dropped) is left alone — this only undoes a borrow.
    if (!info->hasMutableBorrow && info->borrowCount == 0 &&
        (info->state == OwnershipState::BorrowedImmutable ||
         info->state == OwnershipState::BorrowedMutable)) {
        info->state = OwnershipState::Owned;
    }
}

bool OwnershipChecker::isCopyType(const TypeRepr *type) const {
    if (!type)
        return true;

    // Primitives (i8-u64, f32, f64, bool, string) are Copy — but not void
    if (type->isPrimitive() && !type->isVoid())
        return true;

    // NamedTypeRepr("String") — parser sometimes creates this instead of Kind::String
    if (type->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(type);
        const auto &n = named->getName();
        if (n == "String" || n == "Map" || n == "Set")
            return true;
        // Classes are reference types — passing a class value shares the
        // reference rather than consuming it, so treat them as Copy.
        if (classNames_.count(n))
            return true;
        // Çözülmemiş generik tip parametresi (`impl Stream<T>`'nin T'si).
        // Monomorfizasyondan ÖNCE T'nin Copy olup olmadığı BİLİNMİYOR ve
        // burada Copy-olmayan varsaymak, somut tiple sorunsuz derlenen
        // generik gövdeyi reddediyordu: `let v: T = ...; pred(v);
        // result.push(v)` -> "use of moved value 'v'". Aynı kod `T` yerine
        // `i64` ile temiz geçtiğine göre ret ilkeli bir generik disiplininden
        // değil, T'nin kullanıcı struct'ı sanılmasından geliyordu.
        // Muhafazakâr yön TAŞIMAMAK: çözülmemiş bir tip parametresi taşıma
        // tetiklemez. Somut tipler etkilenmez — bu dal yalnız kapsamdaki tip
        // parametresi ADLARI için çalışır.
        if (isTypeParamInScope(n))
            return true;
    }

    // Arrays, Tuples, and Function types are Copy
    auto k = type->getKind();
    if (k == TypeRepr::Kind::Array || k == TypeRepr::Kind::Tuple ||
        k == TypeRepr::Kind::Function)
        return true;

    // Optional<T>: Copy UNLESS its payload is a Drop-conforming struct.
    // Optional<Drop-struct> owns at most one Drop value (or none, if nil) —
    // treating it as Copy would let `let b = a` / call-arg passing / if-let
    // silently duplicate ownership of that payload (double-drop risk). All
    // other Optionals (primitives, plain structs, arrays, ...) are unchanged
    // (still Copy) — conservative scope, spec point 5.
    if (k == TypeRepr::Kind::Optional) {
        auto *opt = static_cast<const OptionalTypeRepr *>(type);
        if (isDropType(opt->getInner()))
            return false;
        return true;
    }

    return false;
}

bool OwnershipChecker::isClassType(const TypeRepr *type) const {
    if (!type || type->getKind() != TypeRepr::Kind::Named)
        return false;
    auto *named = static_cast<const NamedTypeRepr *>(type);
    return classNames_.count(named->getName()) != 0;
}

bool OwnershipChecker::isTypeParamInScope(const std::string &name) const {
    for (const auto &scope : typeParamScopes_) {
        for (const auto &p : scope) {
            if (p == name)
                return true;
        }
    }
    return false;
}

void OwnershipChecker::pushTypeParams(const std::vector<std::string> &params) {
    // Boş liste de itilir: pop/push dengesi bildirim düzeyiyle birebir olsun.
    typeParamScopes_.push_back(params);
}

void OwnershipChecker::popTypeParams() {
    // Sözleşme: her pop'un bir push'u var. Boş yığını sessizce tolere etmek
    // dengesizliği GİZLERDİ — ve dengesizlik burada yanlış bir Copy kararına,
    // yani kaybolan bir tanıya dönüşür.
    assert(!typeParamScopes_.empty() &&
           "popTypeParams without a matching pushTypeParams");
    // Release'te (NDEBUG) assert düşer; boş konteynerde pop_back() UB olurdu.
    // Debug'da hâlâ gürültülü, Release'te güvenli.
    if (typeParamScopes_.empty())
        return;
    typeParamScopes_.pop_back();
}

void OwnershipChecker::collectFuncDecls(TranslationUnit &tu) {
    funcsByName_.clear();
    auto record = [&](const FuncDecl *fn) {
        if (fn)
            funcsByName_[fn->getName()].push_back(fn);
    };
    for (auto &d : tu.getDeclarations()) {
        switch (d->getKind()) {
        case ASTNode::NodeKind::FuncDecl:
            record(static_cast<const FuncDecl *>(d.get()));
            break;
        case ASTNode::NodeKind::ImplDecl:
            for (const auto &m : static_cast<const ImplDecl *>(d.get())->getMethods())
                record(m.get());
            break;
        case ASTNode::NodeKind::ProtocolDecl:
            for (const auto &m :
                 static_cast<const ProtocolDecl *>(d.get())->getMethods())
                record(m.get());
            break;
        case ASTNode::NodeKind::ClassDecl:
            for (const auto &m : static_cast<const ClassDecl *>(d.get())->getMembers())
                record(m.method.get());
            break;
        default:
            break;
        }
    }
}

bool OwnershipChecker::paramIsDynProtocol(const std::string &name,
                                          bool isMemberCall,
                                          size_t argIndex) const {
    // Gevşetme YALNIZ SERBEST çağrılara uygulanır. Aday eleme ad + `self`'li
    // olma üzerindendi, ama `arr.push(p)` gibi BUILTIN üye çağrılarının TU'da
    // hiçbir bildirimi yok — buna rağmen aynı adı taşıyan alakasız bir
    // kullanıcı metodu (`impl Sink { func push(ref mut self, g: dyn Greeter) }`)
    // tek aday olarak eşleşiyor, "tüm adaylar hemfikir" sağlanıyor ve taşıma
    // tanısı sessizce siliniyordu: aynı program `impl Sink` bloğu olmadan
    // reddedilirken, blok eklenince kabul ediliyordu. Ölçülen gerçek dyn
    // kullanımlarının HEPSİ serbest fonksiyon (stdlib'de hiç `dyn` parametre
    // yok; `examples/`teki iki tanesi de serbest), yani üye çağrısında
    // muhafazakâr kalmanın tek riski bugün var olmayan bir tanı fazlalığı.
    if (isMemberCall)
        return false;

    auto it = funcsByName_.find(name);
    if (it == funcsByName_.end())
        return false;

    bool sawCandidate = false;
    for (const FuncDecl *fn : it->second) {
        const auto &params = fn->getParams();
        bool hasSelf = !params.empty() && params[0].isSelf;
        // ÇAĞRI BİÇİMİ eşleşmesi. Ad eşleşmesi tek başına yetmez: `arr.push(p)`
        // bir ÜYE çağrısı, `func push(g: dyn Greeter)` ise serbest bir
        // fonksiyon — aralarında hiçbir ilişki yok. Bu eleme olmadan TU'ya
        // eklenen alakasız bir serbest fonksiyon, aynı adı taşıyan tüm üye
        // çağrılarında tanıyı sessizce düşürüyordu.
        if (isMemberCall != hasSelf)
            continue;
        // `self` params_[0]'da duruyor ve argüman listesinde karşılığı yok.
        size_t idx = argIndex + (hasSelf ? 1 : 0);
        if (idx >= params.size())
            continue; // farklı arite — bu çağrının adayı olamaz
        sawCandidate = true;
        const TypeRepr *t = params[idx].type.get();
        if (!t || t->getKind() != TypeRepr::Kind::DynProtocol)
            return false; // adaylardan biri dyn değil -> muhafazakâr: taşıma
    }
    return sawCandidate;
}

bool OwnershipChecker::isDropType(const TypeRepr *type) const {
    if (!type)
        return false;

    // Optional<T>: inherits Drop-ness from its payload (see isCopyType
    // above) — this is what lets `let b = a` / if-let / call-arg passing on
    // an Optional<Drop-struct> variable participate in move tracking.
    if (type->getKind() == TypeRepr::Kind::Optional) {
        auto *opt = static_cast<const OptionalTypeRepr *>(type);
        return isDropType(opt->getInner());
    }

    // Only NAMED struct types can conform to Drop.
    if (type->getKind() != TypeRepr::Kind::Named)
        return false;

    auto *named = static_cast<const NamedTypeRepr *>(type);
    return dropTypeNames_.count(named->getName()) > 0;
}

void OwnershipChecker::dropScopeVariables() {
    if (scopeStack_.empty())
        return;

    for (auto &[name, info] : scopeStack_.back()) {
        if (info.state == OwnershipState::Owned && !info.isCopyType) {
            // Drop the value (in codegen, this would insert drop calls)
            info.state = OwnershipState::Dropped;
        }
        // Release borrows taken ON this variable — it is going away, so
        // precision does not matter and the blanket reset is fine.
        releaseBorrows(name);
        // A reference binding also HOLDS a borrow, recorded on its referent
        // and therefore on a variable that usually outlives this scope. That
        // one was never given back: `{ let r = ref k }` left k borrowed for
        // good, so mutating k after the block stayed an error forever. Undo
        // exactly the one borrow this binding took, for the same reason the
        // call-argument release is precise — the referent may still be
        // borrowed by something else.
        if (info.isRefBinding && !info.borrowsName.empty())
            releaseBorrow(info.borrowsName, info.borrowsMutable);
        // Remove from flat lookup
        allVariables_.erase(name);
    }
}

void OwnershipChecker::pushOwnershipScope() {
    scopeStack_.emplace_back();
}

void OwnershipChecker::popOwnershipScope() {
    if (!scopeStack_.empty())
        scopeStack_.pop_back();
}

OwnershipInfo *OwnershipChecker::getInfo(const std::string &name) {
    auto it = allVariables_.find(name);
    if (it != allVariables_.end())
        return it->second;
    return nullptr;
}

} // namespace liva
