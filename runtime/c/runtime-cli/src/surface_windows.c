/* SPDX-License-Identifier: MIT
 * Actual Win32 HWND host for the public WebGPU Windows surface descriptor. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "surface_platform.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

struct doe_surface_host {
    HINSTANCE instance;
    HWND window;
    ATOM window_class;
    wchar_t class_name[64];
};

static void pump_messages(void)
{
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void surface_host_release(doe_surface_host *host)
{
    if (!host) return;
    if (host->window) DestroyWindow(host->window);
    if (host->window_class) UnregisterClassW(host->class_name, host->instance);
    free(host);
}

bool surface_host_resize(doe_surface_host *host, uint32_t width, uint32_t height, char *message, size_t capacity)
{
    if (!host || !host->window || !width || !height || width > INT_MAX || height > INT_MAX) {
        snprintf(message, capacity, "invalid Win32 surface extent");
        return false;
    }
    RECT rectangle = {0, 0, (LONG)width, (LONG)height};
    bool adjusted = AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0) != 0;
    int64_t outer_width = (int64_t)rectangle.right - rectangle.left;
    int64_t outer_height = (int64_t)rectangle.bottom - rectangle.top;
    if (!adjusted || outer_width <= 0 || outer_height <= 0 || outer_width > INT_MAX || outer_height > INT_MAX ||
        !SetWindowPos(host->window, NULL, 0, 0, (int)outer_width, (int)outer_height, SWP_NOMOVE | SWP_NOZORDER)) {
        snprintf(message, capacity, "Win32 surface resize failed: %lu", (unsigned long)GetLastError());
        return false;
    }
    ShowWindow(host->window, SW_SHOW);
    UpdateWindow(host->window);
    pump_messages();
    return true;
}

bool surface_host_create(WGPUInstance instance, doe_surface_host **host_out, WGPUSurface *surface_out,
    char *message, size_t capacity)
{
    *host_out = NULL;
    *surface_out = NULL;
    doe_surface_host *host = calloc(1, sizeof(*host));
    if (!host) { snprintf(message, capacity, "Win32 surface allocation failed"); return false; }
    host->instance = GetModuleHandleW(NULL);
    swprintf(host->class_name, sizeof(host->class_name) / sizeof(*host->class_name), L"DoeCSurface%p", (void *)host);
    WNDCLASSEXW window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = host->instance;
    window_class.lpszClassName = host->class_name;
    host->window_class = RegisterClassExW(&window_class);
    if (host->window_class) host->window = CreateWindowExW(0, host->class_name, L"Doe C WebGPU surface",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, NULL, NULL, host->instance, NULL);
    if (!host->window) {
        snprintf(message, capacity, "Win32 surface window creation failed: %lu", (unsigned long)GetLastError());
        surface_host_release(host);
        return false;
    }
    WGPUSurfaceSourceWindowsHWND source = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
    source.hinstance = host->instance;
    source.hwnd = host->window;
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    WGPUSurface surface = wgpuInstanceCreateSurface(instance, &descriptor);
    if (!surface) {
        snprintf(message, capacity, "WebGPU Windows surface creation failed");
        surface_host_release(host);
        return false;
    }
    *host_out = host;
    *surface_out = surface;
    return true;
}
