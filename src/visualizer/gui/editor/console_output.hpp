/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <string>
#include <vector>
#include <imgui.h>

namespace lfs::vis::editor {

    enum class ConsoleMessageType {
        Input,  // User input (prefixed with >>>)
        Output, // Standard output
        Error,  // Error output
        Info    // System info messages
    };

    struct TextSpan {
        std::string text;
        ImVec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        bool bold = false;
        bool use_default_color = true;
    };

    struct ConsoleMessage {
        std::vector<TextSpan> spans;
        ConsoleMessageType type;
    };

    class ConsoleOutput {
    public:
        ConsoleOutput();
        ~ConsoleOutput();

        // Add messages
        void addInput(const std::string& text);
        void addOutput(const std::string& text);
        void addError(const std::string& text);
        void addInfo(const std::string& text);

        // Line-buffered output (for Python stdout/stderr capture)
        void appendOutput(const std::string& text, bool is_error);
        void flushBuffers();

        // Update the last output line (for \r progress updates)
        void updateLastLine(const std::string& text, bool is_error = false);

        // Clear all messages
        void clear();

        // Render the output area
        void render(const ImVec2& size);

        // Copy functionality
        std::string getAllText() const;
        std::string getSelectedText() const;
        bool hasSelection() const { return selection_start_ != selection_end_; }

        // Scroll control
        void scrollToBottom() { scroll_to_bottom_ = true; }

    private:
        void renderContextMenu();
        void handleSelection();
        void copyToClipboard(const std::string& text);

        std::vector<ConsoleMessage> messages_;
        bool scroll_to_bottom_ = false;
        bool auto_scroll_ = true;

        // Line buffers for streaming output
        std::string stdout_buffer_;
        std::string stderr_buffer_;

        // Selection state
        int selection_start_ = 0;
        int selection_end_ = 0;
        bool is_selecting_ = false;
        int hover_line_ = -1;
    };

} // namespace lfs::vis::editor
