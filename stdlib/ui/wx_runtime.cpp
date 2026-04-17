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

#include <unordered_map>
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
   Liva callback wrapper
   ═══════════════════════════════════════════════════════════════════ */

using LivaCallbackFn = void (*)(void *env, int32_t handle);

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

void liva_ui_window_on_close(int32_t handle, void *func, void *env) {
    auto *f = getHandle<wxFrame>(handle);
    if (!f || !func) return;
    LivaCallback cb{(LivaCallbackFn)func, env, handle};
    f->Bind(wxEVT_CLOSE_WINDOW, [cb](wxCloseEvent &evt) {
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

void liva_ui_destroy_widget(int32_t handle) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
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

void liva_ui_on_click(int32_t handle, void *func, void *env) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    LivaCallback cb{(LivaCallbackFn)func, env, handle};

    if (dynamic_cast<wxButton *>(w)) {
        w->Bind(wxEVT_BUTTON, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxCheckBox *>(w)) {
        w->Bind(wxEVT_CHECKBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else {
        w->Bind(wxEVT_LEFT_UP, [cb](wxMouseEvent &) { cb.invoke(); });
    }
}

void liva_ui_on_change(int32_t handle, void *func, void *env) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    LivaCallback cb{(LivaCallbackFn)func, env, handle};

    if (dynamic_cast<wxTextCtrl *>(w)) {
        w->Bind(wxEVT_TEXT, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxSlider *>(w)) {
        w->Bind(wxEVT_SLIDER, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxCheckBox *>(w)) {
        w->Bind(wxEVT_CHECKBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    }
}

void liva_ui_on_select(int32_t handle, void *func, void *env) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    LivaCallback cb{(LivaCallbackFn)func, env, handle};

    if (dynamic_cast<wxChoice *>(w)) {
        w->Bind(wxEVT_CHOICE, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxListBox *>(w)) {
        w->Bind(wxEVT_LISTBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxRadioBox *>(w)) {
        w->Bind(wxEVT_RADIOBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    } else if (dynamic_cast<wxNotebook *>(w)) {
        w->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [cb](wxBookCtrlEvent &) { cb.invoke(); });
    }
}

void liva_ui_on_key(int32_t handle, void *func, void *env) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    LivaCallback cb{(LivaCallbackFn)func, env, handle};
    w->Bind(wxEVT_KEY_DOWN, [cb](wxKeyEvent &evt) {
        cb.invoke();
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

void liva_ui_canvas_on_paint(int32_t handle, void *func, void *env) {
    auto *canvas = getHandle<LivaCanvas>(handle);
    if (canvas && func)
        canvas->SetPaintCallback((LivaPaintFn)func, env);
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

} // extern "C"
