/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "view_context.hpp"
#include "python/python_runtime.hpp"

namespace lfs::vis {

    namespace py = lfs::python;

    struct ViewContextState {
        GetViewCallback view_callback;
        GetViewportRenderCallback viewport_render_callback;
        GetRenderSettingsCallback get_render_settings_callback;
        SetRenderSettingsCallback set_render_settings_callback;
    };

    static ViewContextState& state() {
        auto* p = static_cast<ViewContextState*>(py::get_view_context_state());
        if (!p) {
            p = new ViewContextState();
            py::set_view_context_state(p);
        }
        return *p;
    }

    void set_view_callback(GetViewCallback callback) {
        state().view_callback = std::move(callback);
    }

    void set_viewport_render_callback(GetViewportRenderCallback callback) {
        state().viewport_render_callback = std::move(callback);
    }

    std::optional<ViewInfo> get_current_view_info() {
        const auto& s = state();
        if (!s.view_callback)
            return std::nullopt;
        return s.view_callback();
    }

    std::optional<ViewportRender> get_viewport_render() {
        const auto& s = state();
        if (!s.viewport_render_callback)
            return std::nullopt;
        return s.viewport_render_callback();
    }

    void set_render_settings_callbacks(GetRenderSettingsCallback get_cb, SetRenderSettingsCallback set_cb) {
        auto& s = state();
        s.get_render_settings_callback = std::move(get_cb);
        s.set_render_settings_callback = std::move(set_cb);
    }

    std::optional<RenderSettingsProxy> get_render_settings() {
        const auto& s = state();
        if (!s.get_render_settings_callback)
            return std::nullopt;
        return s.get_render_settings_callback();
    }

    void update_render_settings(const RenderSettingsProxy& settings) {
        const auto& s = state();
        if (s.set_render_settings_callback) {
            s.set_render_settings_callback(settings);
        }
    }

} // namespace lfs::vis
