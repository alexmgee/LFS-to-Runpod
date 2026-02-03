/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

namespace nanobind {
    class module_;
}

namespace lfs::python {

    void register_signals(nanobind::module_& m);
    void shutdown_signal_bridge();

} // namespace lfs::python
