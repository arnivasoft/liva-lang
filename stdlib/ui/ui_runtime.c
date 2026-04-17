#include "ui_runtime.h"
#include <raylib.h>
#include <stdlib.h>
#include <string.h>

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
    // Manual 4-line draw to avoid DrawRectangleLines 1px corner overflow
    Color c = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    DrawLine(x, y, x + w - 1, y, c);             // top
    DrawLine(x, y + h - 1, x + w - 1, y + h - 1, c); // bottom
    DrawLine(x, y, x, y + h - 1, c);             // left
    DrawLine(x + w - 1, y, x + w - 1, y + h - 1, c); // right
}

// === Font ===

#define MAX_FONTS 16
static Font fontSlots[MAX_FONTS];
static bool fontUsed[MAX_FONTS] = {false};

int32_t liva_ui_load_font(const char* path, int32_t size) {
    // slot 0 reserved, custom fonts use 1-15
    for (int i = 1; i < MAX_FONTS; ++i) {
        if (!fontUsed[i]) {
            fontSlots[i] = LoadFontEx(path, size, NULL, 0);
            SetTextureFilter(fontSlots[i].texture, TEXTURE_FILTER_BILINEAR);
            fontUsed[i] = true;
            return i;
        }
    }
    return -1; // no free slot
}

void liva_ui_unload_font(int32_t handle) {
    if (handle >= 1 && handle < MAX_FONTS && fontUsed[handle]) {
        UnloadFont(fontSlots[handle]);
        fontUsed[handle] = false;
    }
}

void liva_ui_draw_text_font(int32_t handle, const char* text,
                             int32_t x, int32_t y, int32_t size,
                             int32_t r, int32_t g, int32_t b, int32_t a) {
    if (handle < 1 || handle >= MAX_FONTS || !fontUsed[handle]) return;
    float spacing = (size < 20) ? 1.0f : 0.0f;
    DrawTextEx(fontSlots[handle], text, (Vector2){(float)x, (float)y}, (float)size, spacing,
               (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

int32_t liva_ui_measure_text_font(int32_t handle, const char* text, int32_t size) {
    if (handle < 1 || handle >= MAX_FONTS || !fontUsed[handle]) return 0;
    float spacing = (size < 20) ? 1.0f : 0.0f;
    return (int32_t)MeasureTextEx(fontSlots[handle], text, (float)size, spacing).x;
}

// === Image/Texture ===

#define MAX_IMAGES 64
static Texture2D imageSlots[MAX_IMAGES];
static bool imageUsed[MAX_IMAGES] = {false};

int32_t liva_ui_load_image(const char* path) {
    for (int i = 1; i < MAX_IMAGES; ++i) {
        if (!imageUsed[i]) {
            imageSlots[i] = LoadTexture(path);
            if (imageSlots[i].id == 0) return -1;
            SetTextureFilter(imageSlots[i], TEXTURE_FILTER_BILINEAR);
            imageUsed[i] = true;
            return i;
        }
    }
    return -1; // no free slot
}

void liva_ui_unload_image(int32_t handle) {
    if (handle >= 1 && handle < MAX_IMAGES && imageUsed[handle]) {
        UnloadTexture(imageSlots[handle]);
        imageUsed[handle] = false;
    }
}

void liva_ui_draw_image(int32_t handle, int32_t x, int32_t y) {
    if (handle < 1 || handle >= MAX_IMAGES || !imageUsed[handle]) return;
    DrawTexture(imageSlots[handle], x, y, WHITE);
}

void liva_ui_draw_image_scaled(int32_t handle, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (handle < 1 || handle >= MAX_IMAGES || !imageUsed[handle]) return;
    Rectangle src = {0, 0, (float)imageSlots[handle].width, (float)imageSlots[handle].height};
    Rectangle dst = {(float)x, (float)y, (float)w, (float)h};
    DrawTexturePro(imageSlots[handle], src, dst, (Vector2){0, 0}, 0.0f, WHITE);
}

int32_t liva_ui_get_image_width(int32_t handle) {
    if (handle < 1 || handle >= MAX_IMAGES || !imageUsed[handle]) return 0;
    return imageSlots[handle].width;
}

int32_t liva_ui_get_image_height(int32_t handle) {
    if (handle < 1 || handle >= MAX_IMAGES || !imageUsed[handle]) return 0;
    return imageSlots[handle].height;
}

// === Clipboard ===

const char* liva_ui_get_clipboard_text(void) {
    const char* text = GetClipboardText();
    return text ? text : "";
}

void liva_ui_set_clipboard_text(const char* text) {
    if (text) SetClipboardText(text);
}

// === Word-Wrap ===

// Helper: draw or measure word-wrapped text. If draw==true, actually draw.
// Uses a temp buffer instead of modifying the input string (which may be read-only).
static int32_t wrap_text_impl(const char* text, int32_t x, int32_t y,
                               int32_t fontSize, int32_t maxWidth,
                               int32_t r, int32_t g, int32_t b, int32_t a,
                               bool draw) {
    if (!text || !text[0]) return 0;
    int32_t lineHeight = fontSize;
    int32_t curY = y;
    const char *p = text;
    // Temp buffer for measuring/drawing substrings (input may be read-only)
    int32_t textLen = (int32_t)strlen(text);
    int32_t bufSize = textLen + 1;
    char *buf = (char*)malloc(bufSize);
    if (!buf) return 0;

    while (*p) {
        // Find end of logical line (\n or end)
        const char *lineEnd = p;
        while (*lineEnd && *lineEnd != '\n') lineEnd++;
        // Process this logical line with word wrapping
        const char *lp = p;
        while (lp < lineEnd) {
            // Skip leading spaces
            while (lp < lineEnd && *lp == ' ') lp++;
            if (lp >= lineEnd) break;

            // Find how many chars fit in maxWidth
            int32_t bestEnd = 0;
            int32_t lastSpace = -1;
            for (int32_t i = 0; lp + i <= lineEnd; ++i) {
                // Copy substring lp[0..i] to temp buffer for measurement
                memcpy(buf, lp, i);
                buf[i] = '\0';
                int32_t w = MeasureText(buf, fontSize);
                if (w <= maxWidth || i == 0) {
                    bestEnd = i;
                    if (i > 0 && lp[i] == ' ') lastSpace = i;
                    if (lp + i == lineEnd) { bestEnd = i; break; }
                } else {
                    break;
                }
            }
            // If we stopped mid-word, break at last space
            if (bestEnd < (int32_t)(lineEnd - lp) && lastSpace > 0) {
                bestEnd = lastSpace;
            }
            // If bestEnd is still 0 but we have chars, force at least 1 char
            if (bestEnd == 0 && lp < lineEnd) bestEnd = 1;

            if (draw) {
                memcpy(buf, lp, bestEnd);
                buf[bestEnd] = '\0';
                DrawText(buf, x, curY, fontSize,
                         (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
            }
            curY += lineHeight;
            lp += bestEnd;
        }
        // Advance past \n (empty line = advance one line)
        if (*lineEnd == '\n') {
            if (lp == p) curY += lineHeight; // empty line
            p = lineEnd + 1;
        } else {
            if (lp == p && lineEnd > p) curY += lineHeight;
            p = lineEnd;
        }
    }
    free(buf);
    return curY - y;
}

int32_t liva_ui_draw_text_wrapped(const char* text, int32_t x, int32_t y,
                                   int32_t fontSize, int32_t maxWidth,
                                   int32_t r, int32_t g, int32_t b, int32_t a) {
    return wrap_text_impl(text, x, y, fontSize, maxWidth, r, g, b, a, true);
}

int32_t liva_ui_measure_text_wrapped(const char* text, int32_t fontSize, int32_t maxWidth) {
    return wrap_text_impl(text, 0, 0, fontSize, maxWidth, 0, 0, 0, 0, false);
}
