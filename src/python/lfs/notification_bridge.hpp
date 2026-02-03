/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

namespace lfs::python {

    // Set up C++ event handlers that forward to PyModalRegistry
    void setup_notification_handlers();

} // namespace lfs::python
