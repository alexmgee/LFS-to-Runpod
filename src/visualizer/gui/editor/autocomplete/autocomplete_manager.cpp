/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "autocomplete_manager.hpp"
#include "python_introspector.hpp"
#include "static_providers.hpp"
#include "theme/theme.hpp"
#include <algorithm>

namespace lfs::vis::editor {

    AutocompleteManager::AutocompleteManager() {
        // Add default static providers
        auto staticProviders = createStaticProviders();
        for (auto& p : staticProviders) {
            providers_.push_back(std::move(p));
        }

        // Add runtime Python introspector
#ifdef LFS_BUILD_PYTHON_BINDINGS
        providers_.push_back(std::make_unique<PythonIntrospector>());
#endif
    }

    AutocompleteManager::~AutocompleteManager() = default;

    void AutocompleteManager::addProvider(std::unique_ptr<ISymbolProvider> provider) {
        providers_.push_back(std::move(provider));
    }

    void AutocompleteManager::updateCompletions(const std::string& prefix, const std::string& context) {
        current_prefix_ = prefix;
        current_context_ = context;

        // Check cache
        if (!needs_refresh_ && prefix == cached_prefix_ && !cached_completions_.empty()) {
            completions_ = cached_completions_;
            return;
        }

        completions_.clear();

        // Gather completions from all providers
        for (auto& provider : providers_) {
            auto items = provider->getCompletions(prefix, context);
            completions_.insert(completions_.end(), items.begin(), items.end());
        }

        // Sort by priority then alphabetically
        std::sort(completions_.begin(), completions_.end());

        // Remove duplicates (keep highest priority)
        auto it = std::unique(completions_.begin(), completions_.end(),
                              [](const CompletionItem& a, const CompletionItem& b) {
                                  return a.text == b.text;
                              });
        completions_.erase(it, completions_.end());

        // Update cache
        cached_prefix_ = prefix;
        cached_completions_ = completions_;
        needs_refresh_ = false;

        // Reset selection
        selected_index_ = 0;

        // Show popup if we have completions
        if (!completions_.empty() && prefix.length() >= 1) {
            visible_ = true;
        }
    }

    bool AutocompleteManager::renderPopup(const ImVec2& anchor_pos, std::string& selected_text) {
        if (!visible_ || completions_.empty()) {
            return false;
        }

        const auto& t = theme();
        bool selected = false;

        // Calculate dynamic width from content
        float maxWidth = 200.0f;
        for (const auto& item : completions_) {
            float itemWidth = ImGui::CalcTextSize(item.text.c_str()).x;
            if (!item.display.empty() && item.display != item.text) {
                itemWidth += ImGui::CalcTextSize(item.display.c_str()).x + 20.0f;
            }
            maxWidth = std::max(maxWidth, itemWidth + 60.0f);
        }
        maxWidth = std::min(maxWidth, 500.0f);

        ImGui::SetNextWindowPos(anchor_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(maxWidth, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);

        // Don't steal focus from the editor - NoFocusOnAppearing + Tooltip flags handle z-order
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_Tooltip; // Tooltip flag ensures it renders on top

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, t.palette.surface);
        ImGui::PushStyleColor(ImGuiCol_Border, t.palette.border);

        if (ImGui::Begin("##autocomplete_popup", nullptr, flags)) {
            constexpr int MAX_VISIBLE = 8;
            const int num_items = static_cast<int>(completions_.size());
            const int start_idx = std::max(0, selected_index_ - MAX_VISIBLE / 2);
            const int end_idx = std::min(num_items, start_idx + MAX_VISIBLE);

            for (int i = start_idx; i < end_idx; ++i) {
                const auto& item = completions_[i];
                const bool is_selected = (i == selected_index_);

                ImVec4 kind_color;
                const char* kind_icon;
                switch (item.kind) {
                case CompletionKind::Keyword:
                    kind_color = t.palette.primary;
                    kind_icon = "K";
                    break;
                case CompletionKind::Builtin:
                    kind_color = t.palette.warning;
                    kind_icon = "B";
                    break;
                case CompletionKind::Function:
                    kind_color = t.palette.info;
                    kind_icon = "F";
                    break;
                case CompletionKind::Class:
                    kind_color = t.palette.success;
                    kind_icon = "C";
                    break;
                case CompletionKind::Module:
                    kind_color = t.palette.secondary;
                    kind_icon = "M";
                    break;
                case CompletionKind::Property:
                    kind_color = t.palette.text;
                    kind_icon = "P";
                    break;
                case CompletionKind::Decorator:
                    kind_color = t.palette.info;
                    kind_icon = "@";
                    break;
                default:
                    kind_color = t.palette.text_dim;
                    kind_icon = "?";
                    break;
                }

                if (is_selected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, withAlpha(t.palette.primary, 0.3f));
                }

                ImGui::PushID(i);
                if (ImGui::Selectable("##item", is_selected, 0, ImVec2(0, 20))) {
                    selected_text = item.text;
                    selected = true;
                    hide();
                }
                ImGui::PopID();

                if (is_selected) {
                    ImGui::PopStyleColor();
                }

                // Draw content on same line
                ImGui::SameLine(8);
                ImGui::TextColored(kind_color, "%s", kind_icon);
                ImGui::SameLine(28);
                ImGui::TextColored(is_selected ? t.palette.text : t.palette.text, "%s", item.text.c_str());

                // Show signature in dim text
                if (!item.display.empty() && item.display != item.text) {
                    ImGui::SameLine();
                    ImGui::TextColored(t.palette.text_dim, "  %s", item.display.c_str());
                }

                // Tooltip with description
                if (ImGui::IsItemHovered() && !item.description.empty()) {
                    ImGui::SetTooltip("%s", item.description.c_str());
                }
            }

            // Scroll indicator
            if (num_items > MAX_VISIBLE) {
                ImGui::Spacing();
                ImGui::TextColored(t.palette.text_dim, "  %d/%d", selected_index_ + 1, num_items);
            }

            // Keyboard hints
            ImGui::Separator();
            ImGui::TextColored(t.palette.text_dim, "Tab accept | Esc dismiss");
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);

        return selected;
    }

    void AutocompleteManager::selectNext() {
        if (!completions_.empty() && selected_index_ < static_cast<int>(completions_.size()) - 1) {
            ++selected_index_;
        }
    }

    void AutocompleteManager::selectPrevious() {
        if (selected_index_ > 0) {
            --selected_index_;
        }
    }

    bool AutocompleteManager::acceptSelected(std::string& out_text) {
        if (!visible_ || completions_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(completions_.size())) {
            return false;
        }

        out_text = completions_[selected_index_].text;
        hide();
        return true;
    }

} // namespace lfs::vis::editor
