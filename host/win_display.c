/* SPDX-License-Identifier: MIT */
#include "win_display.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static uint16_t s_frame[LW_HOST_WIDTH * LW_HOST_HEIGHT];
static lw_host_pointer_t s_pointer;

static HWND s_window;
static HDC s_window_dc;
static HBITMAP s_bitmap;
static HDC s_bitmap_dc;
static void *s_bitmap_bits;
static int s_running = 1;

/* ------------------------------------------------------------- rendering */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (s_bitmap_bits == NULL) {
        lv_display_flush_ready(disp);
        return;
    }
    const uint16_t *src = (const uint16_t *)px_map;
    uint8_t *dst = (uint8_t *)s_bitmap_bits;
    for (int y = area->y1; y <= area->y2; y++) {
        uint8_t *row = dst + (size_t)y * LW_HOST_WIDTH * 4;
        for (int x = area->x1; x <= area->x2; x++) {
            uint16_t pixel = *src++;
            row[x * 4 + 0] = (uint8_t)((pixel & 0x1F) * 255 / 31);          /* B */
            row[x * 4 + 1] = (uint8_t)(((pixel >> 5) & 0x3F) * 255 / 63);   /* G */
            row[x * 4 + 2] = (uint8_t)(((pixel >> 11) & 0x1F) * 255 / 31);  /* R */
            row[x * 4 + 3] = 0;
        }
    }
    lv_display_flush_ready(disp);
    if (s_window != NULL) {
        RECT rect = {area->x1, area->y1, area->x2 + 1, area->y2 + 1};
        InvalidateRect(s_window, &rect, FALSE);
    }
}

static uint32_t tick_cb(void)
{
    return (uint32_t)GetTickCount64();
}

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_pointer.x;
    data->point.y = s_pointer.y;
    data->state = s_pointer.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_display_t *lw_host_display_init(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(LW_HOST_WIDTH, LW_HOST_HEIGHT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_frame, NULL, sizeof(s_frame), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_default(disp);

    lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_PINK), lv_palette_main(LV_PALETTE_PURPLE), false,
                          LV_FONT_DEFAULT);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);

    s_pointer.x = LW_HOST_WIDTH / 2;
    s_pointer.y = LW_HOST_HEIGHT / 2;
    return disp;
}

lw_host_pointer_t *lw_host_pointer(void)
{
    return &s_pointer;
}

/* ------------------------------------------------------------- screenshot */

int lw_host_screenshot_bmp(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    const int width = LW_HOST_WIDTH;
    const int height = LW_HOST_HEIGHT;
    const int row_bytes = (width * 3 + 3) & ~3;
    const int data_size = row_bytes * height;

    uint8_t header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    uint32_t file_size = 54u + (uint32_t)data_size;
    uint32_t offset = 54;
    uint32_t dib = 40;
    int32_t w = width;
    int32_t h = height;
    uint16_t planes = 1;
    uint16_t bpp = 24;
    uint32_t image_size = (uint32_t)data_size;
    memcpy(header + 2, &file_size, 4);
    memcpy(header + 10, &offset, 4);
    memcpy(header + 14, &dib, 4);
    memcpy(header + 18, &w, 4);
    memcpy(header + 22, &h, 4);
    memcpy(header + 26, &planes, 2);
    memcpy(header + 28, &bpp, 2);
    memcpy(header + 34, &image_size, 4);
    fwrite(header, 1, sizeof(header), file);

    uint8_t *row = (uint8_t *)malloc((size_t)row_bytes);
    if (row == NULL) {
        fclose(file);
        return -1;
    }
    for (int y = height - 1; y >= 0; y--) {
        memset(row, 0, (size_t)row_bytes);
        for (int x = 0; x < width; x++) {
            uint16_t pixel = s_frame[y * width + x];
            row[x * 3 + 0] = (uint8_t)((pixel & 0x1F) * 255 / 31);
            row[x * 3 + 1] = (uint8_t)(((pixel >> 5) & 0x3F) * 255 / 63);
            row[x * 3 + 2] = (uint8_t)(((pixel >> 11) & 0x1F) * 255 / 31);
        }
        fwrite(row, 1, (size_t)row_bytes, file);
    }
    free(row);
    fclose(file);
    return 0;
}

/* ----------------------------------------------------------------- window */

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CLOSE:
        s_running = 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (s_bitmap_dc != NULL) {
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top, s_bitmap_dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        s_pointer.x = (int)(short)LOWORD(lparam);
        s_pointer.y = (int)(short)HIWORD(lparam);
        return 0;
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        s_pointer.x = (int)(short)LOWORD(lparam);
        s_pointer.y = (int)(short)HIWORD(lparam);
        s_pointer.pressed = 1;
        return 0;
    }
    case WM_LBUTTONUP:
        s_pointer.x = (int)(short)LOWORD(lparam);
        s_pointer.y = (int)(short)HIWORD(lparam);
        s_pointer.pressed = 0;
        ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            s_running = 0;
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    default:
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
}

int lw_host_run_window(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "LittleWorldHost";
    RegisterClassA(&wc);

    RECT rect = {0, 0, LW_HOST_WIDTH, LW_HOST_HEIGHT};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
    s_window = CreateWindowExA(0, "LittleWorldHost", "Little World  |  ESP32-P4 desktop simulator",
                               WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                               rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, wc.hInstance, NULL);
    if (s_window == NULL) {
        printf("cannot create the window\n");
        return 1;
    }

    s_window_dc = GetDC(s_window);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = LW_HOST_WIDTH;
    bmi.bmiHeader.biHeight = -LW_HOST_HEIGHT; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    s_bitmap = CreateDIBSection(s_window_dc, &bmi, DIB_RGB_COLORS, &s_bitmap_bits, NULL, 0);
    if (s_bitmap == NULL) {
        printf("cannot create the DIB\n");
        return 1;
    }
    s_bitmap_dc = CreateCompatibleDC(s_window_dc);
    SelectObject(s_bitmap_dc, s_bitmap);

    ShowWindow(s_window, SW_SHOW);
    UpdateWindow(s_window);

    MSG msg;
    while (s_running) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                s_running = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!s_running) {
            break;
        }
        lv_timer_handler();
        Sleep(4);
    }

    DeleteDC(s_bitmap_dc);
    DeleteObject(s_bitmap);
    ReleaseDC(s_window, s_window_dc);
    return 0;
}
