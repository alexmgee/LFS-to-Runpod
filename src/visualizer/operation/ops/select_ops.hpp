/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "operation/operation.hpp"

namespace lfs::vis::op {

    class SelectAll : public Operation {
    public:
        OperationResult execute(SceneManager& scene,
                                const OperatorProperties& props,
                                const std::any& input) override;

        [[nodiscard]] bool poll(SceneManager& scene) const override;
        [[nodiscard]] std::string id() const override { return "select.all"; }
        [[nodiscard]] std::string label() const override { return "Select All"; }
        [[nodiscard]] ModifiesFlag modifies() const override { return ModifiesFlag::SELECTION; }
    };

    class SelectNone : public Operation {
    public:
        OperationResult execute(SceneManager& scene,
                                const OperatorProperties& props,
                                const std::any& input) override;

        [[nodiscard]] std::string id() const override { return "select.none"; }
        [[nodiscard]] std::string label() const override { return "Select None"; }
        [[nodiscard]] ModifiesFlag modifies() const override { return ModifiesFlag::SELECTION; }
    };

    class SelectInvert : public Operation {
    public:
        OperationResult execute(SceneManager& scene,
                                const OperatorProperties& props,
                                const std::any& input) override;

        [[nodiscard]] bool poll(SceneManager& scene) const override;
        [[nodiscard]] std::string id() const override { return "select.invert"; }
        [[nodiscard]] std::string label() const override { return "Invert Selection"; }
        [[nodiscard]] ModifiesFlag modifies() const override { return ModifiesFlag::SELECTION; }
    };

    class SelectGrow : public Operation {
    public:
        OperationResult execute(SceneManager& scene,
                                const OperatorProperties& props,
                                const std::any& input) override;

        [[nodiscard]] bool poll(SceneManager& scene) const override;
        [[nodiscard]] std::string id() const override { return "select.grow"; }
        [[nodiscard]] std::string label() const override { return "Grow Selection"; }
        [[nodiscard]] ModifiesFlag modifies() const override { return ModifiesFlag::SELECTION; }
    };

    class SelectShrink : public Operation {
    public:
        OperationResult execute(SceneManager& scene,
                                const OperatorProperties& props,
                                const std::any& input) override;

        [[nodiscard]] bool poll(SceneManager& scene) const override;
        [[nodiscard]] std::string id() const override { return "select.shrink"; }
        [[nodiscard]] std::string label() const override { return "Shrink Selection"; }
        [[nodiscard]] ModifiesFlag modifies() const override { return ModifiesFlag::SELECTION; }
    };

} // namespace lfs::vis::op
