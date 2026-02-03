/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "console_output.hpp"
#include "theme/theme.hpp"
#include <algorithm>
#include <regex>
#include <sstream>

namespace lfs::vis::editor {

    namespace {
        // ANSI color codes to ImVec4
        ImVec4 ansi_to_color(int code, bool bright = false) {
            float intensity = bright ? 1.0f : 0.7f;
            switch (code) {
            case 30: return ImVec4(0.0f, 0.0f, 0.0f, 1.0f);           // Black
            case 31: return ImVec4(intensity, 0.2f, 0.2f, 1.0f);      // Red
            case 32: return ImVec4(0.2f, intensity, 0.2f, 1.0f);      // Green
            case 33: return ImVec4(intensity, intensity, 0.2f, 1.0f); // Yellow
            case 34: return ImVec4(0.3f, 0.5f, intensity, 1.0f);      // Blue
            case 35: return ImVec4(intensity, 0.3f, intensity, 1.0f); // Magenta
            case 36: return ImVec4(0.3f, intensity, intensity, 1.0f); // Cyan
            case 37: return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);           // White
            case 90: return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);           // Bright Black
            case 91: return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);           // Bright Red
            case 92: return ImVec4(0.4f, 1.0f, 0.4f, 1.0f);           // Bright Green
            case 93: return ImVec4(1.0f, 1.0f, 0.4f, 1.0f);           // Bright Yellow
            case 94: return ImVec4(0.4f, 0.6f, 1.0f, 1.0f);           // Bright Blue
            case 95: return ImVec4(1.0f, 0.4f, 1.0f, 1.0f);           // Bright Magenta
            case 96: return ImVec4(0.4f, 1.0f, 1.0f, 1.0f);           // Bright Cyan
            case 97: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // Bright White
            default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        std::vector<TextSpan> parse_ansi(const std::string& text) {
            std::vector<TextSpan> spans;
            TextSpan current;
            current.use_default_color = true;

            size_t pos = 0;
            while (pos < text.size()) {
                // Look for ESC[
                size_t esc_pos = text.find("\x1b[", pos);
                if (esc_pos == std::string::npos) {
                    // No more escape codes, add rest of text
                    if (pos < text.size()) {
                        current.text = text.substr(pos);
                        if (!current.text.empty()) {
                            spans.push_back(current);
                        }
                    }
                    break;
                }

                // Add text before escape sequence
                if (esc_pos > pos) {
                    current.text = text.substr(pos, esc_pos - pos);
                    if (!current.text.empty()) {
                        spans.push_back(current);
                        current.text.clear();
                    }
                }

                // Find 'm' that ends the SGR sequence
                size_t end_pos = text.find('m', esc_pos + 2);
                if (end_pos == std::string::npos) {
                    pos = esc_pos + 2;
                    continue;
                }

                // Parse SGR codes
                std::string codes_str = text.substr(esc_pos + 2, end_pos - esc_pos - 2);
                std::istringstream ss(codes_str);
                std::string code_part;
                while (std::getline(ss, code_part, ';')) {
                    if (code_part.empty())
                        continue;
                    int code = std::stoi(code_part);
                    if (code == 0) {
                        // Reset
                        current.use_default_color = true;
                        current.bold = false;
                    } else if (code == 1) {
                        current.bold = true;
                    } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
                        current.color = ansi_to_color(code);
                        current.use_default_color = false;
                    }
                }

                pos = end_pos + 1;
            }

            if (spans.empty()) {
                TextSpan plain;
                plain.text = text;
                plain.use_default_color = true;
                spans.push_back(plain);
            }

            return spans;
        }
    } // namespace

    ConsoleOutput::ConsoleOutput() = default;
    ConsoleOutput::~ConsoleOutput() = default;

    void ConsoleOutput::addInput(const std::string& text) {
        ConsoleMessage msg;
        msg.type = ConsoleMessageType::Input;
        msg.spans = parse_ansi(text);
        messages_.push_back(std::move(msg));
        if (auto_scroll_) {
            scroll_to_bottom_ = true;
        }
    }

    void ConsoleOutput::addOutput(const std::string& text) {
        ConsoleMessage msg;
        msg.type = ConsoleMessageType::Output;
        msg.spans = parse_ansi(text);
        messages_.push_back(std::move(msg));
        if (auto_scroll_) {
            scroll_to_bottom_ = true;
        }
    }

    void ConsoleOutput::addError(const std::string& text) {
        ConsoleMessage msg;
        msg.type = ConsoleMessageType::Error;
        msg.spans = parse_ansi(text);
        messages_.push_back(std::move(msg));
        if (auto_scroll_) {
            scroll_to_bottom_ = true;
        }
    }

    void ConsoleOutput::addInfo(const std::string& text) {
        ConsoleMessage msg;
        msg.type = ConsoleMessageType::Info;
        msg.spans = parse_ansi(text);
        messages_.push_back(std::move(msg));
        if (auto_scroll_) {
            scroll_to_bottom_ = true;
        }
    }

    void ConsoleOutput::appendOutput(const std::string& text, bool is_error) {
        std::string& buffer = is_error ? stderr_buffer_ : stdout_buffer_;
        buffer += text;

        size_t pos = 0;
        size_t newline_pos;
        while ((newline_pos = buffer.find('\n', pos)) != std::string::npos) {
            std::string line = buffer.substr(pos, newline_pos - pos);
            if (is_error) {
                addError(line);
            } else {
                addOutput(line);
            }
            pos = newline_pos + 1;
        }

        if (pos > 0) {
            buffer.erase(0, pos);
        }
    }

    void ConsoleOutput::flushBuffers() {
        if (!stdout_buffer_.empty()) {
            addOutput(stdout_buffer_);
            stdout_buffer_.clear();
        }
        if (!stderr_buffer_.empty()) {
            addError(stderr_buffer_);
            stderr_buffer_.clear();
        }
    }

    void ConsoleOutput::updateLastLine(const std::string& text, bool is_error) {
        ConsoleMessageType target_type = is_error ? ConsoleMessageType::Error : ConsoleMessageType::Output;

        // Find last message of matching type and update it
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            if (it->type == target_type) {
                it->spans = parse_ansi(text);
                if (auto_scroll_) {
                    scroll_to_bottom_ = true;
                }
                return;
            }
        }

        // No matching message found, add new one
        if (is_error) {
            addError(text);
        } else {
            addOutput(text);
        }
    }

    void ConsoleOutput::clear() {
        messages_.clear();
    }

    void ConsoleOutput::render(const ImVec2& size) {
        const auto& t = theme();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, t.palette.surface);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

        if (ImGui::BeginChild("##console_output", size, true,
                              ImGuiWindowFlags_HorizontalScrollbar)) {

            for (const auto& msg : messages_) {
                ImVec4 default_color;
                const char* prefix = "";
                switch (msg.type) {
                case ConsoleMessageType::Input:
                    default_color = t.palette.success;
                    prefix = ">>> ";
                    break;
                case ConsoleMessageType::Output:
                    default_color = t.palette.text;
                    break;
                case ConsoleMessageType::Error:
                    default_color = t.palette.error;
                    break;
                case ConsoleMessageType::Info:
                    default_color = t.palette.info;
                    break;
                }

                // Render prefix
                if (prefix[0] != '\0') {
                    ImGui::PushStyleColor(ImGuiCol_Text, default_color);
                    ImGui::TextUnformatted(prefix);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 0);
                }

                // Render spans
                for (size_t i = 0; i < msg.spans.size(); ++i) {
                    const auto& span = msg.spans[i];
                    ImVec4 color = span.use_default_color ? default_color : span.color;
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(span.text.c_str());
                    ImGui::PopStyleColor();
                    if (i + 1 < msg.spans.size()) {
                        ImGui::SameLine(0, 0);
                    }
                }

                // Handle empty messages (just show newline)
                if (msg.spans.empty() || (msg.spans.size() == 1 && msg.spans[0].text.empty())) {
                    ImGui::TextUnformatted("");
                }
            }

            // Context menu for copy
            if (ImGui::BeginPopupContextWindow("##console_context")) {
                if (ImGui::MenuItem("Copy All", "Ctrl+C")) {
                    ImGui::SetClipboardText(getAllText().c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear")) {
                    clear();
                }
                ImGui::Separator();
                ImGui::MenuItem("Auto-scroll", nullptr, &auto_scroll_);
                ImGui::EndPopup();
            }

            // Ctrl+C to copy all when focused
            if (ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl &&
                ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                ImGui::SetClipboardText(getAllText().c_str());
            }

            // Auto scroll
            if (scroll_to_bottom_ && auto_scroll_) {
                ImGui::SetScrollHereY(1.0f);
                scroll_to_bottom_ = false;
            }
        }
        ImGui::EndChild();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    std::string ConsoleOutput::getAllText() const {
        std::ostringstream oss;
        for (const auto& msg : messages_) {
            switch (msg.type) {
            case ConsoleMessageType::Input:
                oss << ">>> ";
                break;
            default:
                break;
            }
            for (const auto& span : msg.spans) {
                oss << span.text;
            }
            oss << "\n";
        }
        return oss.str();
    }

    std::string ConsoleOutput::getSelectedText() const {
        return getAllText();
    }

    void ConsoleOutput::copyToClipboard(const std::string& text) {
        ImGui::SetClipboardText(text.c_str());
    }

} // namespace lfs::vis::editor
