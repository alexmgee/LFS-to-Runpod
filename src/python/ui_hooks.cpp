/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "ui_hooks.hpp"
#include "core/logger.hpp"
#include "python_runtime.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace lfs::python {

    namespace {
        struct HookEntry {
            UIHookCallback callback;
            HookPosition position;
            size_t id; // Unique ID for removal
        };

        struct HookRegistry {
            std::mutex mutex;
            std::unordered_map<std::string, std::vector<HookEntry>> hooks;
            size_t next_id = 0;

            static HookRegistry& instance() {
                static HookRegistry registry;
                return registry;
            }

            std::string make_key(const std::string& panel, const std::string& section) {
                return panel + ":" + section;
            }
        };
    } // namespace

    void register_ui_hook(const std::string& panel,
                          const std::string& section,
                          UIHookCallback callback,
                          HookPosition position) {
        auto& reg = HookRegistry::instance();
        std::lock_guard lock(reg.mutex);

        const std::string key = reg.make_key(panel, section);

        HookEntry entry;
        entry.callback = std::move(callback);
        entry.position = position;
        entry.id = reg.next_id++;

        reg.hooks[key].push_back(std::move(entry));

        LOG_INFO("Registered UI hook: {}:{} (position={})",
                 panel, section, position == HookPosition::Prepend ? "prepend" : "append");
    }

    void remove_ui_hook(const std::string& panel,
                        const std::string& section,
                        UIHookCallback /*callback*/) {
        auto& reg = HookRegistry::instance();
        std::lock_guard lock(reg.mutex);

        const std::string key = reg.make_key(panel, section);
        auto it = reg.hooks.find(key);
        if (it != reg.hooks.end() && !it->second.empty()) {
            // Remove the last added hook for this key
            // (proper callback comparison would require additional infrastructure)
            it->second.pop_back();
            LOG_INFO("Removed UI hook: {}:{}", panel, section);
        }
    }

    void clear_ui_hooks(const std::string& panel, const std::string& section) {
        auto& reg = HookRegistry::instance();
        std::lock_guard lock(reg.mutex);

        if (section.empty()) {
            // Clear all hooks for this panel
            std::vector<std::string> to_remove;
            const std::string prefix = panel + ":";
            for (const auto& [key, _] : reg.hooks) {
                if (key.starts_with(prefix)) {
                    to_remove.push_back(key);
                }
            }
            for (const auto& key : to_remove) {
                reg.hooks.erase(key);
            }
            LOG_INFO("Cleared all UI hooks for panel: {}", panel);
        } else {
            const std::string key = reg.make_key(panel, section);
            reg.hooks.erase(key);
            LOG_INFO("Cleared UI hooks: {}:{}", panel, section);
        }
    }

    void clear_all_ui_hooks() {
        auto& reg = HookRegistry::instance();
        std::lock_guard lock(reg.mutex);
        reg.hooks.clear();
        LOG_INFO("Cleared all UI hooks");
    }

    bool has_ui_hooks(const std::string& panel, const std::string& section) {
        auto& reg = HookRegistry::instance();
        std::lock_guard lock(reg.mutex);

        const std::string key = reg.make_key(panel, section);
        auto it = reg.hooks.find(key);
        return it != reg.hooks.end() && !it->second.empty();
    }

    void invoke_ui_hooks(const std::string& panel,
                         const std::string& section,
                         HookPosition position) {
        std::vector<UIHookCallback> callbacks_to_invoke;

        {
            auto& reg = HookRegistry::instance();
            std::lock_guard lock(reg.mutex);

            const std::string key = reg.make_key(panel, section);
            auto it = reg.hooks.find(key);
            if (it == reg.hooks.end()) {
                return;
            }

            for (const auto& entry : it->second) {
                if (entry.position == position) {
                    callbacks_to_invoke.push_back(entry.callback);
                }
            }
        }

        if (callbacks_to_invoke.empty()) {
            return;
        }

        // Callbacks are invoked outside the lock
        // The actual Python callback invocation happens in py_ui.cpp
        // which handles GIL acquisition
        for (const auto& cb : callbacks_to_invoke) {
            try {
                cb(nullptr); // Layout is created by the Python wrapper
            } catch (const std::exception& e) {
                LOG_ERROR("UI hook {}:{} error: {}", panel, section, e.what());
            }
        }
    }

    std::vector<std::string> get_registered_hook_points() {
        auto& reg = HookRegistry::instance();
        std::lock_guard lock(reg.mutex);

        std::vector<std::string> points;
        points.reserve(reg.hooks.size());
        for (const auto& [key, hooks] : reg.hooks) {
            if (!hooks.empty()) {
                points.push_back(key);
            }
        }
        return points;
    }

    namespace {
        PythonHookInvoker g_python_hook_invoker;
        PythonHookChecker g_python_hook_checker;
        std::mutex g_invoker_mutex;
    } // namespace

    void set_python_hook_invoker(PythonHookInvoker invoker) {
        std::lock_guard lock(g_invoker_mutex);
        g_python_hook_invoker = std::move(invoker);
        LOG_INFO("Python hook invoker registered");
    }

    void set_python_hook_checker(PythonHookChecker checker) {
        std::lock_guard lock(g_invoker_mutex);
        g_python_hook_checker = std::move(checker);
    }

    void clear_python_hook_invoker() {
        std::lock_guard lock(g_invoker_mutex);
        g_python_hook_invoker = nullptr;
        g_python_hook_checker = nullptr;
    }

    void invoke_python_hooks(const std::string& panel, const std::string& section, bool prepend) {
        PythonHookInvoker invoker;
        {
            std::lock_guard lock(g_invoker_mutex);
            invoker = g_python_hook_invoker;
        }
        if (invoker) {
            if (bridge().prepare_ui)
                bridge().prepare_ui();
            invoker(panel.c_str(), section.c_str(), prepend);
        } else {
            static bool warned_scene = false;
            static bool warned_training = false;
            bool* warned = (panel == "scene") ? &warned_scene : &warned_training;
            if (!*warned) {
                LOG_INFO("Python hook invoker not set for {}:{} (Python module not loaded?)", panel, section);
                *warned = true;
            }
        }
    }

    bool has_python_hooks(const std::string& panel, const std::string& section) {
        const PythonHookChecker checker = [&] {
            std::lock_guard lock(g_invoker_mutex);
            return g_python_hook_checker;
        }();
        return checker ? checker(panel.c_str(), section.c_str()) : false;
    }

} // namespace lfs::python
