/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <TextEditor.h>

namespace lfs::vis {
    struct Theme;
}

namespace lfs::vis::editor {

    // Apply theme to editor using built-in palettes
    void applyThemeToEditor(TextEditor& editor, const Theme& theme);

} // namespace lfs::vis::editor
