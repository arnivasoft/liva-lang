/**
 * wx_runtime.h — Liva UI runtime (wxWidgets backend)
 *
 * extern "C" wrapper functions around wxWidgets C++ API.
 * All widget objects are stored as int32_t handles in a global handle table.
 * Callbacks receive Liva closure pairs: (func_ptr, env_ptr).
 */

#ifndef LIVA_WX_RUNTIME_H
#define LIVA_WX_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── App lifecycle ─────────────────────────────────────────────── */
void     liva_ui_app_init(void);
void     liva_ui_app_run(void);
void     liva_ui_app_quit(void);

/* ── Window (wxFrame) ──────────────────────────────────────────── */
int32_t  liva_ui_create_window(int32_t w, int32_t h, const char *title);
void     liva_ui_window_show(int32_t handle, int32_t show);
void     liva_ui_window_set_title(int32_t handle, const char *title);
int32_t  liva_ui_window_get_width(int32_t handle);
int32_t  liva_ui_window_get_height(int32_t handle);
void     liva_ui_window_on_close(int32_t handle, void *func, void *env, int32_t size);

/* ── Widget creation ───────────────────────────────────────────── */
int32_t  liva_ui_create_panel(int32_t parent);
int32_t  liva_ui_create_button(int32_t parent, const char *label);
int32_t  liva_ui_create_label(int32_t parent, const char *text);
int32_t  liva_ui_create_textinput(int32_t parent, const char *value);
int32_t  liva_ui_create_checkbox(int32_t parent, const char *label);
int32_t  liva_ui_create_slider(int32_t parent, int32_t minVal, int32_t maxVal, int32_t val);
int32_t  liva_ui_create_progressbar(int32_t parent, int32_t range);
int32_t  liva_ui_create_radiogroup(int32_t parent, const char *choices);
int32_t  liva_ui_create_dropdown(int32_t parent, const char *choices);
int32_t  liva_ui_create_textarea(int32_t parent, const char *value);
int32_t  liva_ui_create_listbox(int32_t parent);
int32_t  liva_ui_create_tabview(int32_t parent);
int32_t  liva_ui_create_scrollview(int32_t parent);
int32_t  liva_ui_create_imageview(int32_t parent, const char *path);
int32_t  liva_ui_create_divider(int32_t parent);

/* ── Widget properties ─────────────────────────────────────────── */
void         liva_ui_set_text(int32_t handle, const char *text);
const char  *liva_ui_get_text(int32_t handle);
void         liva_ui_set_value(int32_t handle, int32_t val);
int32_t      liva_ui_get_value(int32_t handle);
void         liva_ui_set_enabled(int32_t handle, int32_t enabled);
void         liva_ui_set_visible(int32_t handle, int32_t visible);
void         liva_ui_set_size(int32_t handle, int32_t w, int32_t h);
void         liva_ui_set_font(int32_t handle, int32_t size, int32_t bold);
void         liva_ui_set_bg_color(int32_t handle, int32_t r, int32_t g, int32_t b);
void         liva_ui_set_fg_color(int32_t handle, int32_t r, int32_t g, int32_t b);
void         liva_ui_set_tooltip(int32_t handle, const char *text);
void         liva_ui_destroy_widget(int32_t handle);

/* ── Layout (wxSizer) ──────────────────────────────────────────── */
int32_t  liva_ui_create_vbox_sizer(void);
int32_t  liva_ui_create_hbox_sizer(void);
int32_t  liva_ui_create_grid_sizer(int32_t rows, int32_t cols, int32_t hgap, int32_t vgap);
int32_t  liva_ui_create_flex_grid_sizer(int32_t rows, int32_t cols, int32_t hgap, int32_t vgap);
void     liva_ui_sizer_add(int32_t sizer, int32_t widget, int32_t proportion, int32_t flags, int32_t border);
void     liva_ui_set_sizer(int32_t parent, int32_t sizer);

/* ── Events (closure callbacks) — env + size for heap-owned envs ──── */
void     liva_ui_on_click(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_on_change(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_on_select(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_on_key(int32_t handle, void *func, void *env, int32_t size);

/* ── Geometry ──────────────────────────────────────────────────────── */
void     liva_ui_set_bounds(int32_t handle, int32_t x, int32_t y, int32_t w, int32_t h);

/* ── List / Tab operations ─────────────────────────────────────── */
void     liva_ui_list_add_item(int32_t handle, const char *item);
void     liva_ui_list_clear(int32_t handle);
int32_t  liva_ui_list_get_selection(int32_t handle);
void     liva_ui_tab_add_page(int32_t tabHandle, int32_t pageHandle, const char *title);
int32_t  liva_ui_tab_get_selection(int32_t handle);

/* ── Dialogs ───────────────────────────────────────────────────── */
void         liva_ui_message_box(const char *title, const char *message, int32_t style);
const char  *liva_ui_file_dialog(int32_t parent, const char *title, const char *wildcard, int32_t style);
int32_t      liva_ui_color_dialog(int32_t parent);

/* ── Timer ─────────────────────────────────────────────────────── */
int32_t  liva_ui_create_timer(int32_t intervalMs, void *func, void *env);
void     liva_ui_stop_timer(int32_t handle);

/* ── Clipboard ─────────────────────────────────────────────────── */
const char  *liva_ui_get_clipboard_text(void);
void         liva_ui_set_clipboard_text(const char *text);

/* ── Canvas / custom drawing (wxPaintDC) ───────────────────────── */
int32_t  liva_ui_create_canvas(int32_t parent);
void     liva_ui_canvas_on_paint(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_canvas_refresh(int32_t handle);
void     liva_ui_dc_clear(int32_t dc, int32_t r, int32_t g, int32_t b);
void     liva_ui_dc_draw_rect(int32_t dc, int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t r, int32_t g, int32_t b);
void     liva_ui_dc_draw_text(int32_t dc, const char *text, int32_t x, int32_t y,
                               int32_t r, int32_t g, int32_t b);
void     liva_ui_dc_draw_line(int32_t dc, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                               int32_t r, int32_t g, int32_t b);
void     liva_ui_dc_draw_circle(int32_t dc, int32_t cx, int32_t cy, int32_t radius,
                                 int32_t r, int32_t g, int32_t b);

#ifdef __cplusplus
}
#endif

#endif /* LIVA_WX_RUNTIME_H */
