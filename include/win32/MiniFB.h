#ifndef _MINIFB_H_
#define _MINIFB_H_
#include <stdlib.h>
#include <stdint.h>
// Header-only usage:
//   In exactly one .c file, define MINIFB_IMPLEMENTATION before including this header.
//   In all other files, include this header without MINIFB_IMPLEMENTATION.
//
// Optional input callback:
//   #define MFB_HANDLE_INPUT(wParam, isKeyDown) your_input_handler(wParam, isKeyDown)

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API

#define MFB_RGB(r, g, b) ((((unsigned int)r) << 16) | (((unsigned int)g) << 8) | b)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Create a window with a frame buffer of the given pixel depth (1, 8, 15, 16, or 32 bpp).
int mfb_open(const char *title, int width, int height, int scale, int bpp);

// Blit the buffer to the window and process pending messages.
// Returns true to continue, false if the window was closed.
bool mfb_update(const void *buffer, int fps_limit);

// Set a range of palette entries (8-bit mode only).
void mfb_set_palette_array(const uint32_t *new_palette, uint8_t start, uint8_t count);

// Set a single palette entry (8-bit mode only).
void mfb_set_palette(uint8_t color_index, uint32_t color);

// Close the window and release all resources.
void mfb_close();

// Returns a pointer to the 512-byte key state array (nonzero = held).
const char *mfb_keystatus();

// Update the window title bar.
void mfb_set_title(const char *title);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#ifdef MINIFB_IMPLEMENTATION
#ifdef MINIFB_IMPLEMENTATION_INCLUDED
#error "MINIFB_IMPLEMENTATION must be defined before including MiniFB.h in only one translation unit."
#endif
#define MINIFB_IMPLEMENTATION_INCLUDED

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef MFB_HANDLE_INPUT
#define MFB_HANDLE_INPUT(wParam, isKeyDown) ((void)(wParam), (void)(isKeyDown))
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation

typedef struct {
    HWND window;
    int close_requested;
    int buffer_width;
    int buffer_height;
    int scale;
    int bits_per_pixel;
    HDC device_context;
    const void *pixel_buffer;
    BITMAPINFO *bitmap_info;
    char keys[512];
} MfbState;

static MfbState *mfb_state;

// Sets up bitmap_info header and color table for the given bpp.
static void setup_bitmap(const int width, const int height, const int bpp) {
    mfb_state->bitmap_info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    mfb_state->bitmap_info->bmiHeader.biPlanes = 1;
    mfb_state->bitmap_info->bmiHeader.biWidth = width;
    mfb_state->bitmap_info->bmiHeader.biHeight = -height;

    const auto masks = (DWORD *) &mfb_state->bitmap_info->bmiColors[0];

    switch (bpp) {
        case 1:
            mfb_state->bitmap_info->bmiHeader.biBitCount = 1;
            mfb_state->bitmap_info->bmiHeader.biCompression = BI_RGB;
            mfb_state->bitmap_info->bmiHeader.biClrUsed = 2;
            mfb_state->bitmap_info->bmiHeader.biClrImportant = 2;
            masks[0] = 0x000000;
            masks[1] = 0xFFFFFF;
            break;
        case 8:
            mfb_state->bitmap_info->bmiHeader.biBitCount = 8;
            mfb_state->bitmap_info->bmiHeader.biCompression = BI_RGB;
            mfb_state->bitmap_info->bmiHeader.biClrUsed = 256;
            mfb_state->bitmap_info->bmiHeader.biClrImportant = 256;
            break;
        case 15:
            mfb_state->bitmap_info->bmiHeader.biBitCount = 16;
            mfb_state->bitmap_info->bmiHeader.biCompression = BI_BITFIELDS;
            masks[0] = 0x7C00;
            masks[1] = 0x03E0;
            masks[2] = 0x001F;
            break;
        case 16:
            mfb_state->bitmap_info->bmiHeader.biBitCount = 16;
            mfb_state->bitmap_info->bmiHeader.biCompression = BI_BITFIELDS;
            masks[0] = 0xF800;
            masks[1] = 0x07E0;
            masks[2] = 0x001F;
            break;
        case 32:
        default:
            mfb_state->bitmap_info->bmiHeader.biBitCount = 32;
            mfb_state->bitmap_info->bmiHeader.biCompression = BI_RGB;
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles window messages: paint, keyboard input, and close.

static LRESULT CALLBACK WndProc(HWND hWnd, const UINT message, const WPARAM wParam, const LPARAM lParam) {
    LRESULT result = 0;

    switch (message) {
        case WM_PAINT: {
            if (mfb_state->pixel_buffer) {
                StretchDIBits(mfb_state->device_context,
                              0, 0, mfb_state->buffer_width * mfb_state->scale, mfb_state->buffer_height * mfb_state->scale,
                              0, 0, mfb_state->buffer_width, mfb_state->buffer_height,
                              mfb_state->pixel_buffer,
                              mfb_state->bitmap_info, DIB_RGB_COLORS, SRCCOPY);
                ValidateRect(hWnd, nullptr);
            }
            break;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            const BOOL down = !(lParam >> 31 & 1);
            MFB_HANDLE_INPUT(wParam, down);
            if (wParam < sizeof(mfb_state->keys)) {
                mfb_state->keys[wParam] = down;
            }
            break;
        }

        case WM_CLOSE:
            mfb_state->close_requested = 1;
            break;

        default:
            result = DefWindowProc(hWnd, message, wParam, lParam);
    }

    return result;
}

// Creates a window with a frame buffer of the given pixel depth (1, 8, 15, 16, or 32 bpp).
int mfb_open(const char *title, const int width, const int height, const int scale, const int bpp) {
    WNDCLASS wc = {0};
    wc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = title;

    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 0;
    }

    RECT rect = {0};
    rect.right = width * scale;
    rect.bottom = height * scale;

    AdjustWindowRect(&rect, WS_POPUP | WS_SYSMENU | WS_CAPTION, 0);
    rect.right -= rect.left;
    rect.bottom -= rect.top;

    const int screen_width = GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYSCREEN);

    const int x_offset = GetSystemMetrics(SM_XVIRTUALSCREEN) < 0 ? -1920 : 0;
    const int y_offset = GetSystemMetrics(SM_XVIRTUALSCREEN) < 0 ? height : 0;

    mfb_state = (MfbState *) calloc(1, sizeof(MfbState));
    if (!mfb_state) {
        return 0;
    }
    mfb_state->buffer_width = width;
    mfb_state->buffer_height = height;
    mfb_state->scale = scale;
    mfb_state->bits_per_pixel = bpp;

    mfb_state->window = CreateWindowEx(0,
                                       title, title,
                                       WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                                       x_offset + (screen_width - rect.right) / 2,
                                       y_offset + (screen_height - rect.bottom + rect.top) / 2,
                                       rect.right, rect.bottom,
                                       nullptr, nullptr, nullptr, nullptr);

    if (!mfb_state->window) {
        free(mfb_state);
        mfb_state = nullptr;
        return 0;
    }

    mfb_state->bitmap_info = (BITMAPINFO *) calloc(1, sizeof(BITMAPINFO) + sizeof(RGBQUAD) * 256);
    if (!mfb_state->bitmap_info) {
        DestroyWindow(mfb_state->window);
        free(mfb_state);
        mfb_state = nullptr;
        return 0;
    }

    ShowWindow(mfb_state->window, SW_NORMAL);

    setup_bitmap(width, height, bpp);

    mfb_state->device_context = GetDC(mfb_state->window);
    if (!mfb_state->device_context) {
        free(mfb_state->bitmap_info);
        DestroyWindow(mfb_state->window);
        free(mfb_state);
        mfb_state = nullptr;
        return 0;
    }

    return 1;
}

// Sets a range of palette entries (8-bit mode only).
void mfb_set_palette_array(const uint32_t *const new_palette, const uint8_t start, const uint8_t count) {
    const auto palette = (uint32_t *) &mfb_state->bitmap_info->bmiColors[0];
    for (int i = start; i < start + count; i++) {
        palette[i] = new_palette[i - start];
    }
}

// Sets a single palette entry (8-bit mode only).
void mfb_set_palette(const uint8_t color_index, const uint32_t color) {
    const auto palette = (uint32_t *) &mfb_state->bitmap_info->bmiColors[0];
    palette[color_index] = color;
}

// Blits the buffer to the window and processes pending messages. Returns false if window was closed.
bool mfb_update(const void *buffer, const int fps_limit) {
    static DWORD previous_frame_time = 0;
    MSG msg;

    mfb_state->pixel_buffer = buffer;

    InvalidateRect(mfb_state->window, nullptr, TRUE);
    SendMessage(mfb_state->window, WM_PAINT, 0, 0);

    while (PeekMessage(&msg, mfb_state->window, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (mfb_state->close_requested)
        return false;

    if (fps_limit) {
        const DWORD target = 1000 / fps_limit;
        const DWORD elapsed = GetTickCount() - previous_frame_time;
        if (elapsed < target)
            Sleep(target - elapsed);
        previous_frame_time = GetTickCount();
    }

    return true;
}

// Releases all resources and destroys the window.
void mfb_close() {
    mfb_state->pixel_buffer = nullptr;
    free(mfb_state->bitmap_info);
    ReleaseDC(mfb_state->window, mfb_state->device_context);
    DestroyWindow(mfb_state->window);
    free(mfb_state);
    mfb_state = nullptr;
}

// Returns a pointer to the 512-byte key state array (nonzero = held).
const char *mfb_keystatus() {
    return mfb_state->keys;
}

// Update the window title bar.
void mfb_set_title(const char *title) {
    if (mfb_state && mfb_state->window)
        SetWindowText(mfb_state->window, title);
}

#endif

#endif
