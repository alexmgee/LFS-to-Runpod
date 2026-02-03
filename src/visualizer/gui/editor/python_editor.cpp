/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "python_editor.hpp"
#include "editor_theme.hpp"
#include "theme/theme.hpp"
#include <cctype>

namespace lfs::vis::editor {

    PythonEditor::PythonEditor() {
        editor_.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Python);
        editor_.SetShowWhitespacesEnabled(false);
        editor_.SetTabSize(4);
        editor_.SetAutoIndentEnabled(true);
    }

    PythonEditor::~PythonEditor() = default;

    bool PythonEditor::render(const ImVec2& size) {
        execute_requested_ = false;

        // Handle focus request
        if (request_focus_) {
            ImGui::SetKeyboardFocusHere();
            request_focus_ = false;
        }

        // Check for Ctrl+Enter BEFORE rendering (to prevent newline insertion)
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            execute_requested_ = true;
            autocomplete_.hide();
        }

        // Check for autocomplete navigation BEFORE rendering
        if (autocomplete_.isVisible()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                autocomplete_.hide();
            } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
                autocomplete_.selectPrevious();
            } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
                autocomplete_.selectNext();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) ||
                       (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
                std::string selected;
                if (autocomplete_.acceptSelected(selected)) {
                    insertCompletion(selected);
                }
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        editor_.Render("##python_input", !force_unfocused_, size, true);
        ImGui::PopStyleVar();

        is_focused_ = ImGui::IsItemFocused() || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        if (is_focused_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            force_unfocused_ = false;
        }

        // Handle post-render input (Ctrl+Space, history)
        if (is_focused_) {
            // Ctrl+Space to force autocomplete
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                autocomplete_triggered_ = true;
                autocomplete_.show();
            }

            // History navigation (only when single line and autocomplete not visible)
            if (!autocomplete_.isVisible() && editor_.GetLineCount() <= 1) {
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
                    historyUp();
                } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
                    historyDown();
                }
            }

            // Update autocomplete based on current text
            updateAutocomplete();

            // Render autocomplete popup
            if (autocomplete_.isVisible()) {
                int line, col;
                editor_.GetCursorPosition(line, col);
                ImVec2 popup_pos = ImGui::GetItemRectMin();
                popup_pos.y += (line + 1) * ImGui::GetTextLineHeightWithSpacing();

                // Calculate X position using font metrics
                const float charWidth = ImGui::CalcTextSize("M").x;
                const float gutterWidth = ImGui::CalcTextSize("9999").x + 16.0f;
                const std::string word = getWordBeforeCursor();
                const int wordStartCol = col - static_cast<int>(word.length());
                popup_pos.x += gutterWidth + (wordStartCol * charWidth);

                std::string selected;
                if (autocomplete_.renderPopup(popup_pos, selected)) {
                    insertCompletion(selected);
                }
            }
        }

        return execute_requested_;
    }

    void PythonEditor::updateAutocomplete() {
        const std::string word = getWordBeforeCursor();
        const std::string context = getContextBeforeCursor();

        // Check if context ends with '.' (member access)
        const bool is_member_access = !context.empty() && context.back() == '.';

        // Don't show autocomplete for empty input unless:
        // - explicitly triggered (Ctrl+Space)
        // - typing after a dot (member access)
        if (word.empty() && !autocomplete_triggered_ && !is_member_access) {
            autocomplete_.hide();
            return;
        }

        // Update completions
        autocomplete_.updateCompletions(word, context);

        // Auto-hide if no completions
        if (!autocomplete_.hasCompletions()) {
            autocomplete_.hide();
        }

        autocomplete_triggered_ = false;
    }

    std::string PythonEditor::getWordBeforeCursor() const {
        int line, col;
        editor_.GetCursorPosition(line, col);

        auto lines = editor_.GetTextLines();
        if (line < 0 || line >= static_cast<int>(lines.size())) {
            return "";
        }

        const std::string& text = lines[line];
        if (text.empty() || col == 0) {
            return "";
        }

        const int end = std::min(col, static_cast<int>(text.length()));
        int start = end;

        while (start > 0) {
            const char c = text[start - 1];
            if (!std::isalnum(c) && c != '_') {
                break;
            }
            --start;
        }

        return text.substr(start, end - start);
    }

    std::string PythonEditor::getContextBeforeCursor() const {
        int line, col;
        editor_.GetCursorPosition(line, col);

        auto lines = editor_.GetTextLines();
        if (line < 0 || line >= static_cast<int>(lines.size())) {
            return "";
        }

        const std::string& text = lines[line];
        if (text.empty() || col == 0) {
            return "";
        }

        const int end = std::min(col, static_cast<int>(text.length()));
        const int start = std::max(0, end - 50);
        return text.substr(start, end - start);
    }

    void PythonEditor::insertCompletion(const std::string& completion) {
        const std::string word = getWordBeforeCursor();

        int line, col;
        editor_.GetCursorPosition(line, col);

        // Get current text, replace the word with completion
        auto lines = editor_.GetTextLines();
        if (line < 0 || line >= static_cast<int>(lines.size())) {
            return;
        }

        std::string& currentLine = lines[line];
        const int lineLen = static_cast<int>(currentLine.length());
        col = std::min(col, lineLen); // Clamp col to line length
        const int wordStart = col - static_cast<int>(word.length());

        if (wordStart >= 0 && wordStart <= lineLen) {
            // Replace the partial word with the completion
            currentLine = currentLine.substr(0, wordStart) + completion +
                          currentLine.substr(std::min(col, lineLen));
            editor_.SetTextLines(lines);

            // Move cursor to end of completion
            editor_.SetCursorPosition(line, wordStart + static_cast<int>(completion.length()));
        }
    }

    std::string PythonEditor::getText() const {
        return editor_.GetText();
    }

    std::string PythonEditor::getTextStripped() const {
        auto lines = editor_.GetTextLines();
        std::string result;
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string& line = lines[i];
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
                line.pop_back();
            }
            result += line;
            if (i + 1 < lines.size()) {
                result += '\n';
            }
        }
        return result;
    }

    void PythonEditor::setText(const std::string& text) {
        editor_.SetText(text);
    }

    void PythonEditor::clear() {
        editor_.SetText("");
        history_index_ = -1;
    }

    void PythonEditor::updateTheme(const Theme& theme) {
        applyThemeToEditor(editor_, theme);
    }

    void PythonEditor::addToHistory(const std::string& cmd) {
        if (cmd.empty()) {
            return;
        }

        if (!history_.empty() && history_.back() == cmd) {
            return;
        }

        history_.push_back(cmd);

        constexpr size_t MAX_HISTORY = 100;
        if (history_.size() > MAX_HISTORY) {
            history_.erase(history_.begin());
        }

        history_index_ = -1;
    }

    void PythonEditor::historyUp() {
        if (history_.empty()) {
            return;
        }

        if (history_index_ == -1) {
            current_input_ = getText();
            history_index_ = static_cast<int>(history_.size()) - 1;
        } else if (history_index_ > 0) {
            --history_index_;
        }

        setText(history_[history_index_]);
    }

    void PythonEditor::historyDown() {
        if (history_index_ == -1) {
            return;
        }

        if (history_index_ < static_cast<int>(history_.size()) - 1) {
            ++history_index_;
            setText(history_[history_index_]);
        } else {
            history_index_ = -1;
            setText(current_input_);
        }
    }

    void PythonEditor::setReadOnly(bool readonly) {
        editor_.SetReadOnlyEnabled(readonly);
    }

    bool PythonEditor::isReadOnly() const {
        return editor_.IsReadOnlyEnabled();
    }

} // namespace lfs::vis::editor
