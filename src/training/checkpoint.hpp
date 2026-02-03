/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

/**
 * @file checkpoint.hpp
 * @brief Training checkpoint save/load (.resume files)
 *
 * Format types and read-only functions live in core/checkpoint_format.hpp.
 * This header provides save/load that depend on training types (IStrategy, BilateralGrid, PPISP).
 */

#include "core/checkpoint_format.hpp"
#include "core/parameters.hpp"
#include <expected>
#include <filesystem>
#include <string>

namespace lfs::training {

    class IStrategy;
    class BilateralGrid;
    class PPISP;
    class PPISPControllerPool;

    /// Save complete training checkpoint
    std::expected<void, std::string> save_checkpoint(
        const std::filesystem::path& path,
        int iteration,
        const IStrategy& strategy,
        const lfs::core::param::TrainingParameters& params,
        const BilateralGrid* bilateral_grid = nullptr,
        const PPISP* ppisp = nullptr,
        const PPISPControllerPool* ppisp_controller_pool = nullptr);

    /// Load complete training checkpoint (strategy + optional appearance components)
    std::expected<int, std::string> load_checkpoint(
        const std::filesystem::path& path,
        IStrategy& strategy,
        lfs::core::param::TrainingParameters& params,
        BilateralGrid* bilateral_grid = nullptr,
        PPISP* ppisp = nullptr,
        PPISPControllerPool* ppisp_controller_pool = nullptr);

} // namespace lfs::training
