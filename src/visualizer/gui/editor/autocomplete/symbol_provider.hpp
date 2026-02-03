/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <string>
#include <vector>

namespace lfs::vis::editor {

    enum class CompletionKind {
        Keyword,
        Builtin,
        Module,
        Function,
        Class,
        Variable,
        Property,
        Decorator
    };

    struct CompletionItem {
        std::string text;        // Completion text to insert
        std::string display;     // Display with type info
        std::string description; // Tooltip/signature
        CompletionKind kind;
        int priority = 0; // Higher = shown first

        bool operator<(const CompletionItem& other) const {
            if (priority != other.priority)
                return priority > other.priority;
            return text < other.text;
        }
    };

    // Interface for symbol providers
    class ISymbolProvider {
    public:
        virtual ~ISymbolProvider() = default;

        // Get completions matching prefix, optionally filtered by context
        virtual std::vector<CompletionItem> getCompletions(
            const std::string& prefix,
            const std::string& context = "") = 0;

        // Provider name for debugging
        virtual const char* name() const = 0;
    };

} // namespace lfs::vis::editor
