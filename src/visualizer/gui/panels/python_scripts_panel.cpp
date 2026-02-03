/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "gui/panels/python_scripts_panel.hpp"

namespace lfs::vis::gui::panels {

    PythonScriptManagerState& PythonScriptManagerState::getInstance() {
        static PythonScriptManagerState instance;
        return instance;
    }

    void PythonScriptManagerState::addScript(const std::filesystem::path& path) {
        std::lock_guard lock(mutex_);
        for (const auto& s : scripts_) {
            if (s.path == path)
                return;
        }
        scripts_.push_back({path, true, false, ""});
    }

    void PythonScriptManagerState::setScripts(const std::vector<std::filesystem::path>& paths) {
        std::lock_guard lock(mutex_);
        scripts_.clear();
        for (const auto& p : paths) {
            scripts_.push_back({p, true, false, ""});
        }
    }

    void PythonScriptManagerState::setScriptEnabled(size_t index, bool enabled) {
        std::lock_guard lock(mutex_);
        if (index < scripts_.size()) {
            scripts_[index].enabled = enabled;
        }
    }

    void PythonScriptManagerState::setScriptError(size_t index, const std::string& error) {
        std::lock_guard lock(mutex_);
        if (index < scripts_.size()) {
            scripts_[index].has_error = !error.empty();
            scripts_[index].error_message = error;
        }
    }

    void PythonScriptManagerState::clearErrors() {
        std::lock_guard lock(mutex_);
        for (auto& s : scripts_) {
            s.has_error = false;
            s.error_message.clear();
        }
    }

    void PythonScriptManagerState::clear() {
        std::lock_guard lock(mutex_);
        scripts_.clear();
        needs_reload_ = false;
    }

    std::vector<std::filesystem::path> PythonScriptManagerState::enabledScripts() const {
        std::lock_guard lock(mutex_);
        std::vector<std::filesystem::path> result;
        for (const auto& s : scripts_) {
            if (s.enabled) {
                result.push_back(s.path);
            }
        }
        return result;
    }

} // namespace lfs::vis::gui::panels
