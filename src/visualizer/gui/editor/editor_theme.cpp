/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "editor_theme.hpp"
#include "theme/theme.hpp"

namespace lfs::vis::editor {

    void applyThemeToEditor(TextEditor& editor, const Theme& theme) {
        // Detect dark/light theme from background brightness
        const auto& bg = theme.palette.background;
        const float brightness = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
        const bool is_dark = brightness < 0.5f;

        if (is_dark) {
            editor.SetPalette(TextEditor::PaletteId::Dark);
        } else {
            editor.SetPalette(TextEditor::PaletteId::Light);
        }
    }

} // namespace lfs::vis::editor
