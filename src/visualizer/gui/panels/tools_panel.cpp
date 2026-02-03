/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "gui/panels/tools_panel.hpp"
#include "python/ui_hooks.hpp"

namespace lfs::vis::gui::panels {

    void DrawToolsPanel(const UIContext& /*ctx*/) {
        python::invoke_python_hooks("tools", "transform", true);
        python::invoke_python_hooks("tools", "transform", false);
    }

} // namespace lfs::vis::gui::panels
