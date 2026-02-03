/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

namespace lfs::core {

    // Modal event for input routing to Python operators
    struct ModalEvent {
        enum class Type { MouseButton,
                          MouseMove,
                          Scroll,
                          Key };
        Type type;

        double x, y;
        double delta_x, delta_y;
        int button;
        int action;
        int key;
        int mods;
        double scroll_x, scroll_y;
        bool over_gui = false;
    };

} // namespace lfs::core
