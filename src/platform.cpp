// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
#include "platform.hpp"
#include <FL/Fl_Window.H>

#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
# include <dwmapi.h>
# include <FL/platform.H>
#endif

namespace cpv {
void apply_titlebar_theme(Fl_Window& window, bool dark) noexcept {
#ifdef _WIN32
    if (!window.shown()) return;
    const HWND handle = fl_win32_xid(&window);
    if (!handle) return;

    // DWORD constants allow building with older MinGW headers while using the
    // documented DWM attributes. Windows reports unsupported attributes with an
    // HRESULT; no version probing, private ordinals or custom frame is needed.
    constexpr DWORD immersive_dark_mode = 20;
    constexpr DWORD caption_color_attribute = 35;
    constexpr DWORD text_color_attribute = 36;
    constexpr COLORREF system_color = 0xffffffffu;

    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    const bool high_contrast =
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    const bool use_dark = dark && !high_contrast;
    const BOOL immersive = use_dark ? TRUE : FALSE;

    bool changed = SUCCEEDED(DwmSetWindowAttribute(
        handle, immersive_dark_mode, &immersive, sizeof(immersive)));

    // Windows 11 can match the native caption to the application palette. Older
    // Windows versions simply reject these two attributes and retain their
    // native caption behavior. Reset both overrides on light/high-contrast mode.
    const COLORREF caption = use_dark ? RGB(42, 44, 48) : system_color;
    const COLORREF text = use_dark ? RGB(233, 235, 238) : system_color;
    changed = SUCCEEDED(DwmSetWindowAttribute(
        handle, caption_color_attribute, &caption, sizeof(caption))) || changed;
    changed = SUCCEEDED(DwmSetWindowAttribute(
        handle, text_color_attribute, &text, sizeof(text))) || changed;

    if (changed) {
        RedrawWindow(handle, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_NOCHILDREN);
    }
#else
    (void)window;
    (void)dark;
#endif
}
}
