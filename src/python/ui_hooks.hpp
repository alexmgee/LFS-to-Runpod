/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lfs::python {

    enum class HookPosition {
        Prepend, // Run before native content
        Append   // Run after native content
    };

    // Callback type for UI hooks - receives a PyUILayout pointer
    // The void* is opaque to avoid nanobind dependency in this header
    using UIHookCallback = std::function<void(void* layout)>;

    // Register a UI hook callback
    // panel: Name of the panel (e.g., "training", "scene", "rendering")
    // section: Section within the panel (e.g., "header", "stats", "footer")
    // callback: Function to call when hook is invoked
    // position: Whether to run before or after native content
    void register_ui_hook(const std::string& panel,
                          const std::string& section,
                          UIHookCallback callback,
                          HookPosition position = HookPosition::Append);

    // Remove a UI hook
    void remove_ui_hook(const std::string& panel,
                        const std::string& section,
                        UIHookCallback callback);

    // Remove all hooks for a panel/section
    void clear_ui_hooks(const std::string& panel, const std::string& section = "");

    // Clear all registered hooks
    void clear_all_ui_hooks();

    // Check if any hooks are registered for a panel/section
    bool has_ui_hooks(const std::string& panel, const std::string& section);

    // Invoke all hooks for a panel/section
    // This is called from C++ panel code
    // position: Which hooks to invoke (Prepend or Append)
    void invoke_ui_hooks(const std::string& panel,
                         const std::string& section,
                         HookPosition position);

    // Get list of registered hook points (for debugging/introspection)
    std::vector<std::string> get_registered_hook_points();

    // Python hook invoker (C-style pointer for DLL boundary safety)
    using PythonHookInvoker = void (*)(const char* panel, const char* section, bool prepend);
    using PythonHookChecker = bool (*)(const char* panel, const char* section);

    void set_python_hook_invoker(PythonHookInvoker invoker);
    void set_python_hook_checker(PythonHookChecker checker);
    void clear_python_hook_invoker();
    void invoke_python_hooks(const std::string& panel, const std::string& section, bool prepend);
    bool has_python_hooks(const std::string& panel, const std::string& section);

} // namespace lfs::python
