/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/logger.hpp"

#include <any>
#include <optional>
#include <string>
#include <unordered_map>

namespace lfs::vis::op {

    class OperatorProperties {
    public:
        template <typename T>
        void set(const std::string& key, T value) {
            values_[key] = std::move(value);
        }

        template <typename T>
        [[nodiscard]] std::optional<T> get(const std::string& key) const {
            const auto it = values_.find(key);
            if (it == values_.end()) {
                return std::nullopt;
            }
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                LOG_ERROR("OperatorProperties: type mismatch for '{}' - expected {}", key, typeid(T).name());
                return std::nullopt;
            }
        }

        template <typename T>
        [[nodiscard]] T get_or(const std::string& key, const T& default_value) const {
            return get<T>(key).value_or(default_value);
        }

        [[nodiscard]] bool has(const std::string& key) const { return values_.contains(key); }
        [[nodiscard]] bool empty() const { return values_.empty(); }
        void clear() { values_.clear(); }

    private:
        std::unordered_map<std::string, std::any> values_;
    };

} // namespace lfs::vis::op
