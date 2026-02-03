/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "symbol_provider.hpp"
#include <memory>
#include <string>
#include <vector>
#include <imgui.h>

namespace lfs::vis::editor {

    class AutocompleteManager {
    public:
        AutocompleteManager();
        ~AutocompleteManager();

        // Add a provider
        void addProvider(std::unique_ptr<ISymbolProvider> provider);

        // Update completions for given prefix and context
        void updateCompletions(const std::string& prefix, const std::string& context = "");

        // Render the autocomplete popup
        // Returns true if a completion was selected
        // selected_text receives the text to insert
        bool renderPopup(const ImVec2& anchor_pos, std::string& selected_text);

        // Show/hide the popup
        void show() { visible_ = true; }
        void hide() {
            visible_ = false;
            selected_index_ = 0;
        }
        bool isVisible() const { return visible_; }

        // Navigation
        void selectNext();
        void selectPrevious();
        bool acceptSelected(std::string& out_text);

        // Force refresh completions
        void refresh() { needs_refresh_ = true; }

        // Check if completions are available
        bool hasCompletions() const { return !completions_.empty(); }

        // Get current prefix being completed
        const std::string& currentPrefix() const { return current_prefix_; }

    private:
        std::vector<std::unique_ptr<ISymbolProvider>> providers_;
        std::vector<CompletionItem> completions_;
        std::string current_prefix_;
        std::string current_context_;
        int selected_index_ = 0;
        bool visible_ = false;
        bool needs_refresh_ = false;

        // Cache
        std::string cached_prefix_;
        std::vector<CompletionItem> cached_completions_;
    };

} // namespace lfs::vis::editor
