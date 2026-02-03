/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lfs::vis {

    struct SubModeDescriptor {
        std::string id;
        std::string label;
        std::string icon;
    };

    enum class ToolSource : uint8_t { CPP,
                                      PYTHON };

    struct ToolDescriptor {
        std::string id;
        std::string label;
        std::string icon;
        std::string shortcut;
        std::string group;
        int order = 0;
        ToolSource source = ToolSource::CPP;

        std::vector<SubModeDescriptor> submodes;

        std::function<bool()> poll_fn;
        std::function<void()> invoke_fn;
    };

} // namespace lfs::vis
