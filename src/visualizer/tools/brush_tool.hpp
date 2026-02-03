/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "tool_base.hpp"
#include <glm/glm.hpp>

namespace lfs::vis::tools {

    enum class BrushMode { Select,
                           Saturation };

    class BrushTool : public ToolBase {
    public:
        BrushTool();
        ~BrushTool() override = default;

        std::string_view getName() const override { return "Brush Tool"; }
        std::string_view getDescription() const override { return "Paint to select or adjust Gaussians"; }

        bool initialize(const ToolContext& ctx) override;
        void shutdown() override;
        void update(const ToolContext& ctx) override;
        void renderUI(const lfs::vis::gui::UIContext& ui_ctx, bool* p_open) override;

        float getBrushRadius() const { return brush_radius_; }
        void setBrushRadius(float radius) { brush_radius_ = std::clamp(radius, 1.0f, 500.0f); }
        BrushMode getMode() const { return current_mode_; }
        void setMode(BrushMode mode) { current_mode_ = mode; }
        float getSaturationAmount() const { return saturation_amount_; }
        void setSaturationAmount(float amount) { saturation_amount_ = std::clamp(amount, -1.0f, 1.0f); }

    protected:
        void onEnabledChanged(bool enabled) override;

    private:
        BrushMode current_mode_ = BrushMode::Select;
        float brush_radius_ = 20.0f;
        float saturation_amount_ = 0.5f;
        glm::vec2 last_mouse_pos_{0.0f};
        const ToolContext* tool_context_ = nullptr;
    };

} // namespace lfs::vis::tools
