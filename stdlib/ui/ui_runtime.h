#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// === Window ===
void     liva_ui_init_window(int32_t w, int32_t h, const char* title);
void     liva_ui_close_window(void);
int8_t   liva_ui_window_should_close(void);
void     liva_ui_set_target_fps(int32_t fps);
int32_t  liva_ui_get_screen_width(void);
int32_t  liva_ui_get_screen_height(void);

// === Drawing ===
void     liva_ui_begin_drawing(void);
void     liva_ui_end_drawing(void);
void     liva_ui_clear_background(int32_t r, int32_t g, int32_t b, int32_t a);
void     liva_ui_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t r, int32_t g, int32_t b, int32_t a);
void     liva_ui_draw_rect_rounded(int32_t x, int32_t y, int32_t w, int32_t h,
                                   float roundness, int32_t r, int32_t g, int32_t b, int32_t a);
void     liva_ui_draw_text(const char* text, int32_t x, int32_t y, int32_t size,
                           int32_t r, int32_t g, int32_t b, int32_t a);
int32_t  liva_ui_measure_text(const char* text, int32_t size);
void     liva_ui_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                           int32_t r, int32_t g, int32_t b, int32_t a);
void     liva_ui_draw_circle(int32_t cx, int32_t cy, int32_t radius,
                             int32_t r, int32_t g, int32_t b, int32_t a);

// === Input — Mouse ===
int8_t   liva_ui_is_mouse_pressed(int32_t button);
int8_t   liva_ui_is_mouse_released(int32_t button);
int8_t   liva_ui_is_mouse_down(int32_t button);
int32_t  liva_ui_get_mouse_x(void);
int32_t  liva_ui_get_mouse_y(void);

// === Input — Keyboard ===
int8_t   liva_ui_is_key_pressed(int32_t key);
int8_t   liva_ui_is_key_down(int32_t key);
int32_t  liva_ui_get_char_pressed(void);
int32_t  liva_ui_get_key_pressed(void);

// === Scissor (clipping) ===
void     liva_ui_begin_scissor(int32_t x, int32_t y, int32_t w, int32_t h);
void     liva_ui_end_scissor(void);

// === Time ===
float    liva_ui_get_frame_time(void);

// === Input — Mouse Wheel ===
int32_t  liva_ui_get_mouse_wheel(void);

// === Drawing — Rect Lines (border) ===
void     liva_ui_draw_rect_lines(int32_t x, int32_t y, int32_t w, int32_t h,
                                  int32_t r, int32_t g, int32_t b, int32_t a);

// === Font ===
int32_t  liva_ui_load_font(const char* path, int32_t size);
void     liva_ui_unload_font(int32_t handle);
void     liva_ui_draw_text_font(int32_t handle, const char* text,
                                 int32_t x, int32_t y, int32_t size,
                                 int32_t r, int32_t g, int32_t b, int32_t a);
int32_t  liva_ui_measure_text_font(int32_t handle, const char* text, int32_t size);

// === Clipboard ===
const char* liva_ui_get_clipboard_text(void);
void        liva_ui_set_clipboard_text(const char* text);

// === Word-Wrap ===
int32_t  liva_ui_draw_text_wrapped(const char* text, int32_t x, int32_t y,
                                    int32_t fontSize, int32_t maxWidth,
                                    int32_t r, int32_t g, int32_t b, int32_t a);
int32_t  liva_ui_measure_text_wrapped(const char* text, int32_t fontSize, int32_t maxWidth);

#ifdef __cplusplus
}
#endif
