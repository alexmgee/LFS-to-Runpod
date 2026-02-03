/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "autocomplete/autocomplete_manager.hpp"
#include <TextEditor.h>
#include <string>
#include <vector>
#include <imgui.h>

namespace lfs::vis {
    struct Theme;
}

namespace lfs::vis::editor {

    class PythonEditor {
    public:
        PythonEditor();
        ~PythonEditor();

        // Render the editor
        // Returns true if execute was triggered (Ctrl+Enter)
        bool render(const ImVec2& size);

        // Get/set text
        std::string getText() const;
        std::string getTextStripped() const;
        void setText(const std::string& text);
        void clear();

        // Check if execute was requested this frame
        bool shouldExecute() const { return execute_requested_; }

        // Update theme
        void updateTheme(const Theme& theme);

        // Command history
        void addToHistory(const std::string& cmd);
        void historyUp();
        void historyDown();

        // Focus management
        void focus() {
            request_focus_ = true;
            force_unfocused_ = false;
        }
        void unfocus() { force_unfocused_ = true; }
        bool isFocused() const { return is_focused_ && !force_unfocused_; }

        // Read-only mode (prevents keyboard input)
        void setReadOnly(bool readonly);
        bool isReadOnly() const;

    private:
        void updateAutocomplete();
        std::string getWordBeforeCursor() const;
        std::string getContextBeforeCursor() const;
        void insertCompletion(const std::string& text);

        TextEditor editor_;
        AutocompleteManager autocomplete_;

        bool execute_requested_ = false;
        bool request_focus_ = false;
        bool is_focused_ = false;
        bool force_unfocused_ = false;
        bool autocomplete_triggered_ = false;

        // Command history
        std::vector<std::string> history_;
        int history_index_ = -1;
        std::string current_input_;
    };

} // namespace lfs::vis::editor
