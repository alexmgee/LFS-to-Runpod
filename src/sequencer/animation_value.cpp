/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "animation_value.hpp"

#include <cassert>

namespace lfs::sequencer {

    AnimationValue interpolateValue(const AnimationValue& a, const AnimationValue& b, float t) {
        assert(a.index() == b.index() && "Cannot interpolate different value types");

        return std::visit(
            [&b, t](auto&& val_a) -> AnimationValue {
                using T = std::decay_t<decltype(val_a)>;
                const auto& val_b = std::get<T>(b);

                if constexpr (std::is_same_v<T, bool>) {
                    return t >= 0.5f ? val_b : val_a;
                } else if constexpr (std::is_same_v<T, int>) {
                    return t >= 0.5f ? val_b : val_a;
                } else if constexpr (std::is_same_v<T, float>) {
                    return val_a + (val_b - val_a) * t;
                } else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                                     std::is_same_v<T, glm::vec4>) {
                    return glm::mix(val_a, val_b, t);
                } else if constexpr (std::is_same_v<T, glm::quat>) {
                    return glm::slerp(val_a, val_b, t);
                } else if constexpr (std::is_same_v<T, glm::mat4>) {
                    glm::mat4 result;
                    for (int i = 0; i < 4; ++i) {
                        result[i] = glm::mix(val_a[i], val_b[i], t);
                    }
                    return result;
                } else {
                    return val_a;
                }
            },
            a);
    }

} // namespace lfs::sequencer
