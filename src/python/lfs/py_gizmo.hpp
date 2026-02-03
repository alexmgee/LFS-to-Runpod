/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nb = nanobind;

namespace lfs::python {

    enum class GizmoEventType { Press,
                                Release,
                                Move,
                                Drag };

    struct PyGizmoEvent {
        GizmoEventType type = GizmoEventType::Move;
        int button = 0;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        float delta_x = 0.0f;
        float delta_y = 0.0f;
        bool shift = false;
        bool ctrl = false;
        bool alt = false;
    };

    enum class GizmoResult { PassThrough,
                             Running,
                             Finished,
                             Cancelled };

    class PyGizmoContext {
    public:
        struct DrawCommand {
            enum Type { LINE_2D,
                        CIRCLE_2D,
                        RECT_2D,
                        FILLED_RECT_2D,
                        FILLED_CIRCLE_2D,
                        LINE_3D };
            Type type;
            float x1, y1, z1;
            float x2, y2, z2;
            float r, g, b, a;
            float thickness;
            float radius;
        };

        [[nodiscard]] bool has_selection() const;
        [[nodiscard]] std::tuple<float, float, float> selection_center() const;
        [[nodiscard]] std::tuple<float, float> selection_center_screen() const;
        [[nodiscard]] std::tuple<float, float, float> camera_position() const;
        [[nodiscard]] std::tuple<float, float, float> camera_forward() const;
        [[nodiscard]] std::optional<std::tuple<float, float>> world_to_screen(std::tuple<float, float, float> pos) const;
        [[nodiscard]] std::optional<std::tuple<float, float, float>> screen_to_world_ray(std::tuple<float, float> pos) const;

        void draw_line_2d(std::tuple<float, float> start, std::tuple<float, float> end,
                          std::tuple<float, float, float, float> color, float thickness = 1.0f);
        void draw_circle_2d(std::tuple<float, float> center, float radius,
                            std::tuple<float, float, float, float> color, float thickness = 1.0f);
        void draw_rect_2d(std::tuple<float, float> min, std::tuple<float, float> max,
                          std::tuple<float, float, float, float> color, float thickness = 1.0f);
        void draw_filled_rect_2d(std::tuple<float, float> min, std::tuple<float, float> max,
                                 std::tuple<float, float, float, float> color);
        void draw_filled_circle_2d(std::tuple<float, float> center, float radius,
                                   std::tuple<float, float, float, float> color);
        void draw_line_3d(std::tuple<float, float, float> start, std::tuple<float, float, float> end,
                          std::tuple<float, float, float, float> color, float thickness = 1.0f);

        [[nodiscard]] const std::vector<DrawCommand>& get_draw_commands() const { return draw_commands_; }
        void clear_draw_commands() { draw_commands_.clear(); }

    private:
        mutable std::vector<DrawCommand> draw_commands_;
    };

    struct PyGizmoInfo {
        std::string id;
        nb::object gizmo_class;
        nb::object gizmo_instance;
        bool has_poll = false;
        bool has_draw = false;
        bool has_handle_mouse = false;
    };

    class PyGizmoRegistry {
    public:
        static PyGizmoRegistry& instance();

        void register_gizmo(nb::object gizmo_class);
        void unregister_gizmo(const std::string& id);
        void unregister_all();

        [[nodiscard]] bool poll(const std::string& id);
        void draw_all(PyGizmoContext& ctx);
        [[nodiscard]] GizmoResult handle_mouse(const std::string& id, PyGizmoContext& ctx, const PyGizmoEvent& event);

        [[nodiscard]] std::vector<std::string> get_gizmo_ids() const;
        [[nodiscard]] bool has_gizmos() const;

    private:
        PyGizmoRegistry() = default;
        PyGizmoRegistry(const PyGizmoRegistry&) = delete;
        PyGizmoRegistry& operator=(const PyGizmoRegistry&) = delete;

        PyGizmoInfo* ensure_instance(PyGizmoInfo& gizmo);

        mutable std::mutex mutex_;
        std::unordered_map<std::string, PyGizmoInfo> gizmos_;
    };

    void register_gizmos(nb::module_& m);

} // namespace lfs::python
