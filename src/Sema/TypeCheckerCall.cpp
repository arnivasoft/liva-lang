#include "liva/Sema/TypeChecker.h"

namespace liva {

void TypeChecker::propagateClosureParamTypes(CallExpr *node) {
    // Propagate function param types to untyped closure args
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
        auto *sym = scopes_.lookup(ident->getName());
        const std::vector<ParamDecl> *formalParams = nullptr;
        if (sym && sym->funcDecl)
            formalParams = &sym->funcDecl->getParams();
        if (formalParams) {
            for (size_t i = 0; i < node->getArgs().size() && i < formalParams->size(); ++i) {
                if (node->getArgs()[i]->getKind() == ASTNode::NodeKind::ClosureExpr &&
                    (*formalParams)[i].type &&
                    (*formalParams)[i].type->getKind() == TypeRepr::Kind::Function) {
                    auto *ft = static_cast<const FunctionTypeRepr *>((*formalParams)[i].type.get());
                    auto *closure = static_cast<ClosureExpr *>(node->getArgs()[i].get());
                    for (size_t j = 0; j < closure->getParams().size() && j < ft->getParams().size(); ++j) {
                        if (!closure->getParams()[j].type) {
                            closure->setParamType(j, cloneTypeRepr(ft->getParams()[j].get()));
                        }
                    }
                }
            }
        }
    }
}

void TypeChecker::propagateDynArrayClosureTypes(CallExpr *node) {
    // Propagate DynArray element type to closure params for map/filter/forEach
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
        const std::string &methodName = memberExpr->getMember();
        if ((methodName == "map" || methodName == "filter" || methodName == "forEach") &&
            !node->getArgs().empty() &&
            node->getArgs()[0]->getKind() == ASTNode::NodeKind::ClosureExpr) {
            // Look up array element type
            if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto *sym = scopes_.lookup(ident->getName());
                if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
                    auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
                    if (arrType->isDynamic() && arrType->getElement()) {
                        auto *closure = static_cast<ClosureExpr *>(node->getArgs()[0].get());
                        if (!closure->getParams().empty() && !closure->getParams()[0].type) {
                            closure->setParamType(0, cloneTypeRepr(arrType->getElement()));
                        }
                    }
                }
            }
        }
        // reduce(init, |acc, x| -> T { ... }) — set x param type from element type
        if (methodName == "reduce" && node->getArgs().size() >= 2 &&
            node->getArgs()[1]->getKind() == ASTNode::NodeKind::ClosureExpr) {
            if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto *sym = scopes_.lookup(ident->getName());
                if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
                    auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
                    if (arrType->isDynamic() && arrType->getElement()) {
                        auto *closure = static_cast<ClosureExpr *>(node->getArgs()[1].get());
                        // param 1 (x) = element type
                        if (closure->getParams().size() >= 2 && !closure->getParams()[1].type) {
                            closure->setParamType(1, cloneTypeRepr(arrType->getElement()));
                        }
                    }
                }
            }
        }
    }
}

void TypeChecker::checkCallArgCount(CallExpr *node) {
    // Check argument count for user-defined functions / class constructors
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *identChk = static_cast<IdentifierExpr *>(node->getCallee());
        auto *symChk = scopes_.lookup(identChk->getName());
        // Class constructor call: ClassName(args) → overload resolution on arg count
        if (symChk && symChk->kind == Symbol::Kind::ClassType && symChk->classDecl) {
            auto inits = symChk->classDecl->getInits();
            size_t actualArgs = node->getArgs().size();
            const FuncDecl *matchedInit = nullptr;
            if (!inits.empty()) {
                // Find init matching actual arg count (respecting defaults)
                for (auto *it : inits) {
                    size_t minReq = 0, maxP = 0;
                    for (auto &p : it->getParams()) {
                        if (p.isSelf) continue;
                        maxP++;
                        if (!p.hasDefault()) minReq++;
                    }
                    if (actualArgs >= minReq && actualArgs <= maxP) {
                        matchedInit = it;
                        break;
                    }
                }
                if (!matchedInit) {
                    // Report with first init's signature
                    auto *first = inits.front();
                    size_t minReq = 0;
                    for (auto &p : first->getParams()) {
                        if (p.isSelf) continue;
                        if (!p.hasDefault()) minReq++;
                    }
                    diag_.report(node->getStartLoc(), DiagID::err_wrong_arg_count,
                                 identChk->getName(),
                                 std::to_string(minReq),
                                 std::to_string(actualArgs));
                }
            }
            // Set result type: NamedTypeRepr(className), wrapped in Optional if failable
            auto classType = std::make_unique<NamedTypeRepr>(symChk->classDecl->getName());
            if (symChk->classDecl->hasFailableInit()) {
                auto optType = std::make_unique<OptionalTypeRepr>(std::move(classType));
                node->setResolvedType(std::move(optType));
            } else {
                node->setResolvedType(std::move(classType));
            }
        }
        if (symChk && symChk->funcDecl) {
            const auto &params = symChk->funcDecl->getParams();
            size_t actualArgs = node->getArgs().size();
            // Count required params (no default value, not variadic, not self)
            size_t requiredParams = 0;
            size_t maxParams = 0;
            bool hasVariadic = false;
            for (const auto &p : params) {
                if (p.isSelf) continue;
                maxParams++;
                if (p.isVariadic) { hasVariadic = true; continue; }
                if (!p.hasDefault()) requiredParams++;
            }
            if (!hasVariadic && (actualArgs < requiredParams || actualArgs > maxParams)) {
                diag_.report(node->getStartLoc(), DiagID::err_wrong_arg_count,
                             identChk->getName(),
                             std::to_string(requiredParams == maxParams
                                            ? requiredParams
                                            : requiredParams),
                             std::to_string(actualArgs));
            }
        }
    }
}

void TypeChecker::resolveCallReturnType(CallExpr *node) {
    // Try to resolve return type from callee
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());

        if (ident->getName() == "readLine") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "format") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "len") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "toString" || ident->getName() == "charToString") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "abs" || ident->getName() == "min" ||
                   ident->getName() == "max") {
            // abs/min/max return the same type as their first argument
            if (!node->getArgs().empty() && node->getArgs()[0]->getResolvedType()) {
                node->setResolvedType(
                    makePrimitiveType(node->getArgs()[0]->getResolvedType()->getKind()));
            }
        } else if (ident->getName() == "sqrt" || ident->getName() == "pow" ||
                   ident->getName() == "floor" || ident->getName() == "ceil" ||
                   ident->getName() == "log" || ident->getName() == "log10" ||
                   ident->getName() == "sin" || ident->getName() == "cos" ||
                   ident->getName() == "tan" || ident->getName() == "round") {
            // sqrt/pow/floor/ceil/log/log10/sin/cos/tan/round always return f64
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "parseInt") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeI32Type());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "parseInt64") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeI64Type());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "parseFloat") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeF64Type());
            node->setResolvedType(std::move(optType));
        // Stdlib: Random
        } else if (ident->getName() == "randInt") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "randFloat") {
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "randSeed") {
            // void — no resolved type needed
        } else if (ident->getName() == "randI64") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "randUuid" ||
                   ident->getName() == "randUuidV7") {
            node->setResolvedType(makeStringType());
        // Stdlib: Process/Env
        } else if (ident->getName() == "env") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "exit") {
            // void — no resolved type needed
        } else if (ident->getName() == "args") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        // Stdlib: Date/Time
        } else if (ident->getName() == "clock") {
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "clockMs") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "sleep") {
            // void — no resolved type needed
        // Stdlib: Benchmarking
        } else if (ident->getName() == "benchStart") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "benchIter" || ident->getName() == "benchDone") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "benchReport" || ident->getName() == "benchReset") {
            // void — no resolved type needed
        } else if (ident->getName() == "isCancelled") {
            if (!currentIsAsync_) {
                diag_.report(node->getStartLoc(), DiagID::err_is_cancelled_outside_async);
            }
            node->setResolvedType(makeBoolType());
        // Stdlib: Regex
        } else if (ident->getName() == "regexMatch") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "regexFind") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "regexFindAll" || ident->getName() == "regexSplit") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        } else if (ident->getName() == "regexReplace") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "regexFindGroups") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        } else if (ident->getName() == "regexCompile") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "regexTest") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "regexExec") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "regexExecGroups") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        } else if (ident->getName() == "regexReplaceCompiled") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "regexFree") {
            // void
        // Stdlib: Networking
        } else if (ident->getName() == "httpRequestEx") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "httpRawHeaders") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "httpHeaderLookup") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "httpStatus") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "httpBody") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "httpClose") {
            // void
        // Stdlib: WebSocket
        } else if (ident->getName() == "wsSend") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "wsClose") {
            // void
        } else if (ident->getName() == "wsIsOpen") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "wsConnectEx") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "wsRecvKind") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "wsMsgText") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "wsMsgBytes") {
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            node->setResolvedType(std::make_unique<ArrayTypeRepr>(std::move(u8), -1));
        } else if (ident->getName() == "wsSendBinary") {
            node->setResolvedType(makeBoolType());
        // Stdlib: SQLite
        } else if (ident->getName() == "sqliteOpen") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "sqliteClose") {
            // void
        } else if (ident->getName() == "sqliteExec") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "sqliteQueryFirst") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "sqliteQueryInt") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeI64Type());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "sqliteQueryColumn") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "sqliteLastInsertRowid") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "sqliteChanges") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "sqliteErrmsg") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "sqlitePrepare") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "sqliteBindText" ||
                   ident->getName() == "sqliteBindInt" ||
                   ident->getName() == "sqliteBindDouble" ||
                   ident->getName() == "sqliteBindNull" ||
                   ident->getName() == "sqliteStep" ||
                   ident->getName() == "sqliteReset" ||
                   ident->getName() == "sqliteBindByName") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "sqliteColumnCount") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "sqliteColumnText") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "sqliteColumnInt") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "sqliteColumnDouble") {
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "sqliteColumnName") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "sqliteColumnType") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "sqliteColumnIsNull") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "sqliteBindBlob") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "sqliteColumnBlob") {
            // [u8]
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            node->setResolvedType(std::make_unique<ArrayTypeRepr>(std::move(u8), -1));
        } else if (ident->getName() == "sqliteFinalize") {
            // void
        // Stdlib: PostgreSQL
        } else if (ident->getName() == "pgConnect") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "pgClose" || ident->getName() == "pgClear") {
            // void
        } else if (ident->getName() == "pgExec") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "pgErrmsg" ||
                   ident->getName() == "pgNormalizeParams" ||
                   ident->getName() == "pgResultText" ||
                   ident->getName() == "pgColumnName") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "pgQuery" ||
                   ident->getName() == "pgQueryParams") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "pgResultRows" ||
                   ident->getName() == "pgResultCols") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "pgResultIsNull") {
            node->setResolvedType(makeBoolType());
        // Stdlib: Directory operations
        } else if (ident->getName() == "dirList") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        } else if (ident->getName() == "dirCreate" || ident->getName() == "dirRemove" ||
                   ident->getName() == "dirExists" || ident->getName() == "pathExists" ||
                   ident->getName() == "isFile" || ident->getName() == "isDir") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "fileSize" || ident->getName() == "fileModifiedTime") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "pathJoin" || ident->getName() == "pathDirname" ||
                   ident->getName() == "pathBasename" || ident->getName() == "pathExtension" ||
                   ident->getName() == "pathAbsolute") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "fileRead") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "fileWrite" || ident->getName() == "fileAppend" ||
                   ident->getName() == "fileRemove" || ident->getName() == "fileCopy") {
            node->setResolvedType(makeBoolType());
        // Stdlib: Subprocess
        } else if (ident->getName() == "exec" || ident->getName() == "processWait") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "execOutput" || ident->getName() == "processRead") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "processStart") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "processKill") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "processClose") {
            // void — no resolved type needed
        // Stdlib: TOML
        } else if (ident->getName() == "tomlParse") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "tomlGetString") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "tomlGetInt") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeI64Type());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "tomlGetBool") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeBoolType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "tomlHasKey") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "tomlFree") {
            // void
        // Stdlib: JSON
        } else if (ident->getName() == "jsonObjKeys") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        // Stdlib: JSON DOM (parse-tree)
        } else if (ident->getName() == "jsonParse" || ident->getName() == "jsonRoot" ||
                   ident->getName() == "jsonNodeAsInt") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "jsonNodeKind") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "jsonNodeAsFloat") {
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "jsonNodeAsBool") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "jsonNodeAsString" || ident->getName() == "jsonToString" ||
                   ident->getName() == "jsonToStringPretty") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "jsonFreeDoc") {
            // void — no resolved type needed
        } else if (ident->getName() == "jsonObjGet") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "jsonObjHas") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "jsonObjCount") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "jsonArrAt") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "jsonArrCount") {
            node->setResolvedType(makeI32Type());
        // Stdlib: JSON DOM Building / Mutation
        } else if (ident->getName() == "jsonPathGet") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "jsonNewObject" || ident->getName() == "jsonNewArray" ||
                   ident->getName() == "jsonObjSetObject" || ident->getName() == "jsonObjSetArray" ||
                   ident->getName() == "jsonArrAddObject" || ident->getName() == "jsonArrAddArray") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "jsonObjSetString" || ident->getName() == "jsonObjSetInt" ||
                   ident->getName() == "jsonObjSetFloat" || ident->getName() == "jsonObjSetBool" ||
                   ident->getName() == "jsonObjSetNull" || ident->getName() == "jsonObjRemove" ||
                   ident->getName() == "jsonArrAddString" || ident->getName() == "jsonArrAddInt" ||
                   ident->getName() == "jsonArrAddFloat" || ident->getName() == "jsonArrAddBool" ||
                   ident->getName() == "jsonArrAddNull" ||
                   ident->getName() == "jsonPathSetString" || ident->getName() == "jsonPathSetInt" ||
                   ident->getName() == "jsonPathSetFloat" || ident->getName() == "jsonPathSetBool") {
            // void — no resolved type needed
        // Stdlib: Logging (all void)
        } else if (ident->getName() == "logDebug" || ident->getName() == "logInfo" ||
                   ident->getName() == "logWarn" || ident->getName() == "logError" ||
                   ident->getName() == "logSetLevel") {
            // void — no resolved type needed
        // Stdlib: Testing (all void — they abort on failure)
        } else if (ident->getName() == "assert" || ident->getName() == "assertMsg" ||
                   ident->getName() == "assertEq" || ident->getName() == "assertEqStr" ||
                   ident->getName() == "assertEqFloat") {
            // void — no resolved type needed
        } else if (ident->getName() == "testRunClosure") {
            node->setResolvedType(makeBoolType());
        // Stdlib: DateTime
        } else if (ident->getName() == "dateNow" || ident->getName() == "timeNow" ||
                   ident->getName() == "datetimeNow") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "dateFormat") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "dateYear" || ident->getName() == "dateMonth" ||
                   ident->getName() == "dateDay" || ident->getName() == "dateWeekday" ||
                   ident->getName() == "dateHour" || ident->getName() == "dateMinute" ||
                   ident->getName() == "dateSecond") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "dateTimestamp" || ident->getName() == "dateParse" ||
                   ident->getName() == "dateAdd" || ident->getName() == "dateDiff") {
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "isoFormatUtc") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "isoParse") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeF64Type());
            node->setResolvedType(std::move(optType));
        // Stdlib: Encoding/Compression
        } else if (ident->getName() == "base64Encode" || ident->getName() == "hexEncode" ||
                   ident->getName() == "urlEncode" || ident->getName() == "base64UrlEncode") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "base64Decode" || ident->getName() == "hexDecode" ||
                   ident->getName() == "urlDecode" || ident->getName() == "base64UrlDecode") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "bytesToStr" ||
                   ident->getName() == "hexEncodeBytes" ||
                   ident->getName() == "base64UrlEncodeBytes") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "strToBytes") {
            // [u8]
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            node->setResolvedType(std::make_unique<ArrayTypeRepr>(std::move(u8), -1));
        } else if (ident->getName() == "hexDecodeBytes" ||
                   ident->getName() == "base64UrlDecodeBytes") {
            // [u8]?
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            auto arr = std::make_unique<ArrayTypeRepr>(std::move(u8), -1);
            node->setResolvedType(std::make_unique<OptionalTypeRepr>(std::move(arr)));
        } else if (ident->getName() == "gzipEncode") {
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            node->setResolvedType(std::make_unique<ArrayTypeRepr>(std::move(u8), -1));
        } else if (ident->getName() == "gzipDecode") {
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            auto arr = std::make_unique<ArrayTypeRepr>(std::move(u8), -1);
            node->setResolvedType(std::make_unique<OptionalTypeRepr>(std::move(arr)));
        } else if (ident->getName() == "urlScheme" || ident->getName() == "urlHost" ||
                   ident->getName() == "urlPath" || ident->getName() == "urlQuery" ||
                   ident->getName() == "urlFragment") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "urlPort") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "crc32") {
            node->setResolvedType(makeI64Type());
        // Stdlib: Crypto
        } else if (ident->getName() == "sha256" || ident->getName() == "md5" ||
                   ident->getName() == "hmacSha256" ||
                   ident->getName() == "sha1" || ident->getName() == "sha512" ||
                   ident->getName() == "hmacSha1" || ident->getName() == "hmacSha512" ||
                   ident->getName() == "jwtHS256Sig" || ident->getName() == "jwtHS512Sig") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "constTimeEq") {
            node->setResolvedType(makeBoolType());
        // Stdlib: Synchronization
        } else if (ident->getName() == "mutexCreate" ||
                   ident->getName() == "atomicCreate") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "mutexTryLock" ||
                   ident->getName() == "atomicCas") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "atomicLoad" ||
                   ident->getName() == "atomicAdd" ||
                   ident->getName() == "atomicSub") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "mutexLock" ||
                   ident->getName() == "mutexUnlock" ||
                   ident->getName() == "mutexFree" ||
                   ident->getName() == "atomicStore" ||
                   ident->getName() == "atomicFree") {
            // void — no resolved type
        // Stdlib: RWLock + ConditionVariable
        } else if (ident->getName() == "rwlockCreate" ||
                   ident->getName() == "condVarCreate") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "rwlockTryReadLock" ||
                   ident->getName() == "rwlockTryWriteLock") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "rwlockReadLock" ||
                   ident->getName() == "rwlockReadUnlock" ||
                   ident->getName() == "rwlockWriteLock" ||
                   ident->getName() == "rwlockWriteUnlock" ||
                   ident->getName() == "rwlockFree" ||
                   ident->getName() == "condVarWait" ||
                   ident->getName() == "condVarNotifyOne" ||
                   ident->getName() == "condVarNotifyAll" ||
                   ident->getName() == "condVarFree") {
            // void — no resolved type
        // Stdlib: Channel
        } else if (ident->getName() == "channelCreate" ||
                   ident->getName() == "channelLen") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "channelReceive" ||
                   ident->getName() == "channelTryReceive") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeI64Type());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "channelTrySend") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "channelSend" ||
                   ident->getName() == "channelClose" ||
                   ident->getName() == "channelFree") {
            // void — no resolved type
        // Stdlib: TaskGroup
        } else if (ident->getName() == "taskGroupCreate" ||
                   ident->getName() == "taskGroupCount") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "taskGroupSpawn" ||
                   ident->getName() == "taskGroupAwaitAll" ||
                   ident->getName() == "taskGroupCancelAll" ||
                   ident->getName() == "taskGroupFree") {
            // void — no resolved type
        // Stdlib: Task Select & WithTimeout
        } else if (ident->getName() == "taskSelect") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "withTimeout") {
            node->setResolvedType(makeBoolType());
        // Stdlib: Task control (single-task)
        } else if (ident->getName() == "taskIsDone" ||
                   ident->getName() == "taskIsCancelled") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "taskCancel") {
            // void — no resolved type
        // Stdlib: Thread Pool Scheduler
        } else if (ident->getName() == "schedulerWorkerCount") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "schedulerInit" ||
                   ident->getName() == "schedulerShutdown") {
            // void — no resolved type
        // Stdlib: Async I/O
        } else if (ident->getName() == "asyncFileRead") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "asyncFileWrite") {
            node->setResolvedType(makeBoolType());
        // Stdlib: Collections utility functions
        } else if (ident->getName() == "sorted" || ident->getName() == "reversed" ||
                   ident->getName() == "flatten") {
            // Returns same-type array as input (or generic [T])
            if (!node->getArgs().empty() && node->getArgs()[0]->getResolvedType()) {
                node->setResolvedType(cloneTypeRepr(node->getArgs()[0]->getResolvedType()));
            }
        } else if (ident->getName() == "any" || ident->getName() == "all") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "count") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "forEach") {
            // void — no resolved type
        } else if (ident->getName() == "enumerate") {
            // enumerate([T]) -> [(i64, T)] — for now, resolve as dynamic array
            if (!node->getArgs().empty() && node->getArgs()[0]->getResolvedType()) {
                node->setResolvedType(cloneTypeRepr(node->getArgs()[0]->getResolvedType()));
            }
        } else if (ident->getName() == "zip") {
            // zip([A], [B]) -> [(A, B)] — for now, resolve as dynamic array
            if (!node->getArgs().empty() && node->getArgs()[0]->getResolvedType()) {
                node->setResolvedType(cloneTypeRepr(node->getArgs()[0]->getResolvedType()));
            }
        // Stdlib: String utility functions
        } else if (ident->getName() == "strRepeat" || ident->getName() == "strPadLeft" ||
                   ident->getName() == "strPadRight" || ident->getName() == "strJoin" ||
                   ident->getName() == "strTrim" || ident->getName() == "strTrimLeft" ||
                   ident->getName() == "strTrimRight" || ident->getName() == "strReplace" ||
                   ident->getName() == "strToUpper" || ident->getName() == "strToLower" ||
                   ident->getName() == "strReverse") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "strContains" || ident->getName() == "strStartsWith" ||
                   ident->getName() == "strEndsWith") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "strSplit" || ident->getName() == "strChars" ||
                   ident->getName() == "strLines") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        // Stdlib: UTF-8 helpers
        } else if (ident->getName() == "strCharCount") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "strCodepointAt") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "strIsAscii" ||
                   ident->getName() == "charIsAlpha" || ident->getName() == "charIsDigit" ||
                   ident->getName() == "charIsAlnum" || ident->getName() == "charIsSpace" ||
                   ident->getName() == "charIsUpper" || ident->getName() == "charIsLower") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "charToUpper" || ident->getName() == "charToLower") {
            node->setResolvedType(makeI32Type());
        // Stdlib: UI (raylib backend)
        } else if (ident->getName() == "getClipboardText") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "getScreenWidth" || ident->getName() == "getScreenHeight" ||
                   ident->getName() == "getMouseX" || ident->getName() == "getMouseY" ||
                   ident->getName() == "getCharPressed" || ident->getName() == "getKeyPressed" ||
                   ident->getName() == "measureText" || ident->getName() == "measureTextFont" ||
                   ident->getName() == "measureTextWrapped" || ident->getName() == "loadFont" ||
                   ident->getName() == "loadImage" || ident->getName() == "getImageWidth" ||
                   ident->getName() == "getImageHeight") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "getFrameTime" || ident->getName() == "getMouseWheel") {
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "windowShouldClose" ||
                   ident->getName() == "isMousePressed" || ident->getName() == "isMouseReleased" ||
                   ident->getName() == "isMouseDown" ||
                   ident->getName() == "isKeyPressed" || ident->getName() == "isKeyDown") {
            node->setResolvedType(makeBoolType());
        } else {
            auto *sym = scopes_.lookup(ident->getName());
            if (sym && sym->type &&
                sym->type->getKind() == TypeRepr::Kind::Function) {
                auto *ft = static_cast<const FunctionTypeRepr *>(sym->type);
                if (ft->getReturnType() && !ft->getReturnType()->isVoid()) {
                    node->setResolvedType(
                        makePrimitiveType(ft->getReturnType()->getKind()));
                }
            } else if (sym && sym->funcDecl) {
                if (sym->funcDecl->isGeneric()) {
                    // Infer type parameters from argument types
                    std::unordered_map<std::string, const TypeRepr *> typeBindings;
                    const auto &typeParams = sym->funcDecl->getTypeParams();
                    const auto &formalParams = sym->funcDecl->getParams();

                    for (size_t i = 0; i < formalParams.size() && i < node->getArgs().size(); ++i) {
                        const TypeRepr *paramType = formalParams[i].type.get();
                        if (paramType && paramType->getKind() == TypeRepr::Kind::Named) {
                            auto *named = static_cast<const NamedTypeRepr *>(paramType);
                            for (const auto &tp : typeParams) {
                                if (named->getName() == tp) {
                                    const TypeRepr *argType = node->getArgs()[i]->getResolvedType();
                                    if (argType) typeBindings[tp] = argType;
                                    break;
                                }
                            }
                        }
                    }

                    // Check trait bounds
                    const auto &bounds = sym->funcDecl->getTypeParamBounds();
                    for (auto &[pName, boundProtos] : bounds) {
                        auto bindIt = typeBindings.find(pName);
                        if (bindIt == typeBindings.end()) continue;
                        const TypeRepr *concreteType = bindIt->second;
                        std::string concreteName = typeToString(concreteType);
                        for (auto &boundProto : boundProtos) {
                            auto confIt = protocolConformances_.find(boundProto);
                            bool conforms = false;
                            if (confIt != protocolConformances_.end()) {
                                for (const auto &t : confIt->second)
                                    if (t == concreteName) { conforms = true; break; }
                                // [T] (DynArray) implicitly conforms to Iterator
                                if (!conforms && boundProto == "Iterator" && isDynArrayType(concreteType))
                                    conforms = true;
                                // Generic types like Stack<i64> match conformance
                                // entries registered under the bare base name "Stack".
                                if (!conforms && concreteType &&
                                    concreteType->getKind() == TypeRepr::Kind::Generic) {
                                    auto *gt = static_cast<const GenericTypeRepr *>(concreteType);
                                    for (const auto &t : confIt->second)
                                        if (t == gt->getBaseName()) { conforms = true; break; }
                                }
                            }
                            if (!conforms)
                                diag_.report(node->getStartLoc(), DiagID::err_no_conformance, concreteName, boundProto);
                        }
                    }

                    // Check associated type constraints (where T.Item == i32, T.Item: Proto)
                    const auto &whereConstraints = sym->funcDecl->getWhereConstraints();
                    for (auto &wc : whereConstraints) {
                        auto bindIt = typeBindings.find(wc.paramName);
                        if (bindIt == typeBindings.end()) continue;
                        std::string concreteName = typeToString(bindIt->second);

                        // Find the associated type for this concrete type
                        std::string resolvedAssocType;
                        for (auto &[protoName, types] : protocolConformances_) {
                            for (auto &t : types) {
                                if (t == concreteName) {
                                    std::string key = concreteName + "::" + protoName + "::" + wc.assocTypeName;
                                    auto it = implAssociatedTypeResolutions_.find(key);
                                    if (it != implAssociatedTypeResolutions_.end()) {
                                        resolvedAssocType = it->second;
                                        break;
                                    }
                                }
                            }
                            if (!resolvedAssocType.empty()) break;
                        }

                        if (wc.kind == WhereConstraint::Kind::AssociatedTypeEqual) {
                            if (!resolvedAssocType.empty() && resolvedAssocType != wc.equalTypeName) {
                                diag_.report(node->getStartLoc(), DiagID::err_associated_type_mismatch,
                                             wc.paramName, wc.assocTypeName, resolvedAssocType, wc.equalTypeName);
                            }
                        } else if (wc.kind == WhereConstraint::Kind::AssociatedTypeBound) {
                            for (auto &requiredProto : wc.protocolNames) {
                                auto confIt = protocolConformances_.find(requiredProto);
                                bool conforms = false;
                                if (confIt != protocolConformances_.end()) {
                                    for (auto &t : confIt->second)
                                        if (t == resolvedAssocType) { conforms = true; break; }
                                }
                                if (!conforms && !resolvedAssocType.empty()) {
                                    diag_.report(node->getStartLoc(), DiagID::err_associated_type_no_conformance,
                                                 wc.paramName, wc.assocTypeName, resolvedAssocType, requiredProto);
                                }
                            }
                        }
                    }

                    // Resolve return type
                    const TypeRepr *retType = resolveAlias(sym->funcDecl->getReturnType());
                    if (retType && retType->getKind() == TypeRepr::Kind::AssociatedType) {
                        // Resolve T.Item → concrete type
                        auto *assocType = static_cast<const AssociatedTypeRepr *>(retType);
                        auto bindIt2 = typeBindings.find(assocType->getBaseName());
                        if (bindIt2 != typeBindings.end()) {
                            std::string concreteName2 = typeToString(bindIt2->second);
                            for (auto &[protoName2, types2] : protocolConformances_) {
                                for (auto &t2 : types2) {
                                    if (t2 == concreteName2) {
                                        std::string key2 = concreteName2 + "::" + protoName2 + "::" + assocType->getAssocTypeName();
                                        auto resIt2 = implAssociatedTypeResolutions_.find(key2);
                                        if (resIt2 != implAssociatedTypeResolutions_.end()) {
                                            node->setResolvedType(makeNamedType(resIt2->second));
                                            goto assoc_resolved;
                                        }
                                    }
                                }
                            }
                        }
                        assoc_resolved:;
                    } else if (retType && retType->getKind() == TypeRepr::Kind::Named) {
                        auto *named = static_cast<const NamedTypeRepr *>(retType);
                        auto it = typeBindings.find(named->getName());
                        if (it != typeBindings.end()) {
                            node->setResolvedType(makePrimitiveType(it->second->getKind()));
                        } else {
                            node->setResolvedType(makeNamedType(named->getName()));
                        }
                    } else if (retType && !retType->isVoid()) {
                        node->setResolvedType(cloneTypeRepr(retType));
                    }
                } else if (sym->funcDecl->getReturnType()) {
                    const TypeRepr *retType = resolveAlias(sym->funcDecl->getReturnType());
                    node->setResolvedType(cloneTypeRepr(retType));
                }
            }
        }

        // Wrap return type in Task<T> for async function calls
        if (asyncFuncNames_.count(ident->getName()) && node->getResolvedType()) {
            std::vector<std::unique_ptr<TypeRepr>> taskArgs;
            taskArgs.push_back(cloneTypeRepr(node->getResolvedType()));
            node->setResolvedType(std::make_unique<GenericTypeRepr>("Task", std::move(taskArgs)));
        }

        // Wrap return type in Generator<T> for generator function calls.
        // Generator functions auto-detected from `yield` typically have no
        // declared return type, so default the yielded value type to i64
        // (the runtime promise slot uses i64 unless the function explicitly
        // declares a different yielded type).
        auto *genSym = scopes_.lookup(ident->getName());
        if (genSym && genSym->funcDecl && genSym->funcDecl->isGenerator()) {
            std::vector<std::unique_ptr<TypeRepr>> genArgs;
            const TypeRepr *resolved = node->getResolvedType();
            // Treat void-returning generators (no declared return type) the
            // same as no resolved type: default the yielded element to i32.
            // Without this, for-in over `func gen() { yield 42 }` would bind
            // the loop var to void and fail to type-check the body.
            if (resolved && !resolved->isVoid()) {
                // Already wrapped (e.g., Task<T>) — unwrap and re-wrap as Generator<T>.
                if (resolved->getKind() == TypeRepr::Kind::Generic) {
                    auto *gt = static_cast<const GenericTypeRepr *>(resolved);
                    if (gt->getBaseName() == "Task" && !gt->getTypeArgs().empty()) {
                        genArgs.push_back(cloneTypeRepr(gt->getTypeArgs()[0].get()));
                    }
                }
                if (genArgs.empty())
                    genArgs.push_back(cloneTypeRepr(resolved));
            } else {
                genArgs.push_back(makeI32Type());
            }
            node->setResolvedType(std::make_unique<GenericTypeRepr>("Generator", std::move(genArgs)));
        }
    }

}

void TypeChecker::resolveMapSetMethodCall(CallExpr *node) {
    // Map/Set method call resolution: m.insert(), m.get(), m.contains(), m.remove()
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
        const std::string &methodName = memberExpr->getMember();

        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto *sym = scopes_.lookup(ident->getName());
            if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Generic) {
                auto *genType = static_cast<const GenericTypeRepr *>(sym->type);
                if (genType->getBaseName() == "Map") {
                    if (methodName == "get" && genType->getTypeArgs().size() >= 2) {
                        // get() returns Optional<V>
                        auto optType = std::make_unique<OptionalTypeRepr>(
                            cloneTypeRepr(genType->getTypeArgs()[1].get()));
                        node->setResolvedType(std::move(optType));
                    } else if (methodName == "contains" || methodName == "remove") {
                        node->setResolvedType(makeBoolType());
                    }
                    // insert() returns void — no resolved type needed
                } else if (genType->getBaseName() == "Set") {
                    if (methodName == "contains" || methodName == "remove") {
                        node->setResolvedType(makeBoolType());
                    }
                    // insert() returns void
                }
            }

            // DynArray method calls: arr.contains(), arr.indexOf(), arr.reverse()
            if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
                auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
                if (arrType->isDynamic()) {
                    if (methodName == "contains") {
                        node->setResolvedType(makeBoolType());
                    } else if (methodName == "indexOf") {
                        node->setResolvedType(makeI64Type());
                    } else if (methodName == "filter") {
                        // filter returns same type array [T]
                        auto filterArr = std::make_unique<ArrayTypeRepr>(
                            cloneTypeRepr(arrType->getElement()));
                        node->setResolvedType(std::move(filterArr));
                    } else if (methodName == "map" && !node->getArgs().empty()) {
                        // map returns [ClosureReturnType]
                        auto *closureArg = node->getArgs()[0].get();
                        if (closureArg->getKind() == ASTNode::NodeKind::ClosureExpr) {
                            auto *closure = static_cast<ClosureExpr *>(closureArg);
                            if (closure->getReturnType()) {
                                auto mapArr = std::make_unique<ArrayTypeRepr>(
                                    cloneTypeRepr(closure->getReturnType()));
                                node->setResolvedType(std::move(mapArr));
                            }
                        }
                    } else if (methodName == "reduce" && node->getArgs().size() >= 2) {
                        // reduce(init, closure) returns init's type
                        if (node->getArgs()[0]->getResolvedType()) {
                            node->setResolvedType(makePrimitiveType(
                                node->getArgs()[0]->getResolvedType()->getKind()));
                        }
                    }
                    // push(), pop(), reverse(), forEach() return void
                }
            }

            // File.open() static method
            if (ident->getName() == "File" && methodName == "open") {
                auto optType = std::make_unique<OptionalTypeRepr>(makeNamedType("File"));
                node->setResolvedType(std::move(optType));
            }

            // User-defined struct/class static method: TypeName.method(args)
            // Also handles instance method calls: varName.method(args) where
            // varName has a Named struct type — look up TypeName::method.
            if (!node->getResolvedType()) {
                std::string key = ident->getName() + "::" + methodName;
                auto retIt = typeMethodReturnTypes_.find(key);
                if (retIt != typeMethodReturnTypes_.end() && retIt->second) {
                    node->setResolvedType(cloneTypeRepr(retIt->second));
                }
                // Fall back: if the variable has a Named struct type, try StructName::method
                if (!node->getResolvedType() && sym && sym->type &&
                    sym->type->getKind() == TypeRepr::Kind::Named) {
                    auto *namedTy = static_cast<const NamedTypeRepr *>(sym->type);
                    std::string instanceKey = namedTy->getName() + "::" + methodName;
                    auto iRetIt = typeMethodReturnTypes_.find(instanceKey);
                    if (iRetIt != typeMethodReturnTypes_.end() && iRetIt->second) {
                        node->setResolvedType(cloneTypeRepr(iRetIt->second));
                    }
                }
                // Fall back: if the variable has a dyn Protocol type, look up the
                // protocol's method return type (e.g. `db: dyn Database` → Database::query).
                if (!node->getResolvedType() && sym && sym->type &&
                    sym->type->getKind() == TypeRepr::Kind::DynProtocol) {
                    auto *dynTy = static_cast<const DynProtocolTypeRepr *>(sym->type);
                    std::string protoKey = dynTy->getProtocolName() + "::" + methodName;
                    auto pRetIt = typeMethodReturnTypes_.find(protoKey);
                    if (pRetIt != typeMethodReturnTypes_.end() && pRetIt->second) {
                        node->setResolvedType(cloneTypeRepr(pRetIt->second));
                    }
                }
            }

            // File instance methods
            if (fileVariables_.count(ident->getName())) {
                if (methodName == "readLine") {
                    auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
                    node->setResolvedType(std::move(optType));
                } else if (methodName == "readAll") {
                    node->setResolvedType(makeStringType());
                } else if (methodName == "seek") {
                    node->setResolvedType(makeI32Type());
                } else if (methodName == "tell" || methodName == "size") {
                    node->setResolvedType(makeI64Type());
                }
                // write, writeLine, close → void (no resolved type)
            }

            // String method calls: s.contains(), s.startsWith(), etc.
            if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::String) {
                if (methodName == "contains" || methodName == "startsWith" ||
                    methodName == "endsWith") {
                    node->setResolvedType(makeBoolType());
                } else if (methodName == "indexOf") {
                    node->setResolvedType(makeI64Type());
                } else if (methodName == "substring" || methodName == "trim" ||
                           methodName == "toUpper" || methodName == "toLower" ||
                           methodName == "replace") {
                    node->setResolvedType(makeStringType());
                }
                else if (methodName == "split") {
                    // split() returns [string] — dynamic array
                    auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType());
                    node->setResolvedType(std::move(arrType));
                }
            }
        }

        // Chained call: expr.method() where expr has a resolved Named struct type.
        // Handles patterns like stmt.columnDate(i).year() — the inner call's
        // return type was set to a Named type (e.g. Date), and now we need to
        // resolve the outer call's return type from StructName::method.
        if (!node->getResolvedType()) {
            const TypeRepr *objType = memberExpr->getObject()->getResolvedType();
            if (objType && objType->getKind() == TypeRepr::Kind::Named) {
                auto *namedTy = static_cast<const NamedTypeRepr *>(objType);
                std::string chainKey = namedTy->getName() + "::" + methodName;
                auto chainIt = typeMethodReturnTypes_.find(chainKey);
                if (chainIt != typeMethodReturnTypes_.end() && chainIt->second) {
                    node->setResolvedType(cloneTypeRepr(chainIt->second));
                }
            }
        }

        // String method calls on expressions with resolved string type
        if (memberExpr->getObject()->getResolvedType() &&
            memberExpr->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
            if (methodName == "contains" || methodName == "startsWith" ||
                methodName == "endsWith") {
                node->setResolvedType(makeBoolType());
            } else if (methodName == "indexOf") {
                node->setResolvedType(makeI64Type());
            } else if (methodName == "substring" || methodName == "trim" ||
                       methodName == "toUpper" || methodName == "toLower" ||
                       methodName == "replace") {
                node->setResolvedType(makeStringType());
            } else if (methodName == "split") {
                auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType());
                node->setResolvedType(std::move(arrType));
            }
        }

        // P1-8 alt-spec 2: built-in `.hash()` on primitive receivers
        // (i8/i16/i32/i64, u8/u16/u32/u64, string, bool, Char) resolves to i64.
        // Actual call lowering happens at IRGen via runtime liva_hash_* wrappers.
        if (methodName == "hash" && node->getArgs().empty() && !node->getResolvedType()) {
            const TypeRepr *recvType = memberExpr->getObject()->getResolvedType();
            // Fall back to the symbol's type for plain identifier receivers
            // (some primitive-typed identifiers don't get resolvedType set).
            if (!recvType &&
                memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto *sym = scopes_.lookup(ident->getName());
                if (sym && sym->type) recvType = sym->type;
            }
            if (recvType) {
                auto k = recvType->getKind();
                bool isPrim = (k == TypeRepr::Kind::I8 || k == TypeRepr::Kind::I16 ||
                               k == TypeRepr::Kind::I32 || k == TypeRepr::Kind::I64 ||
                               k == TypeRepr::Kind::U8 || k == TypeRepr::Kind::U16 ||
                               k == TypeRepr::Kind::U32 || k == TypeRepr::Kind::U64 ||
                               k == TypeRepr::Kind::String ||
                               k == TypeRepr::Kind::Bool);
                // Char is represented as NamedTypeRepr("Char") (no dedicated Kind).
                if (!isPrim && k == TypeRepr::Kind::Named) {
                    auto *nt = static_cast<const NamedTypeRepr *>(recvType);
                    if (nt->getName() == "Char") isPrim = true;
                }
                if (isPrim) {
                    node->setResolvedType(makeI64Type());
                }
            }
        }
    }
}

} // namespace liva
