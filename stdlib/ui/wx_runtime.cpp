/**
 * wx_runtime.cpp — Liva UI runtime (wxWidgets backend)
 *
 * C++ implementation wrapping wxWidgets classes.
 * All wx objects are stored in a global handle table (int32_t → void*).
 * Liva closures arrive as (func_ptr, env_ptr) pairs and are bound to wx events.
 */

#include "wx_runtime.h"

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/listbox.h>
#include <wx/slider.h>
#include <wx/gauge.h>
#include <wx/radiobox.h>
#include <wx/choice.h>
#include <wx/statline.h>
#include <wx/clipbrd.h>
#include <wx/colordlg.h>
#include <wx/filedlg.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/toolbar.h>
#include <wx/statusbr.h>
#include <wx/artprov.h>
#include <wx/spinctrl.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/combobox.h>
#include <wx/treectrl.h>
#include <wx/grid.h>
#include <wx/splitter.h>

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

/* ═══════════════════════════════════════════════════════════════════
   Handle table
   ═══════════════════════════════════════════════════════════════════ */

static std::unordered_map<int32_t, void *> g_handles;
static int32_t g_nextHandle = 1;

// Separate table for sizers (they are not wxWindow*)
static std::unordered_map<int32_t, wxSizer *> g_sizers;
static int32_t g_nextSizerHandle = -1; // negative to avoid collision

static int32_t allocHandle(void *obj) {
    int32_t h = g_nextHandle++;
    g_handles[h] = obj;
    return h;
}

template <typename T>
static T *getHandle(int32_t h) {
    auto it = g_handles.find(h);
    return (it != g_handles.end()) ? static_cast<T *>(it->second) : nullptr;
}

static int32_t allocSizerHandle(wxSizer *sizer) {
    int32_t h = g_nextSizerHandle--;
    g_sizers[h] = sizer;
    return h;
}

static wxSizer *getSizer(int32_t h) {
    auto it = g_sizers.find(h);
    return (it != g_sizers.end()) ? it->second : nullptr;
}

/* ═══════════════════════════════════════════════════════════════════
   Env ownership: heap-copy stack-allocated closure envs
   ═══════════════════════════════════════════════════════════════════ */

// Widget handle -> heap-owned closure env pointers (freed on widget destroy)
static std::unordered_map<int32_t, std::vector<void *>> g_widgetEnvs;

// If size > 0, heap-copy the (stack) env and register it for free-on-destroy.
// Returns the pointer the callback lambda should capture (heap copy or original).
static void *ownEnv(int32_t widgetHandle, void *env, int32_t size) {
    if (size <= 0 || env == nullptr) return env;   // non-literal / no-capture: keep as-is
    void *heap = std::malloc(static_cast<size_t>(size));
    std::memcpy(heap, env, static_cast<size_t>(size));
    g_widgetEnvs[widgetHandle].push_back(heap);
    return heap;
}

static void freeWidgetEnvs(int32_t widgetHandle) {
    auto it = g_widgetEnvs.find(widgetHandle);
    if (it == g_widgetEnvs.end()) return;
    for (void *p : it->second) std::free(p);
    g_widgetEnvs.erase(it);
}

/* ═══════════════════════════════════════════════════════════════════
   Liva callback wrapper
   ═══════════════════════════════════════════════════════════════════ */

using LivaCallbackFn = void (*)(void *env, int32_t handle);
using LivaKeyFn = void (*)(void *env, int32_t handle, int32_t keycode);
using LivaXYFn = void (*)(void *env, int32_t handle, int32_t x, int32_t y);

// Menu/tool items are not wxWindow*; track the wx object, its command id, and
// the owning frame (for late event binding).
struct LivaMenuItem {
    wxMenuItem *item = nullptr;
    int32_t id = 0;
    wxMenu *ownerMenu = nullptr;     // the menu this item belongs to
    wxFrame *ownerFrame = nullptr;   // resolved when the menubar/popup is shown
    LivaCallbackFn pendingFn = nullptr;
    void *pendingEnv = nullptr;
    bool hasPending = false;
};
static std::unordered_map<int32_t, LivaMenuItem> g_menuItems;

struct LivaToolItem {
    wxToolBarToolBase *tool = nullptr;
    int32_t id = 0;
    wxToolBar *toolbar = nullptr;
};
static std::unordered_map<int32_t, LivaToolItem> g_toolItems;

// TreeView nodes are wxTreeItemId values (not wxWindow*); map them to i32
// handles using the global handle counter (unique, but kept out of g_handles
// since they are never looked up via getHandle<>). Mirrors the menu-item table.
static std::unordered_map<int32_t, wxTreeItemId> g_treeNodes;

static int g_nextCmdId = 20000;  // wx user command id range

static std::unordered_map<int32_t, wxString> &g_menuTitles() {
    static std::unordered_map<int32_t, wxString> m;
    return m;
}

// Free the heap-owned callback envs of every menu/tool item bound to `frame`.
// Menu/tool item envs are registered (in g_widgetEnvs) under the ITEM handle,
// so the window-destroy sweep (which only knows the window handle) would miss
// them; this sweeps them by owner frame. Called when a frame is destroyed.
static void freeFrameOwnedItemEnvs(wxFrame *frame) {
    if (!frame) return;
    for (auto &kv : g_menuItems) {
        if (kv.second.ownerFrame == frame) freeWidgetEnvs(kv.first);
    }
    for (auto &kv : g_toolItems) {
        if (kv.second.toolbar && kv.second.toolbar->GetParent() == frame)
            freeWidgetEnvs(kv.first);
    }
}

struct LivaCallback {
    LivaCallbackFn func = nullptr;
    void *env = nullptr;
    int32_t widgetHandle = 0;

    void invoke() const {
        if (func) func(env, widgetHandle);
    }
};

/* ═══════════════════════════════════════════════════════════════════
   wxApp subclass
   ═══════════════════════════════════════════════════════════════════ */

class LivaApp : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        return true;
    }
};

static LivaApp *g_app = nullptr;
static bool g_appInitialized = false;

/* ═══════════════════════════════════════════════════════════════════
   Canvas panel (for custom drawing)
   ═══════════════════════════════════════════════════════════════════ */

// Forward: DC handle table for paint callbacks
static std::unordered_map<int32_t, wxDC *> g_dcHandles;
static int32_t g_nextDcHandle = 100000;

static int32_t allocDcHandle(wxDC *dc) {
    int32_t h = g_nextDcHandle++;
    g_dcHandles[h] = dc;
    return h;
}

static wxDC *getDcHandle(int32_t h) {
    auto it = g_dcHandles.find(h);
    return (it != g_dcHandles.end()) ? it->second : nullptr;
}

// Callback type for canvas paint: func(env, dcHandle)
using LivaPaintFn = void (*)(void *env, int32_t dcHandle);

class LivaCanvas : public wxPanel {
public:
    LivaCanvas(wxWindow *parent)
        : wxPanel(parent, wxID_ANY) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &LivaCanvas::OnPaint, this);
    }

    void SetPaintCallback(LivaPaintFn fn, void *env) {
        paintFunc_ = fn;
        paintEnv_ = env;
    }

private:
    void OnPaint(wxPaintEvent &) {
        wxBufferedPaintDC dc(this);
        if (paintFunc_) {
            int32_t dcH = allocDcHandle(&dc);
            paintFunc_(paintEnv_, dcH);
            g_dcHandles.erase(dcH);
        }
    }

    LivaPaintFn paintFunc_ = nullptr;
    void *paintEnv_ = nullptr;
};

/* ═══════════════════════════════════════════════════════════════════
   Timer wrapper
   ═══════════════════════════════════════════════════════════════════ */

class LivaTimer : public wxTimer {
public:
    LivaTimer(LivaCallbackFn fn, void *env, int32_t handle)
        : cb_{fn, env, handle} {}

    void Notify() override {
        cb_.invoke();
    }

private:
    LivaCallback cb_;
};

/* ═══════════════════════════════════════════════════════════════════
   Temporary string storage for returned const char*
   ═══════════════════════════════════════════════════════════════════ */

static std::string g_tempStr;

static const char *returnTempStr(const wxString &s) {
    g_tempStr = s.utf8_string();
    return g_tempStr.c_str();
}

/* ═══════════════════════════════════════════════════════════════════
   Helper: split semicolon-separated string into wxArrayString
   ═══════════════════════════════════════════════════════════════════ */

static wxArrayString splitChoices(const char *choices) {
    wxArrayString arr;
    if (!choices) return arr;
    std::string s(choices);
    size_t start = 0;
    while (start < s.size()) {
        size_t pos = s.find(';', start);
        if (pos == std::string::npos) {
            arr.Add(wxString::FromUTF8(s.substr(start)));
            break;
        }
        arr.Add(wxString::FromUTF8(s.substr(start, pos - start)));
        start = pos + 1;
    }
    return arr;
}

/* ═══════════════════════════════════════════════════════════════════
   extern "C" API implementation
   ═══════════════════════════════════════════════════════════════════ */

extern "C" {

/* ── App lifecycle ─────────────────────────────────────────────── */

void liva_ui_app_init(void) {
    if (g_appInitialized) return;

    // wxWidgets needs argc/argv
    static int s_argc = 1;
    static const char *s_argv[] = {"liva", nullptr};
    static char *s_argvMut[] = {const_cast<char *>(s_argv[0]), nullptr};

    g_app = new LivaApp();
    wxApp::SetInstance(g_app);
    wxEntryStart(s_argc, s_argvMut);
    g_app->CallOnInit();
    g_appInitialized = true;
}

void liva_ui_app_run(void) {
    if (!g_appInitialized) return;
    g_app->OnRun();
}

void liva_ui_app_quit(void) {
    if (!g_appInitialized) return;
    wxTheApp->ExitMainLoop();
}

/* ── Window (wxFrame) ──────────────────────────────────────────── */

int32_t liva_ui_create_window(int32_t w, int32_t h, const char *title) {
    auto *frame = new wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(title ? title : ""),
                              wxDefaultPosition, wxSize(w, h));
    return allocHandle(frame);
}

void liva_ui_window_show(int32_t handle, int32_t show) {
    if (auto *f = getHandle<wxFrame>(handle))
        f->Show(show != 0);
}

void liva_ui_window_set_title(int32_t handle, const char *title) {
    if (auto *f = getHandle<wxFrame>(handle))
        f->SetTitle(wxString::FromUTF8(title ? title : ""));
}

int32_t liva_ui_window_get_width(int32_t handle) {
    if (auto *f = getHandle<wxFrame>(handle))
        return f->GetClientSize().GetWidth();
    return 0;
}

int32_t liva_ui_window_get_height(int32_t handle) {
    if (auto *f = getHandle<wxFrame>(handle))
        return f->GetClientSize().GetHeight();
    return 0;
}

void liva_ui_window_on_close(int32_t handle, void *func, void *env, int32_t size) {
    auto *f = getHandle<wxFrame>(handle);
    if (!f || !func) return;
    void *owned = ownEnv(handle, env, size);
    LivaCallback cb{(LivaCallbackFn)func, owned, handle};
    f->Bind(wxEVT_CLOSE_WINDOW, [cb, handle, f](wxCloseEvent &evt) {
        freeWidgetEnvs(handle);
        freeFrameOwnedItemEnvs(f);  // sweep menu/tool item envs owned by this frame
        cb.invoke();
        evt.Skip();
    });
}

/* ── Widget creation ───────────────────────────────────────────── */

int32_t liva_ui_create_panel(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *panel = new wxPanel(p ? p : nullptr, wxID_ANY);
    return allocHandle(panel);
}

int32_t liva_ui_create_button(int32_t parent, const char *label) {
    auto *p = getHandle<wxWindow>(parent);
    auto *btn = new wxButton(p, wxID_ANY, wxString::FromUTF8(label ? label : ""));
    return allocHandle(btn);
}

int32_t liva_ui_create_label(int32_t parent, const char *text) {
    auto *p = getHandle<wxWindow>(parent);
    auto *lbl = new wxStaticText(p, wxID_ANY, wxString::FromUTF8(text ? text : ""));
    return allocHandle(lbl);
}

int32_t liva_ui_create_textinput(int32_t parent, const char *value) {
    auto *p = getHandle<wxWindow>(parent);
    auto *tc = new wxTextCtrl(p, wxID_ANY, wxString::FromUTF8(value ? value : ""));
    return allocHandle(tc);
}

int32_t liva_ui_create_checkbox(int32_t parent, const char *label) {
    auto *p = getHandle<wxWindow>(parent);
    auto *cb = new wxCheckBox(p, wxID_ANY, wxString::FromUTF8(label ? label : ""));
    return allocHandle(cb);
}

int32_t liva_ui_create_slider(int32_t parent, int32_t minVal, int32_t maxVal, int32_t val) {
    auto *p = getHandle<wxWindow>(parent);
    auto *sl = new wxSlider(p, wxID_ANY, val, minVal, maxVal);
    return allocHandle(sl);
}

int32_t liva_ui_create_progressbar(int32_t parent, int32_t range) {
    auto *p = getHandle<wxWindow>(parent);
    auto *g = new wxGauge(p, wxID_ANY, range);
    return allocHandle(g);
}

int32_t liva_ui_create_radiogroup(int32_t parent, const char *choices) {
    auto *p = getHandle<wxWindow>(parent);
    wxArrayString arr = splitChoices(choices);
    auto *rb = new wxRadioBox(p, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, arr, 0, wxRA_SPECIFY_ROWS);
    return allocHandle(rb);
}

int32_t liva_ui_create_dropdown(int32_t parent, const char *choices) {
    auto *p = getHandle<wxWindow>(parent);
    wxArrayString arr = splitChoices(choices);
    auto *ch = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, arr);
    if (!arr.IsEmpty()) ch->SetSelection(0);
    return allocHandle(ch);
}

int32_t liva_ui_create_textarea(int32_t parent, const char *value) {
    auto *p = getHandle<wxWindow>(parent);
    auto *tc = new wxTextCtrl(p, wxID_ANY, wxString::FromUTF8(value ? value : ""),
                              wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    return allocHandle(tc);
}

int32_t liva_ui_create_listbox(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *lb = new wxListBox(p, wxID_ANY);
    return allocHandle(lb);
}

int32_t liva_ui_create_tabview(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *nb = new wxNotebook(p, wxID_ANY);
    return allocHandle(nb);
}

int32_t liva_ui_create_scrollview(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *sw = new wxScrolledWindow(p, wxID_ANY);
    sw->SetScrollRate(5, 5);
    return allocHandle(sw);
}

int32_t liva_ui_create_imageview(int32_t parent, const char *path) {
    auto *p = getHandle<wxWindow>(parent);
    wxBitmap bmp;
    if (path) {
        wxImage img(wxString::FromUTF8(path), wxBITMAP_TYPE_ANY);
        if (img.IsOk()) bmp = wxBitmap(img);
    }
    auto *sb = new wxStaticBitmap(p, wxID_ANY, bmp);
    return allocHandle(sb);
}

int32_t liva_ui_create_divider(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *line = new wxStaticLine(p, wxID_ANY);
    return allocHandle(line);
}

/* ── Widget properties ─────────────────────────────────────────── */

void liva_ui_set_text(int32_t handle, const char *text) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
    wxString s = wxString::FromUTF8(text ? text : "");

    if (auto *tc = dynamic_cast<wxTextCtrl *>(w))
        tc->SetValue(s);
    else if (auto *st = dynamic_cast<wxStaticText *>(w))
        st->SetLabel(s);
    else if (auto *btn = dynamic_cast<wxButton *>(w))
        btn->SetLabel(s);
    else
        w->SetLabel(s);
}

const char *liva_ui_get_text(int32_t handle) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return "";

    if (auto *tc = dynamic_cast<wxTextCtrl *>(w))
        return returnTempStr(tc->GetValue());
    if (auto *combo = dynamic_cast<wxComboBox *>(w))
        return returnTempStr(combo->GetValue());
    return returnTempStr(w->GetLabel());
}

void liva_ui_set_value(int32_t handle, int32_t val) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;

    if (auto *sl = dynamic_cast<wxSlider *>(w))
        sl->SetValue(val);
    else if (auto *g = dynamic_cast<wxGauge *>(w))
        g->SetValue(val);
    else if (auto *cb = dynamic_cast<wxCheckBox *>(w))
        cb->SetValue(val != 0);
    else if (auto *spin = dynamic_cast<wxSpinCtrl *>(w))
        spin->SetValue(val);
}

int32_t liva_ui_get_value(int32_t handle) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return 0;

    if (auto *sl = dynamic_cast<wxSlider *>(w))
        return sl->GetValue();
    if (auto *g = dynamic_cast<wxGauge *>(w))
        return g->GetValue();
    if (auto *cb = dynamic_cast<wxCheckBox *>(w))
        return cb->GetValue() ? 1 : 0;
    if (auto *spin = dynamic_cast<wxSpinCtrl *>(w))
        return spin->GetValue();
    if (auto *combo = dynamic_cast<wxComboBox *>(w))
        return combo->GetSelection();
    return 0;
}

void liva_ui_set_enabled(int32_t handle, int32_t enabled) {
    if (auto *w = getHandle<wxWindow>(handle))
        w->Enable(enabled != 0);
}

void liva_ui_set_visible(int32_t handle, int32_t visible) {
    if (auto *w = getHandle<wxWindow>(handle))
        w->Show(visible != 0);
}

void liva_ui_set_size(int32_t handle, int32_t w, int32_t h) {
    if (auto *win = getHandle<wxWindow>(handle))
        win->SetMinSize(wxSize(w, h));
}

void liva_ui_set_font(int32_t handle, int32_t size, int32_t bold) {
    if (auto *w = getHandle<wxWindow>(handle)) {
        wxFont font(size, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                    bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        w->SetFont(font);
    }
}

void liva_ui_set_bg_color(int32_t handle, int32_t r, int32_t g, int32_t b) {
    if (auto *w = getHandle<wxWindow>(handle)) {
        w->SetBackgroundColour(wxColour(r, g, b));
        w->Refresh();
    }
}

void liva_ui_set_fg_color(int32_t handle, int32_t r, int32_t g, int32_t b) {
    if (auto *w = getHandle<wxWindow>(handle)) {
        w->SetForegroundColour(wxColour(r, g, b));
        w->Refresh();
    }
}

void liva_ui_set_tooltip(int32_t handle, const char *text) {
    if (auto *w = getHandle<wxWindow>(handle))
        w->SetToolTip(wxString::FromUTF8(text ? text : ""));
}

/* Phase 4 layout state — forward-declared here so liva_ui_destroy_widget can
   clean it up; the layout engine functions are defined at end of file. */
struct LivaLayout {
    int  align = 0;                 // 0=none,1=top,2=bottom,3=left,4=right,5=client
    bool aLeft = true, aTop = true, aRight = false, aBottom = false;  // VCL default
    wxRect refBounds;               // anchor referans dikdörtgeni
    wxSize refParentClient;         // anchor referans ebeveyn istemci boyutu
    bool   hasRef = false;
};
// Anahtar wxWindow* (handle değil): relayout doğrudan GetChildren() pointer'larıyla bakar.
static std::unordered_map<wxWindow *, LivaLayout> g_layouts;
static std::unordered_set<wxWindow *> g_layoutParents;

void liva_ui_destroy_widget(int32_t handle) {
    freeWidgetEnvs(handle);
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
    g_layouts.erase(w);
    g_layoutParents.erase(w);
    w->Destroy();
    g_handles.erase(handle);
}

/* ── Layout (wxSizer) ──────────────────────────────────────────── */

int32_t liva_ui_create_vbox_sizer(void) {
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    return allocSizerHandle(sizer);
}

int32_t liva_ui_create_hbox_sizer(void) {
    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    return allocSizerHandle(sizer);
}

int32_t liva_ui_create_grid_sizer(int32_t rows, int32_t cols, int32_t hgap, int32_t vgap) {
    auto *sizer = new wxGridSizer(rows, cols, vgap, hgap);
    return allocSizerHandle(sizer);
}

int32_t liva_ui_create_flex_grid_sizer(int32_t rows, int32_t cols, int32_t hgap, int32_t vgap) {
    auto *sizer = new wxFlexGridSizer(rows, cols, vgap, hgap);
    return allocSizerHandle(sizer);
}

void liva_ui_sizer_add(int32_t sizerH, int32_t widgetH, int32_t proportion, int32_t flags, int32_t border) {
    auto *sizer = getSizer(sizerH);
    if (!sizer) return;

    // widget could be a window or another sizer
    if (auto *w = getHandle<wxWindow>(widgetH)) {
        sizer->Add(w, proportion, flags, border);
    } else if (auto *childSizer = getSizer(widgetH)) {
        sizer->Add(childSizer, proportion, flags, border);
    }
}

void liva_ui_set_sizer(int32_t parentH, int32_t sizerH) {
    auto *parent = getHandle<wxWindow>(parentH);
    auto *sizer = getSizer(sizerH);
    if (parent && sizer) {
        parent->SetSizer(sizer);
        parent->Layout();
    }
}

/* ── Events ────────────────────────────────────────────────────── */

void liva_ui_on_click(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    LivaCallback cb{(LivaCallbackFn)func, owned, handle};

    if (dynamic_cast<wxButton *>(w)) {
        w->Bind(wxEVT_BUTTON, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxCheckBox *>(w)) {
        w->Bind(wxEVT_CHECKBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else {
        w->Bind(wxEVT_LEFT_UP, [cb](wxMouseEvent &) { cb.invoke(); });
    }
}

void liva_ui_on_change(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    LivaCallback cb{(LivaCallbackFn)func, owned, handle};

    if (dynamic_cast<wxTextCtrl *>(w)) {
        w->Bind(wxEVT_TEXT, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxSlider *>(w)) {
        w->Bind(wxEVT_SLIDER, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxCheckBox *>(w)) {
        w->Bind(wxEVT_CHECKBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxSpinCtrl *>(w)) {
        w->Bind(wxEVT_SPINCTRL, [cb](wxSpinEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxDatePickerCtrl *>(w)) {
        w->Bind(wxEVT_DATE_CHANGED, [cb](wxDateEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxComboBox *>(w)) {
        w->Bind(wxEVT_TEXT, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (auto *grid = dynamic_cast<wxGrid *>(w)) {
        grid->Bind(wxEVT_GRID_CELL_CHANGED, [cb](wxGridEvent &) { cb.invoke(); });
    }
}

void liva_ui_on_select(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    LivaCallback cb{(LivaCallbackFn)func, owned, handle};

    if (dynamic_cast<wxComboBox *>(w)) {
        // wxComboBox publicly derives from wxChoice on MSW, so this branch
        // MUST be checked before wxChoice (derived-before-base) — otherwise
        // combo selections would bind wxEVT_CHOICE and never fire onSelect.
        w->Bind(wxEVT_COMBOBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxChoice *>(w)) {
        w->Bind(wxEVT_CHOICE, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxListBox *>(w)) {
        w->Bind(wxEVT_LISTBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxRadioBox *>(w)) {
        w->Bind(wxEVT_RADIOBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxNotebook *>(w)) {
        w->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [cb](wxBookCtrlEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxTreeCtrl *>(w)) {
        w->Bind(wxEVT_TREE_SEL_CHANGED, [cb](wxTreeEvent &) { cb.invoke(); });
    }
}

void liva_ui_on_key(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    auto fn = (LivaKeyFn)func;
    w->Bind(wxEVT_KEY_DOWN, [fn, owned, handle](wxKeyEvent &evt) {
        if (fn) fn(owned, handle, evt.GetKeyCode());
        evt.Skip();
    });
}

/* ── List / Tab operations ─────────────────────────────────────── */

void liva_ui_list_add_item(int32_t handle, const char *item) {
    if (auto *lb = getHandle<wxListBox>(handle))
        lb->Append(wxString::FromUTF8(item ? item : ""));
}

void liva_ui_list_clear(int32_t handle) {
    if (auto *lb = getHandle<wxListBox>(handle))
        lb->Clear();
}

int32_t liva_ui_list_get_selection(int32_t handle) {
    if (auto *lb = getHandle<wxListBox>(handle))
        return lb->GetSelection();
    return -1;
}

void liva_ui_tab_add_page(int32_t tabHandle, int32_t pageHandle, const char *title) {
    auto *nb = getHandle<wxNotebook>(tabHandle);
    auto *page = getHandle<wxWindow>(pageHandle);
    if (nb && page)
        nb->AddPage(page, wxString::FromUTF8(title ? title : ""));
}

int32_t liva_ui_tab_get_selection(int32_t handle) {
    if (auto *nb = getHandle<wxNotebook>(handle))
        return nb->GetSelection();
    return -1;
}

/* ── Dialogs ───────────────────────────────────────────────────── */

void liva_ui_message_box(const char *title, const char *message, int32_t style) {
    long wxStyle = wxOK;
    if (style == 1) wxStyle = wxOK | wxICON_INFORMATION;
    else if (style == 2) wxStyle = wxOK | wxICON_WARNING;
    else if (style == 3) wxStyle = wxOK | wxICON_ERROR;
    else if (style == 4) wxStyle = wxYES_NO | wxICON_QUESTION;

    wxMessageBox(wxString::FromUTF8(message ? message : ""),
                 wxString::FromUTF8(title ? title : ""),
                 wxStyle);
}

const char *liva_ui_file_dialog(int32_t parent, const char *title,
                                 const char *wildcard, int32_t style) {
    auto *p = getHandle<wxWindow>(parent);
    long wxStyle = (style == 1) ? wxFD_SAVE : wxFD_OPEN;

    wxFileDialog dlg(p,
                     wxString::FromUTF8(title ? title : "Choose a file"),
                     wxEmptyString, wxEmptyString,
                     wxString::FromUTF8(wildcard ? wildcard : "*.*"),
                     wxStyle);

    if (dlg.ShowModal() == wxID_OK)
        return returnTempStr(dlg.GetPath());
    return "";
}

int32_t liva_ui_color_dialog(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    wxColourDialog dlg(p);
    if (dlg.ShowModal() == wxID_OK) {
        wxColour c = dlg.GetColourData().GetColour();
        return (c.Red() << 16) | (c.Green() << 8) | c.Blue();
    }
    return 0;
}

/* ── Timer ─────────────────────────────────────────────────────── */

int32_t liva_ui_create_timer(int32_t intervalMs, void *func, void *env) {
    int32_t h = g_nextHandle++;
    auto *timer = new LivaTimer((LivaCallbackFn)func, env, h);
    g_handles[h] = timer;
    timer->Start(intervalMs);
    return h;
}

void liva_ui_stop_timer(int32_t handle) {
    auto it = g_handles.find(handle);
    if (it == g_handles.end()) return;
    auto *timer = static_cast<LivaTimer *>(it->second);
    timer->Stop();
    delete timer;
    g_handles.erase(it);
}

/* ── Clipboard ─────────────────────────────────────────────────── */

const char *liva_ui_get_clipboard_text(void) {
    if (wxTheClipboard->Open()) {
        wxTextDataObject data;
        if (wxTheClipboard->GetData(data)) {
            wxTheClipboard->Close();
            return returnTempStr(data.GetText());
        }
        wxTheClipboard->Close();
    }
    return "";
}

void liva_ui_set_clipboard_text(const char *text) {
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text ? text : "")));
        wxTheClipboard->Close();
    }
}

/* ── Canvas / custom drawing ───────────────────────────────────── */

int32_t liva_ui_create_canvas(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *canvas = new LivaCanvas(p);
    return allocHandle(canvas);
}

void liva_ui_canvas_on_paint(int32_t handle, void *func, void *env, int32_t size) {
    auto *canvas = getHandle<LivaCanvas>(handle);
    if (!canvas || !func) return;
    void *owned = ownEnv(handle, env, size);
    canvas->SetPaintCallback((LivaPaintFn)func, owned);
}

void liva_ui_canvas_refresh(int32_t handle) {
    if (auto *w = getHandle<wxWindow>(handle))
        w->Refresh();
}

void liva_ui_dc_clear(int32_t dcH, int32_t r, int32_t g, int32_t b) {
    if (auto *dc = getDcHandle(dcH)) {
        dc->SetBackground(wxBrush(wxColour(r, g, b)));
        dc->Clear();
    }
}

void liva_ui_dc_draw_rect(int32_t dcH, int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t r, int32_t g, int32_t b) {
    if (auto *dc = getDcHandle(dcH)) {
        dc->SetBrush(wxBrush(wxColour(r, g, b)));
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawRectangle(x, y, w, h);
    }
}

void liva_ui_dc_draw_text(int32_t dcH, const char *text, int32_t x, int32_t y,
                           int32_t r, int32_t g, int32_t b) {
    if (auto *dc = getDcHandle(dcH)) {
        dc->SetTextForeground(wxColour(r, g, b));
        dc->DrawText(wxString::FromUTF8(text ? text : ""), x, y);
    }
}

void liva_ui_dc_draw_line(int32_t dcH, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                           int32_t r, int32_t g, int32_t b) {
    if (auto *dc = getDcHandle(dcH)) {
        dc->SetPen(wxPen(wxColour(r, g, b)));
        dc->DrawLine(x1, y1, x2, y2);
    }
}

void liva_ui_dc_draw_circle(int32_t dcH, int32_t cx, int32_t cy, int32_t radius,
                             int32_t r, int32_t g, int32_t b) {
    if (auto *dc = getDcHandle(dcH)) {
        dc->SetBrush(wxBrush(wxColour(r, g, b)));
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawCircle(cx, cy, radius);
    }
}

/* ── Geometry ──────────────────────────────────────────────────────── */

void liva_ui_set_bounds(int32_t handle, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (auto *win = getHandle<wxWindow>(handle))
        win->SetSize(x, y, w, h);
}

/* ── Menu ──────────────────────────────────────────────────────────── */

int32_t liva_ui_create_menu_bar(void) {
    return allocHandle(new wxMenuBar());
}

int32_t liva_ui_create_menu(const char *title) {
    auto *m = new wxMenu();
    int32_t h = allocHandle(m);
    g_menuTitles()[h] = wxString::FromUTF8(title ? title : "");
    return h;
}

int32_t liva_ui_menu_add_item(int32_t menu, const char *label) {
    auto *m = getHandle<wxMenu>(menu);
    if (!m) return 0;
    int id = g_nextCmdId++;
    auto *item = m->Append(id, wxString::FromUTF8(label ? label : ""));
    int32_t h = allocHandle(item);
    LivaMenuItem mi;
    mi.item = item; mi.id = id; mi.ownerMenu = m;
    g_menuItems[h] = mi;
    return h;
}

int32_t liva_ui_menu_add_check_item(int32_t menu, const char *label) {
    auto *m = getHandle<wxMenu>(menu);
    if (!m) return 0;
    int id = g_nextCmdId++;
    auto *item = m->AppendCheckItem(id, wxString::FromUTF8(label ? label : ""));
    int32_t h = allocHandle(item);
    LivaMenuItem mi;
    mi.item = item; mi.id = id; mi.ownerMenu = m;
    g_menuItems[h] = mi;
    return h;
}

void liva_ui_menu_add_separator(int32_t menu) {
    if (auto *m = getHandle<wxMenu>(menu)) m->AppendSeparator();
}

void liva_ui_menu_add_submenu(int32_t menu, const char *label, int32_t sub) {
    auto *m = getHandle<wxMenu>(menu);
    auto *s = getHandle<wxMenu>(sub);
    if (m && s) m->AppendSubMenu(s, wxString::FromUTF8(label ? label : ""));
}

void liva_ui_menu_bar_add_menu(int32_t bar, int32_t menu) {
    auto *mb = getHandle<wxMenuBar>(bar);
    auto *m = getHandle<wxMenu>(menu);
    if (mb && m) mb->Append(m, g_menuTitles()[menu]);
}

void liva_ui_window_set_menu_bar(int32_t window, int32_t bar) {
    auto *f = getHandle<wxFrame>(window);
    auto *mb = getHandle<wxMenuBar>(bar);
    if (!f || !mb) return;
    f->SetMenuBar(mb);
    for (auto &kv : g_menuItems) {
        auto &mi = kv.second;
        if (mi.hasPending && mi.ownerFrame == nullptr) {
            mi.ownerFrame = f;
            LivaCallbackFn fn = mi.pendingFn;
            void *env = mi.pendingEnv;
            int32_t ih = kv.first;
            f->Bind(wxEVT_MENU,
                    [fn, env, ih](wxCommandEvent &) { if (fn) fn(env, ih); },
                    mi.id);
            mi.hasPending = false;
        }
    }
}

void liva_ui_menu_item_set_enabled(int32_t item, int32_t enabled) {
    auto it = g_menuItems.find(item);
    if (it != g_menuItems.end() && it->second.item)
        it->second.item->Enable(enabled != 0);
}

void liva_ui_menu_item_set_checked(int32_t item, int32_t checked) {
    auto it = g_menuItems.find(item);
    if (it != g_menuItems.end() && it->second.item && it->second.item->IsCheckable())
        it->second.item->Check(checked != 0);
}

void liva_ui_menu_item_on_click(int32_t item, void *func, void *env, int32_t size) {
    auto it = g_menuItems.find(item);
    if (it == g_menuItems.end() || !func) return;
    void *owned = ownEnv(item, env, size);
    auto fn = (LivaCallbackFn)func;
    auto &mi = it->second;
    if (mi.ownerFrame) {
        int32_t ih = item;
        mi.ownerFrame->Bind(wxEVT_MENU,
            [fn, owned, ih](wxCommandEvent &) { if (fn) fn(owned, ih); }, mi.id);
    } else {
        mi.pendingFn = fn;
        mi.pendingEnv = owned;
        mi.hasPending = true;
    }
}

void liva_ui_menu_popup(int32_t menu, int32_t target) {
    auto *m = getHandle<wxMenu>(menu);
    auto *w = getHandle<wxWindow>(target);
    if (!m || !w) return;
    // Flush only THIS menu's pending item bindings onto the target window, so
    // popping one context menu doesn't steal another menu's pending items.
    for (auto &kv : g_menuItems) {
        auto &mi = kv.second;
        if (mi.hasPending && mi.ownerFrame == nullptr && mi.ownerMenu == m) {
            LivaCallbackFn fn = mi.pendingFn;
            void *env = mi.pendingEnv;
            int32_t ih = kv.first;
            w->Bind(wxEVT_MENU,
                    [fn, env, ih](wxCommandEvent &) { if (fn) fn(env, ih); }, mi.id);
            mi.hasPending = false;
        }
    }
    // PopupMenu is synchronous: it returns after the menu is dismissed and any
    // selected item's handler has run. A popup menu (and its items) is
    // typically built fresh per right-click, so free its item envs and drop its
    // handle entries here to avoid a per-popup leak.
    w->PopupMenu(m);
    for (auto it = g_menuItems.begin(); it != g_menuItems.end();) {
        if (it->second.ownerMenu == m) {
            freeWidgetEnvs(it->first);
            it = g_menuItems.erase(it);
        } else {
            ++it;
        }
    }
}

/* ── Context menu ──────────────────────────────────────────────────── */

void liva_ui_on_right_click(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    auto fn = (LivaXYFn)func;
    w->Bind(wxEVT_CONTEXT_MENU, [fn, owned, handle, w](wxContextMenuEvent &evt) {
        wxPoint p = evt.GetPosition();
        if (p == wxDefaultPosition) p = ::wxGetMousePosition();
        wxPoint cli = w->ScreenToClient(p);
        if (fn) fn(owned, handle, cli.x, cli.y);
    });
}

/* ── StatusBar ─────────────────────────────────────────────────────── */

int32_t liva_ui_create_status_bar(int32_t window, int32_t field_count) {
    auto *f = getHandle<wxFrame>(window);
    if (!f) return 0;
    wxStatusBar *sb = f->CreateStatusBar(field_count > 0 ? field_count : 1);
    return allocHandle(sb);
}

void liva_ui_status_bar_set_text(int32_t sb, int32_t field, const char *text) {
    if (auto *s = getHandle<wxStatusBar>(sb))
        s->SetStatusText(wxString::FromUTF8(text ? text : ""), field);
}

/* ── Toolbar ───────────────────────────────────────────────────────── */

int32_t liva_ui_create_toolbar(int32_t window) {
    auto *f = getHandle<wxFrame>(window);
    if (!f) return 0;
    // Show text labels: Phase 2 tools are label-only (no custom icons), so the
    // toolbar must display the text. We still give each tool a valid bitmap
    // below because wxToolBar::Realize() asserts on a null tool bitmap.
    wxToolBar *tb = f->CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT);
    return allocHandle(tb);
}

int32_t liva_ui_toolbar_add_tool(int32_t tb, const char *label) {
    auto *t = getHandle<wxToolBar>(tb);
    if (!t) return 0;
    int id = g_nextCmdId++;
    wxString lbl = wxString::FromUTF8(label ? label : "");
    // wxToolBar requires a valid bitmap; use a generic stock icon as a
    // placeholder (no per-tool icon support in Phase 2). Size it to the
    // toolbar's configured tool bitmap size so Realize() is happy.
    wxBitmap bmp = wxArtProvider::GetBitmap(wxART_NEW, wxART_TOOLBAR,
                                            t->GetToolBitmapSize());
    if (!bmp.IsOk()) {
        // Fall back to a 16x16 transparent bitmap if the art provider fails.
        wxSize sz = t->GetToolBitmapSize();
        if (sz.x <= 0 || sz.y <= 0) sz = wxSize(16, 16);
        wxBitmap blank(sz);
        wxMemoryDC dc(blank);
        dc.SetBackground(*wxTRANSPARENT_BRUSH);
        dc.Clear();
        dc.SelectObject(wxNullBitmap);
        bmp = blank;
    }
    auto *tool = t->AddTool(id, lbl, bmp, lbl);
    int32_t h = allocHandle(tool);
    g_toolItems[h] = LivaToolItem{tool, id, t};
    return h;
}

void liva_ui_toolbar_add_separator(int32_t tb) {
    if (auto *t = getHandle<wxToolBar>(tb)) t->AddSeparator();
}

void liva_ui_toolbar_realize(int32_t tb) {
    if (auto *t = getHandle<wxToolBar>(tb)) t->Realize();
}

void liva_ui_tool_item_set_enabled(int32_t tool, int32_t enabled) {
    auto it = g_toolItems.find(tool);
    if (it != g_toolItems.end() && it->second.toolbar)
        it->second.toolbar->EnableTool(it->second.id, enabled != 0);
}

void liva_ui_tool_item_on_click(int32_t tool, void *func, void *env, int32_t size) {
    auto it = g_toolItems.find(tool);
    if (it == g_toolItems.end() || !func) return;
    void *owned = ownEnv(tool, env, size);
    auto fn = (LivaCallbackFn)func;
    auto &ti = it->second;
    int32_t th = tool;
    if (ti.toolbar)
        ti.toolbar->Bind(wxEVT_TOOL,
            [fn, owned, th](wxCommandEvent &) { if (fn) fn(owned, th); }, ti.id);
}

/* ═══════════════════════════════════════════════════════════════════
   Phase 3: new widgets
   ═══════════════════════════════════════════════════════════════════ */

int32_t liva_ui_create_spin_ctrl(int32_t parent, int32_t minVal, int32_t maxVal, int32_t val) {
    auto *p = getHandle<wxWindow>(parent);
    auto *sc = new wxSpinCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxSP_ARROW_KEYS, minVal, maxVal, val);
    return allocHandle(sc);
}

int32_t liva_ui_create_date_picker(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *dp = new wxDatePickerCtrl(p, wxID_ANY);
    return allocHandle(dp);
}

const char *liva_ui_date_get_value(int32_t handle) {
    auto *dp = getHandle<wxDatePickerCtrl>(handle);
    if (!dp) return "";
    wxDateTime d = dp->GetValue();
    if (!d.IsValid()) return "";
    return returnTempStr(d.FormatISODate());
}

int32_t liva_ui_create_combo_box(int32_t parent, const char *value) {
    auto *p = getHandle<wxWindow>(parent);
    auto *cb = new wxComboBox(p, wxID_ANY, wxString::FromUTF8(value ? value : ""));
    return allocHandle(cb);
}

void liva_ui_combo_add_item(int32_t handle, const char *item) {
    if (auto *cb = getHandle<wxComboBox>(handle))
        cb->Append(wxString::FromUTF8(item ? item : ""));
}

int32_t liva_ui_create_tree_view(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *tree = new wxTreeCtrl(p, wxID_ANY);
    return allocHandle(tree);
}

int32_t liva_ui_tree_add_root(int32_t handle, const char *label) {
    auto *tree = getHandle<wxTreeCtrl>(handle);
    if (!tree) return 0;
    wxTreeItemId id = tree->AddRoot(wxString::FromUTF8(label ? label : ""));
    int32_t nh = g_nextHandle++;
    g_treeNodes[nh] = id;
    return nh;
}

int32_t liva_ui_tree_add_node(int32_t handle, int32_t parentNode, const char *label) {
    auto *tree = getHandle<wxTreeCtrl>(handle);
    if (!tree) return 0;
    auto it = g_treeNodes.find(parentNode);
    if (it == g_treeNodes.end()) return 0;
    wxTreeItemId id = tree->AppendItem(it->second, wxString::FromUTF8(label ? label : ""));
    int32_t nh = g_nextHandle++;
    g_treeNodes[nh] = id;
    return nh;
}

int32_t liva_ui_tree_get_selection(int32_t handle) {
    auto *tree = getHandle<wxTreeCtrl>(handle);
    if (!tree) return 0;
    wxTreeItemId sel = tree->GetSelection();
    if (!sel.IsOk()) return 0;
    for (auto &kv : g_treeNodes)
        if (kv.second == sel) return kv.first;
    return 0;
}

int32_t liva_ui_create_data_grid(int32_t parent, int32_t rows, int32_t cols) {
    auto *p = getHandle<wxWindow>(parent);
    auto *grid = new wxGrid(p, wxID_ANY);
    grid->CreateGrid(rows, cols);
    return allocHandle(grid);
}

void liva_ui_grid_set_cell(int32_t handle, int32_t row, int32_t col, const char *text) {
    if (auto *grid = getHandle<wxGrid>(handle))
        grid->SetCellValue(row, col, wxString::FromUTF8(text ? text : ""));
}

const char *liva_ui_grid_get_cell(int32_t handle, int32_t row, int32_t col) {
    auto *grid = getHandle<wxGrid>(handle);
    if (!grid) return "";
    return returnTempStr(grid->GetCellValue(row, col));
}

void liva_ui_grid_set_col_label(int32_t handle, int32_t col, const char *text) {
    if (auto *grid = getHandle<wxGrid>(handle))
        grid->SetColLabelValue(col, wxString::FromUTF8(text ? text : ""));
}

int32_t liva_ui_create_splitter(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *sp = new wxSplitterWindow(p, wxID_ANY);
    return allocHandle(sp);
}

void liva_ui_splitter_split_v(int32_t handle, int32_t left, int32_t right) {
    auto *sp = getHandle<wxSplitterWindow>(handle);
    auto *l = getHandle<wxWindow>(left);
    auto *r = getHandle<wxWindow>(right);
    if (sp && l && r) sp->SplitVertically(l, r);
}

void liva_ui_splitter_split_h(int32_t handle, int32_t top, int32_t bottom) {
    auto *sp = getHandle<wxSplitterWindow>(handle);
    auto *t = getHandle<wxWindow>(top);
    auto *b = getHandle<wxWindow>(bottom);
    if (sp && t && b) sp->SplitHorizontally(t, b);
}

void liva_ui_splitter_set_sash(int32_t handle, int32_t px) {
    if (auto *sp = getHandle<wxSplitterWindow>(handle))
        sp->SetSashPosition(px);
}

/* ═══════════════════════════════════════════════════════════════════
   Phase 4: Align/Anchors layout (VCL AlignControls)
   ═══════════════════════════════════════════════════════════════════ */

static void relayoutParent(wxWindow *parent);   // forward

// Çocuğun mevcut rect'ini + ebeveyn istemci boyutunu anchor referansı olarak yakala.
static void captureLayoutRef(wxWindow *w) {
    LivaLayout &L = g_layouts[w];
    L.refBounds = w->GetRect();
    if (wxWindow *p = w->GetParent()) {
        L.refParentClient = p->GetClientSize();
        L.hasRef = true;
    }
}

// Ebeveynin yeniden boyutlanınca VCL yerleşimini çalıştırmasını sağla (bir kez bağla).
static void ensureParentBound(wxWindow *parent) {
    if (!parent || g_layoutParents.count(parent)) return;
    if (parent->GetSizer()) parent->SetSizer(nullptr, false);  // ebeveyn başına biri
    parent->Bind(wxEVT_SIZE, [parent](wxSizeEvent &e) {
        relayoutParent(parent);
        e.Skip();
    });
    g_layoutParents.insert(parent);
}

// VCL AlignControls: kenar-dock çocuklar istemci dikdörtgenini tüketir; sonra
// 'none' çocuklar anchor'lara göre konumlanır (anchor matematiği Task 2'de).
static void relayoutParent(wxWindow *parent) {
    if (!parent) return;
    wxSize cs = parent->GetClientSize();
    wxRect R(0, 0, cs.x, cs.y);

    // 1) Kenar-dock (top, bottom, left, right) — z-sırasında.
    for (wxWindow *child : parent->GetChildren()) {
        auto it = g_layouts.find(child);
        if (it == g_layouts.end()) continue;
        wxSize sz = child->GetSize();
        switch (it->second.align) {
            case 1: child->SetSize(R.x, R.y, R.width, sz.y);
                    R.y += sz.y; R.height -= sz.y; break;             // top
            case 2: child->SetSize(R.x, R.y + R.height - sz.y, R.width, sz.y);
                    R.height -= sz.y; break;                          // bottom
            case 3: child->SetSize(R.x, R.y, sz.x, R.height);
                    R.x += sz.x; R.width -= sz.x; break;              // left
            case 4: child->SetSize(R.x + R.width - sz.x, R.y, sz.x, R.height);
                    R.width -= sz.x; break;                           // right
            default: break;
        }
    }
    // 2) client: kalan R'yi doldur (yalnız ilki).
    bool clientUsed = false;
    for (wxWindow *child : parent->GetChildren()) {
        auto it = g_layouts.find(child);
        if (it != g_layouts.end() && it->second.align == 5 && !clientUsed) {
            child->SetSize(R.x, R.y, R.width, R.height);
            clientUsed = true;
        }
    }
    // 3) 'none' çocuklar: anchors (refBounds + ebeveyn boyut deltası).
    for (wxWindow *child : parent->GetChildren()) {
        auto it = g_layouts.find(child);
        if (it == g_layouts.end()) continue;        // dokunulmamış → sabit
        LivaLayout &L = it->second;
        if (L.align != 0 || !L.hasRef) continue;
        int dw = cs.x - L.refParentClient.x;
        int dh = cs.y - L.refParentClient.y;
        int nx = L.refBounds.x, nw = L.refBounds.width;
        int ny = L.refBounds.y, nh = L.refBounds.height;
        if (L.aLeft && L.aRight)        nw = L.refBounds.width + dw;   // yatay esner
        else if (!L.aLeft && L.aRight)  nx = L.refBounds.x + dw;       // sag kenarla
        else if (!L.aLeft && !L.aRight) nx = L.refBounds.x + dw / 2;   // orantili
        if (L.aTop && L.aBottom)        nh = L.refBounds.height + dh;  // dikey esner
        else if (!L.aTop && L.aBottom)  ny = L.refBounds.y + dh;       // alt kenarla
        else if (!L.aTop && !L.aBottom) ny = L.refBounds.y + dh / 2;   // orantili
        child->SetSize(nx, ny, nw, nh);
    }
}

void liva_ui_set_align(int32_t handle, int32_t align) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
    g_layouts[w].align = align;
    captureLayoutRef(w);
    wxWindow *parent = w->GetParent();
    ensureParentBound(parent);
    relayoutParent(parent);
}

void liva_ui_set_anchors(int32_t handle, int32_t left, int32_t top,
                         int32_t right, int32_t bottom) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
    LivaLayout &L = g_layouts[w];
    L.aLeft = left != 0; L.aTop = top != 0;
    L.aRight = right != 0; L.aBottom = bottom != 0;
    captureLayoutRef(w);
    wxWindow *parent = w->GetParent();
    ensureParentBound(parent);
    relayoutParent(parent);
}

} // extern "C"
