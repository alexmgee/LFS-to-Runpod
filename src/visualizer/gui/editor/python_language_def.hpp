/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

// This file is deprecated - santaclose TextEditor fork has built-in Python support.
// Use TextEditor::SetLanguageDefinition(TextEditor::LanguageDefinitionId::Python)

namespace lfs::vis::editor {
    // Kept for backward compatibility - does nothing
    inline void initPythonLanguage() {}
} // namespace lfs::vis::editor
