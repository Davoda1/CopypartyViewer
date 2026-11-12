// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
#pragma once

class Fl_Window;

namespace cpv {
// Call on the UI thread after show(), and after an application palette change.
// Uses native Windows decorations; unsupported attributes safely retain the
// operating system's frame. Other platforms keep their window-manager styling.
void apply_titlebar_theme(Fl_Window& window, bool dark) noexcept;
}
