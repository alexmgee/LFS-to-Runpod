/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <nanobind/nanobind.h>

namespace nb = nanobind;

namespace lfs::python {

    void register_mcp(nb::module_& m);

} // namespace lfs::python
