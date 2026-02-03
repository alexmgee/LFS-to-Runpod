/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "tool_base.hpp"

namespace lfs::vis::tools {

    class AlignTool : public ToolBase {
    public:
        AlignTool();
        ~AlignTool() override = default;

        std::string_view getName() const override { return "Align Tool"; }
        std::string_view getDescription() const override { return "3-point alignment to world axes"; }

        bool initialize(const ToolContext& ctx) override;
        void shutdown() override;
        void update(const ToolContext& ctx) override;
        void renderUI(const lfs::vis::gui::UIContext& ui_ctx, bool* p_open) override;

    protected:
        void onEnabledChanged(bool enabled) override;

    private:
        const ToolContext* tool_context_ = nullptr;
    };

} // namespace lfs::vis::tools
