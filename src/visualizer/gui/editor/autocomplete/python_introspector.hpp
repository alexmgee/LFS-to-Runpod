/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "symbol_provider.hpp"
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace lfs::vis::editor {

    // Runtime Python symbol provider using introspection
    class PythonIntrospector : public ISymbolProvider {
    public:
        PythonIntrospector();
        ~PythonIntrospector() override;

        std::vector<CompletionItem> getCompletions(
            const std::string& prefix,
            const std::string& context = "") override;

        const char* name() const override { return "PythonIntrospector"; }

        // Refresh runtime symbols from Python namespace
        void refresh();

        // Check if we should auto-refresh (based on time since last refresh)
        bool shouldRefresh() const;

    private:
        void introspectGlobals();
        void introspectObject(const std::string& obj_expr, std::vector<CompletionItem>& out);

        std::vector<CompletionItem> cached_globals_;
        mutable std::mutex mutex_;
        std::chrono::steady_clock::time_point last_refresh_;
        static constexpr auto REFRESH_INTERVAL = std::chrono::seconds(1);
    };

} // namespace lfs::vis::editor
