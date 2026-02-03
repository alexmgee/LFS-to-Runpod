/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "core/animatable_property.hpp"

namespace lfs::core::prop {

    namespace {
        UndoCallback g_undo_callback;
    }

    void set_undo_callback(UndoCallback callback) { g_undo_callback = std::move(callback); }

    UndoCallback get_undo_callback() { return g_undo_callback; }

} // namespace lfs::core::prop
