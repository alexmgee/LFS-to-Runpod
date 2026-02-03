/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "tool_base.hpp"
#include "tools/selection_operation.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace lfs::vis::input {
    class InputBindings;
}

namespace lfs::vis::tools {

    class SelectionTool : public ToolBase {
    public:
        SelectionTool();
        ~SelectionTool() override = default;

        [[nodiscard]] std::string_view getName() const override { return "Selection Tool"; }
        [[nodiscard]] std::string_view getDescription() const override { return "Paint to select Gaussians"; }

        bool initialize(const ToolContext& ctx) override;
        void shutdown() override;
        void update(const ToolContext& ctx) override;
        void renderUI(const lfs::vis::gui::UIContext& ui_ctx, bool* p_open) override;

        [[nodiscard]] float getBrushRadius() const { return brush_radius_; }
        void setBrushRadius(float radius) { brush_radius_ = std::clamp(radius, 1.0f, 500.0f); }

        [[nodiscard]] bool hasActivePolygon() const { return !polygon_points_.empty(); }
        void clearPolygon();
        void onSelectionModeChanged();

        // Depth filter
        [[nodiscard]] bool isDepthFilterEnabled() const { return depth_filter_enabled_; }
        void resetDepthFilter();

        // Crop filter (use scene crop box/ellipsoid as selection filter)
        [[nodiscard]] bool isCropFilterEnabled() const { return crop_filter_enabled_; }
        void setCropFilterEnabled(bool enabled);

        // Input bindings
        void setInputBindings(const input::InputBindings* bindings) { input_bindings_ = bindings; }

    protected:
        void onEnabledChanged(bool enabled) override;

    private:
        // Interaction state
        glm::vec2 last_mouse_pos_{0.0f};
        float brush_radius_ = 20.0f;
        const ToolContext* tool_context_ = nullptr;

        // Polygon selection (legacy - not yet moved to operator)
        std::vector<glm::vec2> polygon_points_;
        bool polygon_closed_ = false;
        static constexpr float POLYGON_VERTEX_RADIUS = 6.0f;
        static constexpr float POLYGON_CLOSE_THRESHOLD = 12.0f;

        // Determine operation from modifier keys
        SelectionOp getOpFromModifiers(int mods) const;

        // Polygon helpers
        void resetPolygon();
        int findPolygonVertexAt(float x, float y) const;

        // Depth filter
        bool depth_filter_enabled_ = false;
        float depth_far_ = 100.0f;
        float frustum_half_width_ = 50.0f;

        // Crop filter
        bool crop_filter_enabled_ = false;
        std::string node_before_crop_filter_;

        static constexpr float DEPTH_MIN = 0.01f;
        static constexpr float DEPTH_MAX = 1000.0f;
        static constexpr float WIDTH_MIN = 0.1f;
        static constexpr float WIDTH_MAX = 10000.0f;

        void drawDepthFrustum(const ToolContext& ctx) const;
        void updateSelectionCropBox(const ToolContext& ctx);
        void disableDepthFilter(const ToolContext& ctx);

        // Input bindings
        const input::InputBindings* input_bindings_ = nullptr;
    };

} // namespace lfs::vis::tools
