/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/tensor.hpp"
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

        [[nodiscard]] bool isActive() const { return is_dragging_; }
        bool handleMouseButton(int button, int action, int mods, double x, double y, const ToolContext& ctx);
        bool handleMouseMove(double x, double y, const ToolContext& ctx);
        bool handleScroll(double x_offset, double y_offset, int mods, const ToolContext& ctx);
        bool handleKeyPress(int key, int mods, const ToolContext& ctx);

        [[nodiscard]] float getBrushRadius() const { return brush_radius_; }
        void setBrushRadius(float radius) { brush_radius_ = std::clamp(radius, 1.0f, 500.0f); }

        [[nodiscard]] bool hasActivePolygon() const { return !polygon_points_.empty(); }
        void clearPolygon();
        void onSelectionModeChanged();

        // Depth filter
        [[nodiscard]] bool isDepthFilterEnabled() const { return depth_filter_enabled_; }
        void resetDepthFilter();

        // Check if crop box or ellipsoid is selected (disables brush painting)
        [[nodiscard]] bool isCropBoxSelected() const;

        // Input bindings
        void setInputBindings(const input::InputBindings* bindings) { input_bindings_ = bindings; }

    protected:
        void onEnabledChanged(bool enabled) override;

    private:
        // ========== Selection State ==========
        SelectionOp current_op_ = SelectionOp::Replace;
        lfs::core::Tensor stroke_selection_; // Accumulated selection during current stroke
        std::shared_ptr<lfs::core::Tensor> selection_before_stroke_;

        // ========== Interaction State ==========
        bool is_dragging_ = false;
        glm::vec2 last_mouse_pos_{0.0f};
        glm::vec2 last_stroke_pos_{0.0f}; // Last position during active stroke (for interpolation)
        float brush_radius_ = 20.0f;
        const ToolContext* tool_context_ = nullptr;

        // Rectangle selection
        glm::vec2 rect_start_{0.0f};
        glm::vec2 rect_end_{0.0f};

        // Lasso selection
        std::vector<glm::vec2> lasso_points_;

        // Polygon selection
        std::vector<glm::vec2> polygon_points_;
        bool polygon_closed_ = false;
        int polygon_dragged_vertex_ = -1;
        static constexpr float POLYGON_VERTEX_RADIUS = 6.0f;
        static constexpr float POLYGON_CLOSE_THRESHOLD = 12.0f;

        // Preview
        lfs::core::Tensor preview_selection_;

        // ========== Core Methods ==========
        // Determine operation from modifier keys
        SelectionOp getOpFromModifiers(int mods) const;

        // Begin a selection stroke (brush/rect/lasso)
        void beginStroke(const ToolContext& ctx);

        // Finalize and apply selection to scene
        void finalizeSelection(const ToolContext& ctx);

        // ========== Mode-Specific Methods ==========
        void updateBrushSelection(double x, double y, const ToolContext& ctx);
        void updateBrushPreview(double x, double y, const ToolContext& ctx);
        void computeRectSelection(const ToolContext& ctx);
        void computeLassoSelection(const ToolContext& ctx);
        void computePolygonSelection(const ToolContext& ctx);

        // Preview updates
        void updateRectanglePreview(const ToolContext& ctx);
        void updateLassoPreview(const ToolContext& ctx);
        void updatePolygonPreview(const ToolContext& ctx);
        void clearPreview(const ToolContext& ctx);

        // Polygon helpers
        void resetPolygon();
        int findPolygonVertexAt(float x, float y) const;
        int findPolygonEdgeAt(float x, float y, float& t_out) const;

        // ========== Depth Filter ==========
        bool depth_filter_enabled_ = false;
        float depth_far_ = 100.0f;
        float frustum_half_width_ = 50.0f;

        static constexpr float DEPTH_MIN = 0.01f;
        static constexpr float DEPTH_MAX = 1000.0f;
        static constexpr float WIDTH_MIN = 0.1f;
        static constexpr float WIDTH_MAX = 10000.0f;
        static constexpr float ADJUST_FACTOR = 1.15f;

        void drawDepthFrustum(const ToolContext& ctx) const;
        void updateSelectionCropBox(const ToolContext& ctx);
        void disableDepthFilter(const ToolContext& ctx);

        // Input bindings
        const input::InputBindings* input_bindings_ = nullptr;
    };

} // namespace lfs::vis::tools
