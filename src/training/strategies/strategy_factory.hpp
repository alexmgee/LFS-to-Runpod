/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/splat_data.hpp"
#include "istrategy.hpp"
#include <expected>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lfs::training {

    class StrategyFactory {
    public:
        using Creator = std::function<
            std::expected<std::unique_ptr<IStrategy>, std::string>(core::SplatData&)>;

        static StrategyFactory& instance();

        bool register_creator(const std::string& name, Creator creator);
        bool unregister(const std::string& name);

        [[nodiscard]] std::expected<std::unique_ptr<IStrategy>, std::string>
        create(const std::string& name, core::SplatData& model) const;

        [[nodiscard]] bool has(const std::string& name) const;
        [[nodiscard]] std::vector<std::string> list() const;

    private:
        StrategyFactory();
        void register_builtins();

        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, Creator> registry_;
    };

} // namespace lfs::training
