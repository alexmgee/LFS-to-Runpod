/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "control/command_api.hpp"
#include "core/event_bridge/command_center_bridge.hpp"
#include "core/logger.hpp"
#include "core/property_registry.hpp"
#include "py_ui.hpp"
#include "python/python_runtime.hpp"
#include "visualizer/scene/scene_manager.hpp"
#include "visualizer/theme/theme.hpp"

#include <algorithm>
#include <imgui.h>

namespace lfs::python {

    namespace {
        std::string get_class_id(nb::object cls) {
            auto mod = nb::cast<std::string>(cls.attr("__module__"));
            auto name = nb::cast<std::string>(cls.attr("__qualname__"));
            return mod + "." + name;
        }
    } // namespace

    PyPanelRegistry& PyPanelRegistry::instance() {
        static PyPanelRegistry registry;
        return registry;
    }

    void PyPanelRegistry::init() {
        // Subscribe to property changes for automatic UI invalidation
        core::prop::PropertyRegistry::instance().subscribe(
            [](const std::string& /*group_id*/, const std::string& /*prop_id*/,
               const std::any& /*old_val*/, const std::any& /*new_val*/) {
                // Invalidate poll cache when any property changes
                PyPanelRegistry::instance().invalidate_poll_cache();
                // Request UI redraw
                request_redraw();
            });
    }

    void PyPanelRegistry::register_panel(nb::object panel_class) {
        std::lock_guard lock(mutex_);

        if (!panel_class.is_valid()) {
            LOG_ERROR("register_panel: invalid panel_class");
            return;
        }

        std::string label = "Python Panel";
        std::string idname = get_class_id(panel_class);
        PanelSpace space = PanelSpace::Floating;
        int order = 100;
        std::string category;
        std::string parent_id;
        uint32_t options = 0;

        try {
            if (nb::hasattr(panel_class, "label")) {
                label = nb::cast<std::string>(panel_class.attr("label"));
            }
            std::string space_str;
            if (nb::hasattr(panel_class, "space")) {
                space_str = nb::cast<std::string>(panel_class.attr("space"));
            }
            if (!space_str.empty()) {
                if (space_str == "SIDE_PANEL") {
                    space = PanelSpace::SidePanel;
                } else if (space_str == "VIEWPORT_OVERLAY") {
                    space = PanelSpace::ViewportOverlay;
                } else if (space_str == "DOCKABLE") {
                    space = PanelSpace::Dockable;
                } else if (space_str == "PROPERTIES") {
                    space = PanelSpace::SidePanel;
                } else if (space_str == "MAIN_PANEL_TAB") {
                    space = PanelSpace::MainPanelTab;
                } else if (space_str == "SCENE_HEADER") {
                    space = PanelSpace::SceneHeader;
                } else if (space_str == "STATUS_BAR") {
                    space = PanelSpace::StatusBar;
                }
            }
            if (nb::hasattr(panel_class, "order")) {
                order = nb::cast<int>(panel_class.attr("order"));
            }
            if (nb::hasattr(panel_class, "category")) {
                category = nb::cast<std::string>(panel_class.attr("category"));
            }
            if (nb::hasattr(panel_class, "parent_id")) {
                parent_id = nb::cast<std::string>(panel_class.attr("parent_id"));
            }
            nb::object opts;
            if (nb::hasattr(panel_class, "options")) {
                opts = panel_class.attr("options");
            }
            if (opts.is_valid() && nb::isinstance<nb::set>(opts)) {
                nb::set opts_set = nb::cast<nb::set>(opts);
                for (auto item : opts_set) {
                    std::string opt_str = nb::cast<std::string>(item);
                    if (opt_str == "DEFAULT_CLOSED") {
                        options |= static_cast<uint32_t>(PanelOption::DEFAULT_CLOSED);
                    } else if (opt_str == "HIDE_HEADER") {
                        options |= static_cast<uint32_t>(PanelOption::HIDE_HEADER);
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("register_panel: failed to extract attributes: {}", e.what());
            return;
        }

        LOG_DEBUG("Panel '{}' registered (space={})", label, static_cast<int>(space));

        for (auto& p : panels_) {
            if (p.idname == idname) {
                try {
                    p.panel_class = panel_class;
                    p.panel_instance = panel_class();
                    p.label = label;
                    p.space = space;
                    p.order = order;
                    p.category = category;
                    p.parent_id = parent_id;
                    p.options = options;
                } catch (const std::exception& e) {
                    LOG_ERROR("register_panel: failed to update '{}': {}", label, e.what());
                }
                return;
            }
        }

        nb::object instance;
        try {
            instance = panel_class();
        } catch (const std::exception& e) {
            LOG_ERROR("register_panel: failed to create instance for '{}': {}", label, e.what());
            return;
        }

        if (!instance.is_valid()) {
            LOG_ERROR("register_panel: invalid instance for '{}'", label);
            return;
        }

        PyPanelInfo info;
        info.panel_class = panel_class;
        info.panel_instance = instance;
        info.label = label;
        info.idname = idname;
        info.space = space;
        info.order = order;
        info.category = category;
        info.parent_id = parent_id;
        info.options = options;
        info.enabled = true;

        panels_.push_back(std::move(info));
        std::stable_sort(panels_.begin(), panels_.end(), [](const PyPanelInfo& a, const PyPanelInfo& b) {
            if (a.order != b.order)
                return a.order < b.order;
            return a.label < b.label;
        });
    }

    void PyPanelRegistry::unregister_panel(nb::object panel_class) {
        std::lock_guard lock(mutex_);

        std::string idname = get_class_id(panel_class);
        std::erase_if(panels_, [&idname](const PyPanelInfo& p) { return p.idname == idname; });
        poll_cache_.erase(idname);
    }

    void PyPanelRegistry::unregister_all() {
        std::lock_guard lock(mutex_);
        panels_.clear();
        poll_cache_.clear();
    }

    void PyPanelRegistry::invalidate_poll_cache() {
        std::lock_guard lock(mutex_);
        poll_cache_.clear();
    }

    bool PyPanelRegistry::check_poll_cached(const std::string& idname, nb::object panel_class,
                                            PanelPollDependency deps) {
        // Use consolidated context for generation, direct checks for selection and training
        const auto& ctx = context();
        const uint64_t gen = ctx.scene_generation;
        const auto* sm = get_scene_manager();
        const bool has_sel = sm && sm->hasSelectedNode();
        const auto* cc = lfs::event::command_center();
        const bool training = cc ? cc->snapshot().is_running : false;

        // Check cache validity based on DECLARED dependencies
        auto cache_it = poll_cache_.find(idname);
        if (cache_it != poll_cache_.end()) {
            const auto& e = cache_it->second;
            bool valid = true;
            if ((deps & PanelPollDependency::SCENE) != PanelPollDependency::NONE) {
                valid &= (e.scene_generation == gen);
            }
            if ((deps & PanelPollDependency::SELECTION) != PanelPollDependency::NONE) {
                valid &= (e.has_selection == has_sel);
            }
            if ((deps & PanelPollDependency::TRAINING) != PanelPollDependency::NONE) {
                valid &= (e.is_training == training);
            }
            if (valid) {
                return e.result;
            }
        }

        bool result = true;
        if (nb::hasattr(panel_class, "poll")) {
            result = nb::cast<bool>(panel_class.attr("poll")(get_app_context()));
        }

        poll_cache_[idname] = {result, gen, has_sel, training, deps};
        return result;
    }

    void PyPanelRegistry::draw_panels(PanelSpace space) {
        std::vector<std::pair<size_t, PyPanelInfo>> indexed_panels;
        {
            std::lock_guard lock(mutex_);
            indexed_panels.reserve(panels_.size());
            for (size_t i = 0; i < panels_.size(); ++i) {
                indexed_panels.emplace_back(i, panels_[i]);
            }
        }

        if (indexed_panels.empty()) {
            return;
        }

        for (auto& [index, panel] : indexed_panels) {
            if (panel.space != space || !panel.enabled || panel.error_disabled) {
                continue;
            }

            bool draw_succeeded = false;
            try {
                const bool should_draw = (space == PanelSpace::ViewportOverlay)
                                             ? (!nb::hasattr(panel.panel_class, "poll") ||
                                                nb::cast<bool>(panel.panel_class.attr("poll")(get_app_context())))
                                             : check_poll_cached(panel.idname, panel.panel_class, panel.poll_deps);
                if (!should_draw) {
                    continue;
                }

                if (space == PanelSpace::Floating) {
                    bool open = true;
                    if (ImGui::Begin(panel.label.c_str(), &open)) {
                        PyUILayout layout;
                        panel.panel_instance.attr("draw")(layout);
                    }
                    ImGui::End();

                    if (!open) {
                        std::lock_guard lock(mutex_);
                        if (index < panels_.size() && panels_[index].label == panel.label) {
                            panels_[index].enabled = false;
                        }
                    }
                } else if (space == PanelSpace::SidePanel) {
                    ImGuiTreeNodeFlags flags = panel.has_option(PanelOption::DEFAULT_CLOSED)
                                                   ? ImGuiTreeNodeFlags_None
                                                   : ImGuiTreeNodeFlags_DefaultOpen;
                    if (panel.has_option(PanelOption::HIDE_HEADER)) {
                        PyUILayout layout;
                        panel.panel_instance.attr("draw")(layout);
                    } else if (ImGui::CollapsingHeader(panel.label.c_str(), flags)) {
                        PyUILayout layout;
                        panel.panel_instance.attr("draw")(layout);
                    }
                } else if (space == PanelSpace::ViewportOverlay) {
                    PyUILayout layout;
                    panel.panel_instance.attr("draw")(layout);
                } else if (space == PanelSpace::Dockable) {
                    bool open = true;
                    if (ImGui::Begin(panel.label.c_str(), &open)) {
                        PyUILayout layout;
                        panel.panel_instance.attr("draw")(layout);
                    }
                    ImGui::End();

                    if (!open) {
                        std::lock_guard lock(mutex_);
                        if (index < panels_.size() && panels_[index].label == panel.label) {
                            panels_[index].enabled = false;
                        }
                    }
                } else if (space == PanelSpace::SceneHeader) {
                    PyUILayout layout;
                    panel.panel_instance.attr("draw")(layout);
                } else if (space == PanelSpace::StatusBar) {
                    constexpr float STATUS_BAR_HEIGHT = 22.0f;
                    constexpr float PADDING = 8.0f;
                    const auto* vp = ImGui::GetMainViewport();
                    const ImVec2 bar_pos{vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - STATUS_BAR_HEIGHT};
                    const ImVec2 bar_size{vp->WorkSize.x, STATUS_BAR_HEIGHT};

                    ImGui::SetNextWindowPos(bar_pos, ImGuiCond_Always);
                    ImGui::SetNextWindowSize(bar_size, ImGuiCond_Always);

                    constexpr ImGuiWindowFlags FLAGS =
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing;

                    const auto& t = vis::theme();
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, vis::withAlpha(t.palette.background, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_Border, vis::withAlpha(t.palette.border, 0.6f));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {PADDING, 3.0f});
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6.0f, 0.0f});
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

                    if (ImGui::Begin("##StatusBar", nullptr, FLAGS)) {
                        ImGui::GetWindowDrawList()->AddLine(bar_pos, {bar_pos.x + bar_size.x, bar_pos.y},
                                                            vis::toU32(vis::withAlpha(t.palette.surface_bright, 0.4f)), 1.0f);
                        PyUILayout layout;
                        panel.panel_instance.attr("draw")(layout);
                    }
                    ImGui::End();

                    ImGui::PopStyleVar(4);
                    ImGui::PopStyleColor(2);
                }
                draw_succeeded = true;
            } catch (const std::exception& e) {
                LOG_ERROR("Panel '{}' draw error: {}", panel.label, e.what());
            }

            {
                std::lock_guard lock(mutex_);
                if (index < panels_.size() && panels_[index].label == panel.label) {
                    if (draw_succeeded) {
                        panels_[index].consecutive_errors = 0;
                    } else {
                        panels_[index].consecutive_errors++;
                        if (panels_[index].consecutive_errors >= PyPanelInfo::MAX_CONSECUTIVE_ERRORS) {
                            panels_[index].error_disabled = true;
                            LOG_ERROR("Panel '{}' disabled after {} errors",
                                      panel.label, panels_[index].consecutive_errors);
                        }
                    }
                }
            }
        }
    }

    bool PyPanelRegistry::has_panels(PanelSpace space) const {
        std::lock_guard lock(mutex_);
        for (const auto& p : panels_) {
            if (p.space == space && p.enabled) {
                return true;
            }
        }
        return false;
    }

    void PyPanelRegistry::draw_single_panel(const std::string& label) {
        size_t panel_index = 0;
        PyPanelInfo panel_copy;
        bool found = false;
        {
            std::lock_guard lock(mutex_);
            for (size_t i = 0; i < panels_.size(); ++i) {
                if (panels_[i].label == label && panels_[i].enabled && !panels_[i].error_disabled) {
                    panel_copy = panels_[i];
                    panel_index = i;
                    found = true;
                    break;
                }
            }
        }

        if (!found || !panel_copy.panel_instance.is_valid()) {
            return;
        }

        if (!nb::hasattr(panel_copy.panel_instance, "draw")) {
            LOG_ERROR("Panel '{}' has no draw method", label);
            return;
        }

        bool draw_succeeded = false;
        try {
            PyUILayout layout;
            panel_copy.panel_instance.attr("draw")(layout);
            draw_succeeded = true;
        } catch (const std::exception& e) {
            LOG_ERROR("Panel '{}' error: {}", label, e.what());
        }

        {
            std::lock_guard lock(mutex_);
            if (panel_index < panels_.size() && panels_[panel_index].label == label) {
                if (draw_succeeded) {
                    panels_[panel_index].consecutive_errors = 0;
                } else {
                    panels_[panel_index].consecutive_errors++;
                    if (panels_[panel_index].consecutive_errors >= PyPanelInfo::MAX_CONSECUTIVE_ERRORS) {
                        panels_[panel_index].error_disabled = true;
                        LOG_ERROR("Panel '{}' disabled after {} errors",
                                  label, panels_[panel_index].consecutive_errors);
                    }
                }
            }
        }
    }

    std::vector<std::string> PyPanelRegistry::get_panel_names(PanelSpace space) const {
        std::lock_guard lock(mutex_);
        std::vector<std::string> names;
        names.reserve(panels_.size());
        for (const auto& p : panels_) {
            if (p.space == space) {
                names.push_back(p.label);
            }
        }
        return names;
    }

    void PyPanelRegistry::set_panel_enabled(const std::string& label, bool enabled) {
        std::lock_guard lock(mutex_);
        for (auto& p : panels_) {
            if (p.label == label) {
                p.enabled = enabled;
                return;
            }
        }
    }

    bool PyPanelRegistry::is_panel_enabled(const std::string& label) const {
        std::lock_guard lock(mutex_);
        for (const auto& p : panels_) {
            if (p.label == label) {
                return p.enabled;
            }
        }
        return false;
    }

    std::vector<PyPanelInfo*> PyPanelRegistry::get_main_panel_tabs() {
        std::lock_guard lock(mutex_);
        std::vector<PyPanelInfo*> tabs;
        for (auto& p : panels_) {
            if (p.space == PanelSpace::MainPanelTab && p.enabled && !p.error_disabled) {
                tabs.push_back(&p);
            }
        }
        std::stable_sort(tabs.begin(), tabs.end(), [](const PyPanelInfo* a, const PyPanelInfo* b) {
            if (a->order != b->order)
                return a->order < b->order;
            return a->label < b->label;
        });
        return tabs;
    }

    PyPanelInfo* PyPanelRegistry::get_panel(const std::string& idname) {
        std::lock_guard lock(mutex_);
        for (auto& p : panels_) {
            if (p.idname == idname) {
                return &p;
            }
        }
        return nullptr;
    }

    bool PyPanelRegistry::set_panel_label(const std::string& idname, const std::string& new_label) {
        std::lock_guard lock(mutex_);
        for (auto& p : panels_) {
            if (p.idname == idname) {
                p.label = new_label;
                return true;
            }
        }
        return false;
    }

    bool PyPanelRegistry::set_panel_order(const std::string& idname, int new_order) {
        std::lock_guard lock(mutex_);
        for (auto& p : panels_) {
            if (p.idname == idname) {
                p.order = new_order;
                std::stable_sort(panels_.begin(), panels_.end(), [](const PyPanelInfo& a, const PyPanelInfo& b) {
                    if (a.order != b.order)
                        return a.order < b.order;
                    return a.label < b.label;
                });
                return true;
            }
        }
        return false;
    }

    bool PyPanelRegistry::set_panel_space(const std::string& idname, PanelSpace new_space) {
        std::lock_guard lock(mutex_);
        for (auto& p : panels_) {
            if (p.idname == idname) {
                p.space = new_space;
                return true;
            }
        }
        return false;
    }

    void register_ui_panels(nb::module_& m) {
        nb::enum_<PanelSpace>(m, "PanelSpace")
            .value("SIDE_PANEL", PanelSpace::SidePanel)
            .value("FLOATING", PanelSpace::Floating)
            .value("VIEWPORT_OVERLAY", PanelSpace::ViewportOverlay)
            .value("DOCKABLE", PanelSpace::Dockable)
            .value("MAIN_PANEL_TAB", PanelSpace::MainPanelTab)
            .value("SCENE_HEADER", PanelSpace::SceneHeader)
            .value("STATUS_BAR", PanelSpace::StatusBar);

        m.def(
            "register_panel",
            [](nb::object cls) { PyPanelRegistry::instance().register_panel(cls); },
            nb::arg("cls"),
            "Register a panel class for rendering in the UI");

        m.def(
            "unregister_panel",
            [](nb::object cls) { PyPanelRegistry::instance().unregister_panel(cls); },
            nb::arg("cls"),
            "Unregister a panel class");

        m.def(
            "unregister_all_panels", []() {
                PyPanelRegistry::instance().unregister_all();
            },
            "Unregister all Python panels");

        m.def(
            "get_panel_names", [](const std::string& space) {
                PanelSpace ps = PanelSpace::Floating;
                if (space == "SIDE_PANEL") {
                    ps = PanelSpace::SidePanel;
                } else if (space == "VIEWPORT_OVERLAY") {
                    ps = PanelSpace::ViewportOverlay;
                } else if (space == "DOCKABLE") {
                    ps = PanelSpace::Dockable;
                } else if (space == "SCENE_HEADER") {
                    ps = PanelSpace::SceneHeader;
                } else if (space == "STATUS_BAR") {
                    ps = PanelSpace::StatusBar;
                }
                return PyPanelRegistry::instance().get_panel_names(ps);
            },
            nb::arg("space") = "FLOATING", "Get registered panel names for a given space");

        m.def(
            "set_panel_enabled", [](const std::string& label, bool enabled) {
                PyPanelRegistry::instance().set_panel_enabled(label, enabled);
            },
            nb::arg("label"), nb::arg("enabled"), "Enable or disable a panel by label");

        m.def(
            "is_panel_enabled", [](const std::string& label) {
                return PyPanelRegistry::instance().is_panel_enabled(label);
            },
            nb::arg("label"), "Check if a panel is enabled");

        m.def(
            "get_main_panel_tabs", []() {
                auto tabs = PyPanelRegistry::instance().get_main_panel_tabs();
                nb::list result;
                for (auto* tab : tabs) {
                    nb::dict info;
                    info["idname"] = tab->idname;
                    info["label"] = tab->label;
                    info["order"] = tab->order;
                    info["enabled"] = tab->enabled;
                    result.append(info);
                }
                return result;
            },
            "Get all main panel tabs as list of dicts");

        m.def(
            "get_panel", [](const std::string& idname) -> nb::object {
                auto* panel = PyPanelRegistry::instance().get_panel(idname);
                if (!panel) {
                    return nb::none();
                }
                nb::dict info;
                info["idname"] = panel->idname;
                info["label"] = panel->label;
                info["order"] = panel->order;
                info["enabled"] = panel->enabled;
                info["space"] = static_cast<int>(panel->space);
                return info;
            },
            nb::arg("idname"), "Get panel info by idname (None if not found)");

        m.def(
            "set_panel_label", [](const std::string& idname, const std::string& new_label) {
                return PyPanelRegistry::instance().set_panel_label(idname, new_label);
            },
            nb::arg("idname"), nb::arg("label"), "Set the display label for a panel");

        m.def(
            "set_panel_order", [](const std::string& idname, int new_order) {
                return PyPanelRegistry::instance().set_panel_order(idname, new_order);
            },
            nb::arg("idname"), nb::arg("order"), "Set the sort order for a panel");

        m.def(
            "set_panel_space", [](const std::string& idname, const std::string& space_str) {
                PanelSpace space = PanelSpace::Floating;
                if (space_str == "SIDE_PANEL") {
                    space = PanelSpace::SidePanel;
                } else if (space_str == "VIEWPORT_OVERLAY") {
                    space = PanelSpace::ViewportOverlay;
                } else if (space_str == "DOCKABLE") {
                    space = PanelSpace::Dockable;
                } else if (space_str == "MAIN_PANEL_TAB") {
                    space = PanelSpace::MainPanelTab;
                } else if (space_str == "SCENE_HEADER") {
                    space = PanelSpace::SceneHeader;
                } else if (space_str == "STATUS_BAR") {
                    space = PanelSpace::StatusBar;
                }
                return PyPanelRegistry::instance().set_panel_space(idname, space);
            },
            nb::arg("idname"), nb::arg("space"), "Set the panel space (where it renders)");

        m.def(
            "has_main_panel_tabs", []() {
                return PyPanelRegistry::instance().has_panels(PanelSpace::MainPanelTab);
            },
            "Check if any main panel tabs are registered");
    }

} // namespace lfs::python
