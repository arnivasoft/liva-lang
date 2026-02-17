#include "ui_runtime.h"
#include <raylib.h>

// === Window ===

void liva_ui_init_window(int32_t w, int32_t h, const char* title) {
    InitWindow(w, h, title);
}

void liva_ui_close_window(void) {
    CloseWindow();
}

int8_t liva_ui_window_should_close(void) {
    return WindowShouldClose() ? 1 : 0;
}

void liva_ui_set_target_fps(int32_t fps) {
    SetTargetFPS(fps);
}

int32_t liva_ui_get_screen_width(void) {
    return GetScreenWidth();
}

int32_t liva_ui_get_screen_height(void) {
    return GetScreenHeight();
}

// === Drawing ===

void liva_ui_begin_drawing(void) {
    BeginDrawing();
}

void liva_ui_end_drawing(void) {
    EndDrawing();
}

void liva_ui_clear_background(int32_t r, int32_t g, int32_t b, int32_t a) {
    ClearBackground((Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

void liva_ui_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawRectangle(x, y, w, h,
                  (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

void liva_ui_draw_rect_rounded(int32_t x, int32_t y, int32_t w, int32_t h,
                               float roundness, int32_t r, int32_t g, int32_t b, int32_t a) {
    Rectangle rec = {(float)x, (float)y, (float)w, (float)h};
    DrawRectangleRounded(rec, roundness, 8,
                         (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

void liva_ui_draw_text(const char* text, int32_t x, int32_t y, int32_t size,
                       int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawText(text, x, y, size,
             (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

int32_t liva_ui_measure_text(const char* text, int32_t size) {
    return MeasureText(text, size);
}

void liva_ui_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawLine(x1, y1, x2, y2,
             (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

void liva_ui_draw_circle(int32_t cx, int32_t cy, int32_t radius,
                         int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawCircle(cx, cy, (float)radius,
               (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

// === Input — Mouse ===

int8_t liva_ui_is_mouse_pressed(int32_t button) {
    return IsMouseButtonPressed(button) ? 1 : 0;
}

int8_t liva_ui_is_mouse_released(int32_t button) {
    return IsMouseButtonReleased(button) ? 1 : 0;
}

int8_t liva_ui_is_mouse_down(int32_t button) {
    return IsMouseButtonDown(button) ? 1 : 0;
}

int32_t liva_ui_get_mouse_x(void) {
    return GetMouseX();
}

int32_t liva_ui_get_mouse_y(void) {
    return GetMouseY();
}

// === Input — Keyboard ===

int8_t liva_ui_is_key_pressed(int32_t key) {
    return IsKeyPressed(key) ? 1 : 0;
}

int8_t liva_ui_is_key_down(int32_t key) {
    return IsKeyDown(key) ? 1 : 0;
}

int32_t liva_ui_get_char_pressed(void) {
    return GetCharPressed();
}

int32_t liva_ui_get_key_pressed(void) {
    return GetKeyPressed();
}

// === Scissor (clipping) ===

void liva_ui_begin_scissor(int32_t x, int32_t y, int32_t w, int32_t h) {
    BeginScissorMode(x, y, w, h);
}

void liva_ui_end_scissor(void) {
    EndScissorMode();
}

// === Time ===

float liva_ui_get_frame_time(void) {
    return GetFrameTime();
}

// === Input — Mouse Wheel ===

int32_t liva_ui_get_mouse_wheel(void) {
    return (int32_t)GetMouseWheelMove();
}

// === Drawing — Rect Lines (border) ===

void liva_ui_draw_rect_lines(int32_t x, int32_t y, int32_t w, int32_t h,
                              int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawRectangleLines(x, y, w, h,
                       (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}
