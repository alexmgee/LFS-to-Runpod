/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <type_traits>

namespace lfs::vis::op {

    enum class OperatorFlags : uint32_t {
        NONE = 0,
        REGISTER = 1 << 0,
        UNDO = 1 << 1,
        UNDO_GROUPED = 1 << 2,
        INTERNAL = 1 << 3,
        MODAL = 1 << 4,
        BLOCKING = 1 << 5,
    };

    constexpr OperatorFlags operator|(OperatorFlags a, OperatorFlags b) {
        return static_cast<OperatorFlags>(static_cast<std::underlying_type_t<OperatorFlags>>(a) |
                                          static_cast<std::underlying_type_t<OperatorFlags>>(b));
    }

    constexpr OperatorFlags operator&(OperatorFlags a, OperatorFlags b) {
        return static_cast<OperatorFlags>(static_cast<std::underlying_type_t<OperatorFlags>>(a) &
                                          static_cast<std::underlying_type_t<OperatorFlags>>(b));
    }

    constexpr bool hasFlag(const OperatorFlags flags, const OperatorFlags flag) {
        return (flags & flag) == flag;
    }

} // namespace lfs::vis::op
