/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "property_system.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace lfs::core::prop {

    template <typename T>
    struct PropertyTraits {
        static constexpr PropType type = PropType::Float;
        static T interpolate(const T& a, const T& b, float t) { return a; }
    };

    template <>
    struct PropertyTraits<bool> {
        static constexpr PropType type = PropType::Bool;
        static bool interpolate(bool a, bool b, float t) { return t >= 0.5f ? b : a; }
    };

    template <>
    struct PropertyTraits<int> {
        static constexpr PropType type = PropType::Int;
        static int interpolate(int a, int b, float t) { return t >= 0.5f ? b : a; }
    };

    template <>
    struct PropertyTraits<float> {
        static constexpr PropType type = PropType::Float;
        static float interpolate(float a, float b, float t) { return a + (b - a) * t; }
    };

    template <>
    struct PropertyTraits<double> {
        static constexpr PropType type = PropType::Float;
        static double interpolate(double a, double b, float t) {
            return a + (b - a) * static_cast<double>(t);
        }
    };

    template <>
    struct PropertyTraits<size_t> {
        static constexpr PropType type = PropType::SizeT;
        static size_t interpolate(size_t a, size_t b, float t) { return t >= 0.5f ? b : a; }
    };

    template <>
    struct PropertyTraits<glm::vec2> {
        static constexpr PropType type = PropType::Vec2;
        static glm::vec2 interpolate(const glm::vec2& a, const glm::vec2& b, float t) {
            return glm::mix(a, b, t);
        }
    };

    template <>
    struct PropertyTraits<glm::vec3> {
        static constexpr PropType type = PropType::Vec3;
        static glm::vec3 interpolate(const glm::vec3& a, const glm::vec3& b, float t) {
            return glm::mix(a, b, t);
        }
    };

    template <>
    struct PropertyTraits<glm::vec4> {
        static constexpr PropType type = PropType::Vec4;
        static glm::vec4 interpolate(const glm::vec4& a, const glm::vec4& b, float t) {
            return glm::mix(a, b, t);
        }
    };

    template <>
    struct PropertyTraits<glm::quat> {
        static constexpr PropType type = PropType::Quat;
        static glm::quat interpolate(const glm::quat& a, const glm::quat& b, float t) {
            return glm::slerp(a, b, t);
        }
    };

    template <>
    struct PropertyTraits<glm::mat4> {
        static constexpr PropType type = PropType::Mat4;
        static glm::mat4 interpolate(const glm::mat4& a, const glm::mat4& b, float t) {
            // Simple component-wise interpolation
            // For proper transform interpolation, decompose into TRS and interpolate separately
            glm::mat4 result;
            for (int i = 0; i < 4; ++i) {
                result[i] = glm::mix(a[i], b[i], t);
            }
            return result;
        }
    };

} // namespace lfs::core::prop
