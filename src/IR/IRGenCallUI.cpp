#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

std::optional<llvm::Value *>
IRGen::tryEmitUIBuiltin(CallExpr *node, const std::string &funcName) {
    // === Stdlib: UI (wxWidgets wrapper) ===

    // Helper lambda: emit a callback call decomposing Liva closure into func+env
    auto emitCallbackCall = [&](const std::string &cFuncName, int32_t handleArgIdx,
                                 int32_t closureArgIdx) -> llvm::Value * {
        auto *handle = visit(node->getArgs()[handleArgIdx].get());
        auto *closureVal = visit(node->getArgs()[closureArgIdx].get());
        if (!handle || !closureVal) return nullptr;
        auto *closureObjTy = getClosureObjTy();
        auto *alloca = createEntryBlockAlloca(
            builder_->GetInsertBlock()->getParent(), "cb.tmp", closureObjTy);
        builder_->CreateStore(closureVal, alloca);
        auto *funcPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 0));
        auto *envPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 1));
        // Free-function / ordinary-method path: the env stays on the caller's
        // stack, so pass size 0 (no heap-own). The inline closure-literal fast
        // path in visitCallExpr passes a real size instead.
        auto *i32Ty = builder_->getInt32Ty();
        builder_->CreateCall(getOrPanic(cFuncName.c_str()),
                             {handle, funcPtr, envPtr, llvm::ConstantInt::get(i32Ty, 0)});
        return llvm::Constant::getNullValue(i32Ty);
    };

    // appInit() -> void
    if (funcName == "appInit" && node->getArgs().empty()) {
        builder_->CreateCall(getOrPanic("liva_ui_app_init"), {});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // appRun() -> void
    if (funcName == "appRun" && node->getArgs().empty()) {
        builder_->CreateCall(getOrPanic("liva_ui_app_run"), {});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // appQuit() -> void
    if (funcName == "appQuit" && node->getArgs().empty()) {
        builder_->CreateCall(getOrPanic("liva_ui_app_quit"), {});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // createWindow(w, h, title) -> i32
    if (funcName == "createWindow" && node->getArgs().size() >= 3) {
        auto *w = visit(node->getArgs()[0].get());
        auto *h = visit(node->getArgs()[1].get());
        auto *title = visit(node->getArgs()[2].get());
        if (!w || !h || !title) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_window"), {w, h, title}, "ui.win");
    }

    // windowShow(handle, show) -> void
    if (funcName == "windowShow" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *show = visit(node->getArgs()[1].get());
        if (!handle || !show) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_window_show"), {handle, show});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // windowSetTitle(handle, title) -> void
    if (funcName == "windowSetTitle" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *title = visit(node->getArgs()[1].get());
        if (!handle || !title) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_window_set_title"), {handle, title});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // windowGetWidth(handle) -> i32
    if (funcName == "windowGetWidth" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_window_get_width"), {handle}, "ui.ww");
    }

    // windowGetHeight(handle) -> i32
    if (funcName == "windowGetHeight" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_window_get_height"), {handle}, "ui.wh");
    }

    // windowOnClose(handle, callback) -> void
    if (funcName == "windowOnClose" && node->getArgs().size() >= 2) {
        return emitCallbackCall("liva_ui_window_on_close", 0, 1);
    }

    // Widget creation: create*(parent) -> i32
    // Single-arg parent versions
    for (const auto &[livaName, cName] : std::initializer_list<std::pair<const char *, const char *>>{
             {"createPanel", "liva_ui_create_panel"},
             {"createListBox", "liva_ui_create_listbox"},
             {"createTabView", "liva_ui_create_tabview"},
             {"createScrollView", "liva_ui_create_scrollview"},
             {"createDivider", "liva_ui_create_divider"},
             {"createCanvas", "liva_ui_create_canvas"}}) {
        if (funcName == livaName && node->getArgs().size() >= 1) {
            auto *parent = visit(node->getArgs()[0].get());
            if (!parent) return nullptr;
            return builder_->CreateCall(getOrPanic(cName), {parent}, "ui.w");
        }
    }

    // Widget creation: create*(parent, text) -> i32
    for (const auto &[livaName, cName] : std::initializer_list<std::pair<const char *, const char *>>{
             {"createButton", "liva_ui_create_button"},
             {"createLabel", "liva_ui_create_label"},
             {"createTextInput", "liva_ui_create_textinput"},
             {"createCheckbox", "liva_ui_create_checkbox"},
             {"createTextArea", "liva_ui_create_textarea"},
             {"createRadioGroup", "liva_ui_create_radiogroup"},
             {"createDropdown", "liva_ui_create_dropdown"},
             {"createImageView", "liva_ui_create_imageview"}}) {
        if (funcName == livaName && node->getArgs().size() >= 2) {
            auto *parent = visit(node->getArgs()[0].get());
            auto *text = visit(node->getArgs()[1].get());
            if (!parent || !text) return nullptr;
            return builder_->CreateCall(getOrPanic(cName), {parent, text}, "ui.w");
        }
    }

    // createSlider(parent, min, max, val) -> i32
    if (funcName == "createSlider" && node->getArgs().size() >= 4) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *minV = visit(node->getArgs()[1].get());
        auto *maxV = visit(node->getArgs()[2].get());
        auto *val = visit(node->getArgs()[3].get());
        if (!parent || !minV || !maxV || !val) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_slider"),
                                    {parent, minV, maxV, val}, "ui.sl");
    }

    // createProgressBar(parent, range) -> i32
    if (funcName == "createProgressBar" && node->getArgs().size() >= 2) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *range = visit(node->getArgs()[1].get());
        if (!parent || !range) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_progressbar"),
                                    {parent, range}, "ui.pb");
    }

    // setText(handle, text) -> void
    if (funcName == "setText" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *text = visit(node->getArgs()[1].get());
        if (!handle || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_text"), {handle, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // getText(handle) -> string
    if (funcName == "getText" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_get_text"), {handle}, "ui.gt");
    }

    // setValue(handle, val) -> void
    if (funcName == "setValue" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *val = visit(node->getArgs()[1].get());
        if (!handle || !val) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_value"), {handle, val});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // getValue(handle) -> i32
    if (funcName == "getValue" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_get_value"), {handle}, "ui.gv");
    }

    // setEnabled(handle, enabled) -> void
    if (funcName == "setEnabled" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *flag = visit(node->getArgs()[1].get());
        if (!handle || !flag) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_enabled"), {handle, flag});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setVisible(handle, visible) -> void
    if (funcName == "setVisible" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *flag = visit(node->getArgs()[1].get());
        if (!handle || !flag) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_visible"), {handle, flag});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setWidgetSize(handle, w, h) -> void
    if (funcName == "setWidgetSize" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *w = visit(node->getArgs()[1].get());
        auto *h = visit(node->getArgs()[2].get());
        if (!handle || !w || !h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_size"), {handle, w, h});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setBounds(handle, x, y, w, h) -> void
    if (funcName == "setBounds" && node->getArgs().size() >= 5) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *x = visit(node->getArgs()[1].get());
        auto *y = visit(node->getArgs()[2].get());
        auto *w = visit(node->getArgs()[3].get());
        auto *h = visit(node->getArgs()[4].get());
        if (!handle || !x || !y || !w || !h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_bounds"), {handle, x, y, w, h});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // ── Phase 2: menu / statusbar / toolbar intrinsics ──────────────────
    // createMenuBar() -> i32
    if (funcName == "createMenuBar" && node->getArgs().empty())
        return builder_->CreateCall(getOrPanic("liva_ui_create_menu_bar"), {}, "ui.mb");
    // createMenu(title) -> i32
    if (funcName == "createMenu" && node->getArgs().size() >= 1) {
        auto *title = visit(node->getArgs()[0].get());
        if (!title) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_menu"), {title}, "ui.menu");
    }
    // menuAddItem(menu, label) -> i32
    if (funcName == "menuAddItem" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!m || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_menu_add_item"), {m, l}, "ui.mi");
    }
    // menuAddCheckItem(menu, label) -> i32
    if (funcName == "menuAddCheckItem" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!m || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_menu_add_check_item"), {m, l}, "ui.mci");
    }
    // menuAddSeparator(menu) -> void
    if (funcName == "menuAddSeparator" && node->getArgs().size() >= 1) {
        auto *m = visit(node->getArgs()[0].get());
        if (!m) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_add_separator"), {m});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuAddSubmenu(menu, label, sub) -> void
    if (funcName == "menuAddSubmenu" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        auto *s = visit(node->getArgs()[2].get());
        if (!m || !l || !s) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_add_submenu"), {m, l, s});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuBarAddMenu(bar, menu) -> void
    if (funcName == "menuBarAddMenu" && node->getArgs().size() >= 2) {
        auto *b = visit(node->getArgs()[0].get());
        auto *m = visit(node->getArgs()[1].get());
        if (!b || !m) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_bar_add_menu"), {b, m});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // windowSetMenuBar(window, bar) -> void
    if (funcName == "windowSetMenuBar" && node->getArgs().size() >= 2) {
        auto *w = visit(node->getArgs()[0].get());
        auto *b = visit(node->getArgs()[1].get());
        if (!w || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_window_set_menu_bar"), {w, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuItemSetEnabled(item, enabled) -> void
    if (funcName == "menuItemSetEnabled" && node->getArgs().size() >= 2) {
        auto *i = visit(node->getArgs()[0].get());
        auto *e = visit(node->getArgs()[1].get());
        if (!i || !e) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_item_set_enabled"), {i, e});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuItemSetChecked(item, checked) -> void
    if (funcName == "menuItemSetChecked" && node->getArgs().size() >= 2) {
        auto *i = visit(node->getArgs()[0].get());
        auto *c = visit(node->getArgs()[1].get());
        if (!i || !c) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_item_set_checked"), {i, c});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuPopup(menu, target) -> void
    if (funcName == "menuPopup" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *t = visit(node->getArgs()[1].get());
        if (!m || !t) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_popup"), {m, t});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createStatusBar(window, fieldCount) -> i32
    if (funcName == "createStatusBar" && node->getArgs().size() >= 2) {
        auto *w = visit(node->getArgs()[0].get());
        auto *fc = visit(node->getArgs()[1].get());
        if (!w || !fc) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_status_bar"), {w, fc}, "ui.sb");
    }
    // statusBarSetText(sb, field, text) -> void
    if (funcName == "statusBarSetText" && node->getArgs().size() >= 3) {
        auto *s = visit(node->getArgs()[0].get());
        auto *f = visit(node->getArgs()[1].get());
        auto *t = visit(node->getArgs()[2].get());
        if (!s || !f || !t) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_status_bar_set_text"), {s, f, t});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createToolbar(window) -> i32
    if (funcName == "createToolbar" && node->getArgs().size() >= 1) {
        auto *w = visit(node->getArgs()[0].get());
        if (!w) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_toolbar"), {w}, "ui.tb");
    }
    // toolbarAddTool(tb, label) -> i32
    if (funcName == "toolbarAddTool" && node->getArgs().size() >= 2) {
        auto *tb = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!tb || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_toolbar_add_tool"), {tb, l}, "ui.tool");
    }
    // toolbarAddSeparator(tb) -> void
    if (funcName == "toolbarAddSeparator" && node->getArgs().size() >= 1) {
        auto *tb = visit(node->getArgs()[0].get());
        if (!tb) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_toolbar_add_separator"), {tb});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // toolbarRealize(tb) -> void
    if (funcName == "toolbarRealize" && node->getArgs().size() >= 1) {
        auto *tb = visit(node->getArgs()[0].get());
        if (!tb) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_toolbar_realize"), {tb});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // toolItemSetEnabled(tool, enabled) -> void
    if (funcName == "toolItemSetEnabled" && node->getArgs().size() >= 2) {
        auto *t = visit(node->getArgs()[0].get());
        auto *e = visit(node->getArgs()[1].get());
        if (!t || !e) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_tool_item_set_enabled"), {t, e});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 3: new widgets ─────────────────────────────────────────
    // createSpinCtrl(parent, min, max, val) -> i32
    if (funcName == "createSpinCtrl" && node->getArgs().size() >= 4) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *minV = visit(node->getArgs()[1].get());
        auto *maxV = visit(node->getArgs()[2].get());
        auto *val = visit(node->getArgs()[3].get());
        if (!parent || !minV || !maxV || !val) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_spin_ctrl"),
                                    {parent, minV, maxV, val}, "ui.spin");
    }
    // createDatePicker(parent) -> i32
    if (funcName == "createDatePicker" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_date_picker"),
                                    {parent}, "ui.date");
    }
    // dateGetValue(handle) -> string
    if (funcName == "dateGetValue" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_date_get_value"),
                                    {handle}, "ui.dgv");
    }
    // createComboBox(parent, value) -> i32
    if (funcName == "createComboBox" && node->getArgs().size() >= 2) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *value = visit(node->getArgs()[1].get());
        if (!parent || !value) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_combo_box"),
                                    {parent, value}, "ui.combo");
    }
    // comboAddItem(handle, item) -> void
    if (funcName == "comboAddItem" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *item = visit(node->getArgs()[1].get());
        if (!handle || !item) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_combo_add_item"), {handle, item});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createTreeView(parent) -> i32
    if (funcName == "createTreeView" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_tree_view"),
                                    {parent}, "ui.tree");
    }
    // treeAddRoot(handle, label) -> i32
    if (funcName == "treeAddRoot" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *label = visit(node->getArgs()[1].get());
        if (!handle || !label) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_add_root"),
                                    {handle, label}, "ui.troot");
    }
    // treeAddNode(handle, parentNode, label) -> i32
    if (funcName == "treeAddNode" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *parentNode = visit(node->getArgs()[1].get());
        auto *label = visit(node->getArgs()[2].get());
        if (!handle || !parentNode || !label) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_add_node"),
                                    {handle, parentNode, label}, "ui.tnode");
    }
    // treeGetSelection(handle) -> i32
    if (funcName == "treeGetSelection" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_get_selection"),
                                    {handle}, "ui.tsel");
    }
    // createDataGrid(parent, rows, cols) -> i32
    if (funcName == "createDataGrid" && node->getArgs().size() >= 3) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *rows = visit(node->getArgs()[1].get());
        auto *cols = visit(node->getArgs()[2].get());
        if (!parent || !rows || !cols) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_data_grid"),
                                    {parent, rows, cols}, "ui.grid2");
    }
    // gridSetCell(handle, row, col, text) -> void
    if (funcName == "gridSetCell" && node->getArgs().size() >= 4) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        auto *text = visit(node->getArgs()[3].get());
        if (!handle || !row || !col || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_grid_set_cell"),
                             {handle, row, col, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // gridGetCell(handle, row, col) -> string
    if (funcName == "gridGetCell" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!handle || !row || !col) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_grid_get_cell"),
                                    {handle, row, col}, "ui.gcell");
    }
    // gridSetColLabel(handle, col, text) -> void
    if (funcName == "gridSetColLabel" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *col = visit(node->getArgs()[1].get());
        auto *text = visit(node->getArgs()[2].get());
        if (!handle || !col || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_grid_set_col_label"),
                             {handle, col, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createSplitter(parent) -> i32
    if (funcName == "createSplitter" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_splitter"),
                                    {parent}, "ui.split");
    }
    // splitterSplitV(handle, left, right) -> void
    if (funcName == "splitterSplitV" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *left = visit(node->getArgs()[1].get());
        auto *right = visit(node->getArgs()[2].get());
        if (!handle || !left || !right) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_split_v"),
                             {handle, left, right});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // splitterSplitH(handle, top, bottom) -> void
    if (funcName == "splitterSplitH" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *top = visit(node->getArgs()[1].get());
        auto *bottom = visit(node->getArgs()[2].get());
        if (!handle || !top || !bottom) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_split_h"),
                             {handle, top, bottom});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // splitterSetSash(handle, px) -> void
    if (funcName == "splitterSetSash" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *px = visit(node->getArgs()[1].get());
        if (!handle || !px) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_set_sash"), {handle, px});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 4: Align/Anchors layout ────────────────────────────────
    // setAlign(handle, align) -> void  (align: basit enum → i32 discriminant)
    if (funcName == "setAlign" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *align = visit(node->getArgs()[1].get());
        if (!handle || !align) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_align"), {handle, align});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // setAnchors(handle, left, top, right, bottom) -> void
    if (funcName == "setAnchors" && node->getArgs().size() >= 5) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        auto *t = visit(node->getArgs()[2].get());
        auto *r = visit(node->getArgs()[3].get());
        auto *b = visit(node->getArgs()[4].get());
        if (!handle || !l || !t || !r || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_anchors"), {handle, l, t, r, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 5: data binding ────────────────────────────────────────
    // modelCreate() -> i32
    if (funcName == "modelCreate" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_model_create"), {}, "ui.model");
    }
    // modelSetText(model, key, val) -> void
    if (funcName == "modelSetText" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_set_text"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelGetText(model, key) -> string
    if (funcName == "modelGetText" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_get_text"), {m, k}, "ui.mget");
    }
    // modelBindText(model, key, widget) -> void
    if (funcName == "modelBindText" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_text"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelSetInt(model, key, val) -> void
    if (funcName == "modelSetInt" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_set_int"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelGetInt(model, key) -> i32
    if (funcName == "modelGetInt" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_get_int"), {m, k}, "ui.mgeti");
    }
    // modelBindInt(model, key, widget) -> void
    if (funcName == "modelBindInt" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_int"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 6: collection binding ──────────────────────────────────
    // modelBindList(model, key, widget) -> void
    if (funcName == "modelBindList" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_list"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListAdd(model, key, item) -> void
    if (funcName == "modelListAdd" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_list_add"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListClear(model, key) -> void
    if (funcName == "modelListClear" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_list_clear"), {m, k});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListCount(model, key) -> i32
    if (funcName == "modelListCount" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_list_count"), {m, k}, "ui.mlcount");
    }
    // ── Phase 6.1: list readback ─────────────────────────────────────
    // modelListGet(model, key, index) -> string
    if (funcName == "modelListGet" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *i = visit(node->getArgs()[2].get());
        if (!m || !k || !i) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_list_get"),
                                    {m, k, i}, "ui.mlget");
    }
    // Closure-taking free-function forms (called from class methods; stack env, size 0)
    if (funcName == "menuItemOnClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_menu_item_on_click", 0, 1);
    if (funcName == "toolItemOnClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_tool_item_on_click", 0, 1);
    if (funcName == "onRightClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_right_click", 0, 1);

    // setWidgetFont(handle, size, bold) -> void
    if (funcName == "setWidgetFont" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *size = visit(node->getArgs()[1].get());
        auto *bold = visit(node->getArgs()[2].get());
        if (!handle || !size || !bold) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_font"), {handle, size, bold});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setBgColor(handle, r, g, b) -> void
    if (funcName == "setBgColor" && node->getArgs().size() >= 4) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *r = visit(node->getArgs()[1].get());
        auto *g = visit(node->getArgs()[2].get());
        auto *b = visit(node->getArgs()[3].get());
        if (!handle || !r || !g || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_bg_color"), {handle, r, g, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setFgColor(handle, r, g, b) -> void
    if (funcName == "setFgColor" && node->getArgs().size() >= 4) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *r = visit(node->getArgs()[1].get());
        auto *g = visit(node->getArgs()[2].get());
        auto *b = visit(node->getArgs()[3].get());
        if (!handle || !r || !g || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_fg_color"), {handle, r, g, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setTooltip(handle, text) -> void
    if (funcName == "setTooltip" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *text = visit(node->getArgs()[1].get());
        if (!handle || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_tooltip"), {handle, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // destroyWidget(handle) -> void
    if (funcName == "destroyWidget" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_destroy_widget"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // Layout: createVBoxSizer() -> i32
    if (funcName == "createVBoxSizer" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_create_vbox_sizer"), {}, "ui.vbox");
    }

    // createHBoxSizer() -> i32
    if (funcName == "createHBoxSizer" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_create_hbox_sizer"), {}, "ui.hbox");
    }

    // createGridSizer(rows, cols, hgap, vgap) -> i32
    if (funcName == "createGridSizer" && node->getArgs().size() >= 4) {
        auto *rows = visit(node->getArgs()[0].get());
        auto *cols = visit(node->getArgs()[1].get());
        auto *hgap = visit(node->getArgs()[2].get());
        auto *vgap = visit(node->getArgs()[3].get());
        if (!rows || !cols || !hgap || !vgap) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_grid_sizer"),
                                    {rows, cols, hgap, vgap}, "ui.grid");
    }

    // createFlexGridSizer(rows, cols, hgap, vgap) -> i32
    if (funcName == "createFlexGridSizer" && node->getArgs().size() >= 4) {
        auto *rows = visit(node->getArgs()[0].get());
        auto *cols = visit(node->getArgs()[1].get());
        auto *hgap = visit(node->getArgs()[2].get());
        auto *vgap = visit(node->getArgs()[3].get());
        if (!rows || !cols || !hgap || !vgap) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_flex_grid_sizer"),
                                    {rows, cols, hgap, vgap}, "ui.fgrid");
    }

    // sizerAdd(sizer, widget, proportion, flags, border) -> void
    if (funcName == "sizerAdd" && node->getArgs().size() >= 5) {
        auto *sizer = visit(node->getArgs()[0].get());
        auto *widget = visit(node->getArgs()[1].get());
        auto *prop = visit(node->getArgs()[2].get());
        auto *flags = visit(node->getArgs()[3].get());
        auto *border = visit(node->getArgs()[4].get());
        if (!sizer || !widget || !prop || !flags || !border) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_sizer_add"),
                             {sizer, widget, prop, flags, border});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setSizer(parent, sizer) -> void
    if (funcName == "setSizer" && node->getArgs().size() >= 2) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *sizer = visit(node->getArgs()[1].get());
        if (!parent || !sizer) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_sizer"), {parent, sizer});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // Event callbacks: onClick/onChange/onSelect/onKey(handle, closure) -> void
    if (funcName == "onClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_click", 0, 1);
    if (funcName == "onChange" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_change", 0, 1);
    if (funcName == "onSelect" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_select", 0, 1);
    if (funcName == "onKey" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_key", 0, 1);

    // listAddItem(handle, item) -> void
    if (funcName == "listAddItem" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *item = visit(node->getArgs()[1].get());
        if (!handle || !item) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_list_add_item"), {handle, item});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // listClear(handle) -> void
    if (funcName == "listClear" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_list_clear"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // listGetSelection(handle) -> i32
    if (funcName == "listGetSelection" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_list_get_selection"), {handle}, "ui.lsel");
    }

    // tabAddPage(tab, page, title) -> void
    if (funcName == "tabAddPage" && node->getArgs().size() >= 3) {
        auto *tab = visit(node->getArgs()[0].get());
        auto *page = visit(node->getArgs()[1].get());
        auto *title = visit(node->getArgs()[2].get());
        if (!tab || !page || !title) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_tab_add_page"), {tab, page, title});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // tabGetSelection(handle) -> i32
    if (funcName == "tabGetSelection" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tab_get_selection"), {handle}, "ui.tsel");
    }

    // messageBox(title, message, style) -> void
    if (funcName == "messageBox" && node->getArgs().size() >= 3) {
        auto *title = visit(node->getArgs()[0].get());
        auto *msg = visit(node->getArgs()[1].get());
        auto *style = visit(node->getArgs()[2].get());
        if (!title || !msg || !style) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_message_box"), {title, msg, style});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // fileDialog(parent, title, wildcard, style) -> string
    if (funcName == "fileDialog" && node->getArgs().size() >= 4) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *title = visit(node->getArgs()[1].get());
        auto *wildcard = visit(node->getArgs()[2].get());
        auto *style = visit(node->getArgs()[3].get());
        if (!parent || !title || !wildcard || !style) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_file_dialog"),
                                    {parent, title, wildcard, style}, "ui.fdlg");
    }

    // colorDialog(parent) -> i32
    if (funcName == "colorDialog" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_color_dialog"), {parent}, "ui.cdlg");
    }

    // createTimer(intervalMs, callback) -> i32
    if (funcName == "createTimer" && node->getArgs().size() >= 2) {
        auto *interval = visit(node->getArgs()[0].get());
        auto *closureVal = visit(node->getArgs()[1].get());
        if (!interval || !closureVal) return nullptr;
        auto *closureObjTy = getClosureObjTy();
        auto *alloca = createEntryBlockAlloca(
            builder_->GetInsertBlock()->getParent(), "tmr.cb", closureObjTy);
        builder_->CreateStore(closureVal, alloca);
        auto *funcPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 0));
        auto *envPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 1));
        return builder_->CreateCall(getOrPanic("liva_ui_create_timer"),
                                    {interval, funcPtr, envPtr}, "ui.tmr");
    }

    // stopTimer(handle) -> void
    if (funcName == "stopTimer" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_stop_timer"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // getClipboardText() -> string
    if (funcName == "getClipboardText" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_get_clipboard_text"), {}, "ui.clip");
    }

    // setClipboardText(text) -> void
    if (funcName == "setClipboardText" && node->getArgs().size() == 1) {
        auto *text = visit(node->getArgs()[0].get());
        if (!text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_clipboard_text"), {text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // Canvas: canvasOnPaint(handle, callback) -> void
    if (funcName == "canvasOnPaint" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_canvas_on_paint", 0, 1);

    // canvasRefresh(handle) -> void
    if (funcName == "canvasRefresh" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_canvas_refresh"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcClear(dc, r, g, b) -> void
    if (funcName == "dcClear" && node->getArgs().size() >= 4) {
        auto *dc = visit(node->getArgs()[0].get());
        auto *r = visit(node->getArgs()[1].get());
        auto *g = visit(node->getArgs()[2].get());
        auto *b = visit(node->getArgs()[3].get());
        if (!dc || !r || !g || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_dc_clear"), {dc, r, g, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawRect(dc, x, y, w, h, r, g, b) -> void
    if (funcName == "dcDrawRect" && node->getArgs().size() >= 8) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 8; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_rect"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawText(dc, text, x, y, r, g, b) -> void
    if (funcName == "dcDrawText" && node->getArgs().size() >= 7) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 7; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_text"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawLine(dc, x1, y1, x2, y2, r, g, b) -> void
    if (funcName == "dcDrawLine" && node->getArgs().size() >= 8) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 8; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_line"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawCircle(dc, cx, cy, radius, r, g, b) -> void
    if (funcName == "dcDrawCircle" && node->getArgs().size() >= 7) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 7; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_circle"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }


    return std::nullopt;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
