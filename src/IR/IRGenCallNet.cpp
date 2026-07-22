#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

std::optional<llvm::Value *>
IRGen::tryEmitNetBuiltin(CallExpr *node, const std::string &funcName) {
    // === Stdlib: Networking ===
    // httpStatus(handle) -> i32
    if (funcName == "httpStatus" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_status");
        return builder_->CreateCall(fn, {handleArg}, "http.status");
    }

    // httpBody(handle) -> string
    if (funcName == "httpBody" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_body");
        auto *r = builder_->CreateCall(fn, {handleArg}, "http.body");
        trackStringTemp(r);
        return r;
    }

    // httpRequestEx(method, url, body, headersBlob, timeout_ms) -> i64 handle
    if (funcName == "httpRequestEx" && node->getArgs().size() >= 5) {
        auto *methodArg = visit(node->getArgs()[0].get());
        auto *urlArg = visit(node->getArgs()[1].get());
        auto *bodyArg = visit(node->getArgs()[2].get());
        auto *hdrArg = visit(node->getArgs()[3].get());
        auto *timeoutArg = visit(node->getArgs()[4].get());
        if (!methodArg || !urlArg || !bodyArg || !hdrArg || !timeoutArg) return nullptr;
        if (timeoutArg->getType()->isIntegerTy(32))
            timeoutArg = builder_->CreateSExt(timeoutArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_http_req_ex");
        return builder_->CreateCall(fn, {methodArg, urlArg, bodyArg, hdrArg, timeoutArg},
                                    "http.reqex.handle");
    }

    // httpRawHeaders(handle) -> string
    if (funcName == "httpRawHeaders" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_http_raw_headers"), {handleArg}, "http.rawhdr");
        trackStringTemp(r);
        return r;
    }

    // httpHeaderLookup(blob, name) -> string?
    if (funcName == "httpHeaderLookup" && node->getArgs().size() >= 2) {
        auto *blobArg = visit(node->getArgs()[0].get());
        auto *nameArg = visit(node->getArgs()[1].get());
        if (!blobArg || !nameArg) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_http_header_lookup"),
                                            {blobArg, nameArg}, "http.hdrlookup.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.hdrlookup.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.hdrlookup.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.hdrlookup.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "http.hdrlookup.result");
    }

    // httpClose(handle) -> void
    if (funcName == "httpClose" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_close");
        builder_->CreateCall(fn, {handleArg});
        return nullptr;
    }

    // wsSend(handle, msg) -> bool (true = success)
    if (funcName == "wsSend" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *msgArg = visit(node->getArgs()[1].get());
        if (!handleArg || !msgArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_send_text");
        auto *rc = builder_->CreateCall(fn, {handleArg, msgArg}, "ws.send.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "ws.send.ok");
    }

    // wsClose(handle, status, reason) -> void
    if (funcName == "wsClose" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *statusArg = visit(node->getArgs()[1].get());
        auto *reasonArg = visit(node->getArgs()[2].get());
        if (!handleArg || !statusArg || !reasonArg) return nullptr;
        if (statusArg->getType()->isIntegerTy(64))
            statusArg = builder_->CreateTrunc(statusArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_ws_close");
        builder_->CreateCall(fn, {handleArg, statusArg, reasonArg});
        return nullptr;
    }

    // wsIsOpen(handle) -> bool
    if (funcName == "wsIsOpen" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_is_open");
        auto *r = builder_->CreateCall(fn, {handleArg}, "ws.isopen.rc");
        return builder_->CreateICmpNE(r, builder_->getInt32(0), "ws.isopen.bool");
    }

    // wsConnectEx(url, headersBlob, subprotocol, keepAliveMs) -> i64
    if (funcName == "wsConnectEx" && node->getArgs().size() >= 4) {
        auto *urlArg = visit(node->getArgs()[0].get());
        auto *hdrArg = visit(node->getArgs()[1].get());
        auto *subArg = visit(node->getArgs()[2].get());
        auto *kaArg = visit(node->getArgs()[3].get());
        if (!urlArg || !hdrArg || !subArg || !kaArg) return nullptr;
        if (kaArg->getType()->isIntegerTy(32))
            kaArg = builder_->CreateSExt(kaArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_ws_connect_ex");
        return builder_->CreateCall(fn, {urlArg, hdrArg, subArg, kaArg}, "ws.connectex");
    }

    // wsRecvKind(handle) -> i32
    if (funcName == "wsRecvKind" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_recv");
        return builder_->CreateCall(fn, {handleArg}, "ws.recvkind");
    }

    // wsMsgText(handle) -> string
    if (funcName == "wsMsgText" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_ws_msg_text"), {handleArg}, "ws.msgtext");
        trackStringTemp(r);
        return r;
    }

    // wsMsgBytes(handle) -> [u8]  (out_len out-param pattern, mirrors sqliteColumnBlob)
    if (funcName == "wsMsgBytes" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "ws.bytes.olen",
            builder_->getInt64Ty());
        builder_->CreateStore(builder_->getInt64(0), outLenAlloca);
        auto *fn = getOrPanic("liva_ws_msg_bytes");
        auto *dataPtr = builder_->CreateCall(fn, {handleArg, outLenAlloca}, "ws.bytes.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(), outLenAlloca, "ws.bytes.olen.v");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "ws.bytes.da", daTy);
        builder_->CreateStore(dataPtr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "ws.bytes.val");
    }

    // wsSendBinary(handle, data: [u8]) -> bool  (mirrors sqliteBindBlob inbound)
    if (funcName == "wsSendBinary" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *arr = visit(node->getArgs()[1].get());
        if (!handleArg || !arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "ws.bin.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy, builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_ws_send_binary");
        auto *rc = builder_->CreateCall(fn, {handleArg, data, len}, "ws.sendbin.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "ws.sendbin.ok");
    }

    // pgNormalizeParams(sql) -> string
    if (funcName == "pgNormalizeParams" && !node->getArgs().empty()) {
        auto *sqlArg = visit(node->getArgs()[0].get());
        if (!sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_pg_normalize_params");
        auto *r = builder_->CreateCall(fn, {sqlArg}, "pg.normparams");
        trackStringTemp(r);
        return r;
    }

    // pgConnect(conninfo) -> i64
    if (funcName == "pgConnect" && !node->getArgs().empty()) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *fn = getOrPanic("liva_pg_connect");
        return builder_->CreateCall(fn, {s}, "pg.connect");
    }

    // pgClose(handle) -> void
    if (funcName == "pgClose" && !node->getArgs().empty()) {
        auto *h = visit(node->getArgs()[0].get());
        if (!h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_pg_close"), {h});
        return nullptr;
    }

    // pgExec(handle, sql) -> bool
    if (funcName == "pgExec" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        if (!h || !sql) return nullptr;
        auto *rc = builder_->CreateCall(getOrPanic("liva_pg_exec"), {h, sql}, "pg.exec.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "pg.exec.ok");
    }

    // pgErrmsg(handle) -> string
    if (funcName == "pgErrmsg" && !node->getArgs().empty()) {
        auto *h = visit(node->getArgs()[0].get());
        if (!h) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_errmsg"), {h}, "pg.errmsg");
        trackStringTemp(r);
        return r;
    }

    // pgQuery(handle, sql) -> i64 (result handle)
    if (funcName == "pgQuery" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        if (!h || !sql) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_pg_query"), {h, sql}, "pg.query");
    }

    // pgClear(result) -> void
    if (funcName == "pgClear" && !node->getArgs().empty()) {
        auto *res = visit(node->getArgs()[0].get());
        if (!res) return nullptr;
        builder_->CreateCall(getOrPanic("liva_pg_clear"), {res});
        return nullptr;
    }

    // pgResultRows(result) -> i32  /  pgResultCols(result) -> i32
    if ((funcName == "pgResultRows" || funcName == "pgResultCols") &&
        !node->getArgs().empty()) {
        auto *res = visit(node->getArgs()[0].get());
        if (!res) return nullptr;
        auto *fn = getOrPanic(funcName == "pgResultRows"
                              ? "liva_pg_ntuples" : "liva_pg_nfields");
        return builder_->CreateCall(fn, {res}, "pg.rescount");
    }

    // pgResultText(result, row, col) -> string
    if (funcName == "pgResultText" && node->getArgs().size() >= 3) {
        auto *res = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!res || !row || !col) return nullptr;
        if (row->getType()->isIntegerTy(64))
            row = builder_->CreateTrunc(row, builder_->getInt32Ty());
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_getvalue"), {res, row, col}, "pg.getval");
        trackStringTemp(r);
        return r;
    }

    // pgResultIsNull(result, row, col) -> bool
    if (funcName == "pgResultIsNull" && node->getArgs().size() >= 3) {
        auto *res = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!res || !row || !col) return nullptr;
        if (row->getType()->isIntegerTy(64))
            row = builder_->CreateTrunc(row, builder_->getInt32Ty());
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *rc = builder_->CreateCall(getOrPanic("liva_pg_getisnull"), {res, row, col}, "pg.isnull.rc");
        return builder_->CreateICmpNE(rc, builder_->getInt32(0), "pg.isnull");
    }

    // pgColumnName(result, col) -> string
    if (funcName == "pgColumnName" && node->getArgs().size() >= 2) {
        auto *res = visit(node->getArgs()[0].get());
        auto *col = visit(node->getArgs()[1].get());
        if (!res || !col) return nullptr;
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_fname"), {res, col}, "pg.fname");
        trackStringTemp(r);
        return r;
    }

    // pgQueryParams(handle, sql, params: [String]) -> i64 (result handle)
    if (funcName == "pgQueryParams" && node->getArgs().size() >= 3) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        auto *arr = visit(node->getArgs()[2].get());
        if (!h || !sql || !arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "pgp.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        // .data is the contiguous buffer of char* (string pointers).
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_pg_query_params");
        return builder_->CreateCall(fn, {h, sql, data, len}, "pg.queryparams");
    }

    // sqliteOpen(path) -> i64
    if (funcName == "sqliteOpen" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_open");
        return builder_->CreateCall(fn, {pathArg}, "sqlite.open");
    }

    // sqliteClose(handle) -> void
    if (funcName == "sqliteClose" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_close");
        builder_->CreateCall(fn, {handleArg});
        return nullptr;
    }

    // sqliteExec(handle, sql) -> bool (true on success)
    if (funcName == "sqliteExec" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_exec");
        auto *rc = builder_->CreateCall(fn, {handleArg, sqlArg}, "sqlite.exec.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.exec.ok");
    }

    // sqliteQueryFirst(handle, sql) -> string?
    if (funcName == "sqliteQueryFirst" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_query_first");
        auto *result = builder_->CreateCall(fn, {handleArg, sqlArg}, "sqlite.qfirst.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "sqlite.qfirst.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "sqlite.qfirst.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "sqlite.qfirst.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "sqlite.qfirst.result");
    }

    // sqliteQueryInt(handle, sql) -> i64? (nil when no row)
    if (funcName == "sqliteQueryInt" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *okAlloca = createEntryBlockAlloca(curFunc, "sqlite.qint.ok",
                                                 builder_->getInt32Ty());
        builder_->CreateStore(builder_->getInt32(0), okAlloca);
        auto *fn = getOrPanic("liva_sqlite_query_int");
        auto *val = builder_->CreateCall(fn, {handleArg, sqlArg, okAlloca},
                                          "sqlite.qint.val");
        auto *okVal = builder_->CreateLoad(builder_->getInt32Ty(), okAlloca,
                                            "sqlite.qint.okv");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt32(0),
                                               "sqlite.qint.hasval");
        auto *optTy = getOptionalType(builder_->getInt64Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "sqlite.qint.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(val, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "sqlite.qint.result");
    }

    // sqliteQueryColumn(handle, sql) -> string  (newline-joined first column)
    if (funcName == "sqliteQueryColumn" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_query_all_first_col");
        auto *r = builder_->CreateCall(fn, {handleArg, sqlArg}, "sqlite.qall");
        trackStringTemp(r);
        return r;
    }

    // sqliteLastInsertRowid(handle) -> i64
    if (funcName == "sqliteLastInsertRowid" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_last_insert_rowid");
        return builder_->CreateCall(fn, {handleArg}, "sqlite.lastid");
    }

    // sqliteChanges(handle) -> i32
    if (funcName == "sqliteChanges" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_changes");
        return builder_->CreateCall(fn, {handleArg}, "sqlite.changes");
    }

    // sqliteErrmsg(handle) -> string
    if (funcName == "sqliteErrmsg" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_errmsg");
        auto *r = builder_->CreateCall(fn, {handleArg}, "sqlite.errmsg");
        trackStringTemp(r);
        return r;
    }

    // sqlitePrepare(db, sql) -> i64
    if (funcName == "sqlitePrepare" && node->getArgs().size() >= 2) {
        auto *dbArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!dbArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_prepare");
        return builder_->CreateCall(fn, {dbArg, sqlArg}, "sqlite.prep");
    }

    // sqliteBindText(stmt, idx, val) -> bool
    if (funcName == "sqliteBindText" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !valArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_bind_text");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, valArg}, "sqlite.bind.text.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.text.ok");
    }

    // sqliteBindInt(stmt, idx, val) -> bool
    if (funcName == "sqliteBindInt" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !valArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        if (valArg->getType()->isIntegerTy(32))
            valArg = builder_->CreateSExt(valArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_sqlite_bind_int");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, valArg}, "sqlite.bind.int.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.int.ok");
    }

    // sqliteBindDouble(stmt, idx, val) -> bool
    if (funcName == "sqliteBindDouble" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !valArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        if (valArg->getType()->isFloatTy())
            valArg = builder_->CreateFPExt(valArg, builder_->getDoubleTy());
        auto *fn = getOrPanic("liva_sqlite_bind_double");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, valArg}, "sqlite.bind.dbl.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.dbl.ok");
    }

    // sqliteBindNull(stmt, idx) -> bool
    if (funcName == "sqliteBindNull" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !idxArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_bind_null");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg}, "sqlite.bind.null.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.null.ok");
    }

    // sqliteStep(stmt) -> bool (true if row available)
    if (funcName == "sqliteStep" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_step");
        auto *rc = builder_->CreateCall(fn, {stmtArg}, "sqlite.step.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(1), "sqlite.step.row");
    }

    // sqliteReset(stmt) -> bool
    if (funcName == "sqliteReset" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_reset");
        auto *rc = builder_->CreateCall(fn, {stmtArg}, "sqlite.reset.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.reset.ok");
    }

    // sqliteColumnCount(stmt) -> i32
    if (funcName == "sqliteColumnCount" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_column_count");
        return builder_->CreateCall(fn, {stmtArg}, "sqlite.colcount");
    }

    // sqliteColumnName(stmt, col) -> string
    if (funcName == "sqliteColumnName" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_name");
        auto *r = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.colname");
        trackStringTemp(r);
        return r;
    }

    // sqliteColumnType(stmt, col) -> i32
    if (funcName == "sqliteColumnType" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_type");
        return builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltype");
    }

    // sqliteColumnIsNull(stmt, col) -> bool  (column_type == 5)
    if (funcName == "sqliteColumnIsNull" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_type");
        auto *t = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltype.n");
        return builder_->CreateICmpEQ(t, builder_->getInt32(5), "sqlite.isnull");
    }

    // sqliteBindByName(stmt, name, val) -> bool
    if (funcName == "sqliteBindByName" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *nameArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !nameArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_bind_by_name");
        auto *rc = builder_->CreateCall(fn, {stmtArg, nameArg, valArg}, "sqlite.bindname.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bindname.ok");
    }

    // sqliteBindBlob(stmt, idx, data: [u8]) -> bool
    if (funcName == "sqliteBindBlob" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *arr = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !arr) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "blob.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_sqlite_bind_blob");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, data, len}, "sqlite.bindblob.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bindblob.ok");
    }

    // sqliteColumnBlob(stmt, col) -> [u8]
    if (funcName == "sqliteColumnBlob" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "blob.olen",
            builder_->getInt64Ty());
        builder_->CreateStore(builder_->getInt64(0), outLenAlloca);
        auto *fn = getOrPanic("liva_sqlite_column_blob");
        auto *dataPtr = builder_->CreateCall(fn, {stmtArg, colArg, outLenAlloca}, "blob.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(), outLenAlloca, "blob.olen.v");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "blob.da", daTy);
        builder_->CreateStore(dataPtr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "blob.val");
    }

    // sqliteColumnText(stmt, col) -> string
    if (funcName == "sqliteColumnText" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_text");
        auto *r = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltext");
        trackStringTemp(r);
        return r;
    }

    // sqliteColumnInt(stmt, col) -> i64
    if (funcName == "sqliteColumnInt" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_int");
        return builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.colint");
    }

    // sqliteColumnDouble(stmt, col) -> f64
    if (funcName == "sqliteColumnDouble" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_double");
        return builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coldbl");
    }

    // sqliteFinalize(stmt) -> void
    if (funcName == "sqliteFinalize" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_finalize");
        builder_->CreateCall(fn, {stmtArg});
        return nullptr;
    }

    return std::nullopt;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
