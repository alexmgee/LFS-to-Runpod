/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "symbol_provider.hpp"
#include <memory>

namespace lfs::vis::editor {

    // Python keywords provider
    class PythonKeywordsProvider : public ISymbolProvider {
    public:
        std::vector<CompletionItem> getCompletions(
            const std::string& prefix,
            const std::string& context = "") override;
        const char* name() const override { return "PythonKeywords"; }
    };

    // Python builtins provider
    class PythonBuiltinsProvider : public ISymbolProvider {
    public:
        std::vector<CompletionItem> getCompletions(
            const std::string& prefix,
            const std::string& context = "") override;
        const char* name() const override { return "PythonBuiltins"; }
    };

    // Lichtfeld API provider
    class LichtfeldApiProvider : public ISymbolProvider {
    public:
        std::vector<CompletionItem> getCompletions(
            const std::string& prefix,
            const std::string& context = "") override;
        const char* name() const override { return "LichtfeldApi"; }
    };

    // Factory to create all static providers
    std::vector<std::unique_ptr<ISymbolProvider>> createStaticProviders();

} // namespace lfs::vis::editor
