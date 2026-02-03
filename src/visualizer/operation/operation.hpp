/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "operator/operator_properties.hpp"
#include "undo_entry.hpp"
#include <any>
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace lfs::vis {
    class SceneManager;
}

namespace lfs::vis::op {

    struct OperationResult {
        enum class Status { SUCCESS,
                            FAILURE,
                            SKIPPED };

        Status status;
        std::string error;
        std::any data;

        static OperationResult success(std::any output = {}) {
            return {Status::SUCCESS, "", std::move(output)};
        }

        static OperationResult failure(std::string msg) {
            return {Status::FAILURE, std::move(msg), {}};
        }

        static OperationResult skipped(std::string reason = "") {
            return {Status::SKIPPED, std::move(reason), {}};
        }

        [[nodiscard]] bool ok() const { return status == Status::SUCCESS; }
    };

    class Operation {
    public:
        virtual ~Operation() = default;

        [[nodiscard]] virtual OperationResult execute(
            SceneManager& scene,
            const OperatorProperties& props,
            const std::any& input) = 0;

        [[nodiscard]] virtual bool poll(SceneManager& /*scene*/) const { return true; }
        [[nodiscard]] virtual std::string id() const = 0;
        [[nodiscard]] virtual std::string label() const = 0;
        [[nodiscard]] virtual ModifiesFlag modifies() const = 0;
    };

    using OperationPtr = std::unique_ptr<Operation>;
    using OperationFactory = std::function<OperationPtr()>;

    class OperationRegistry {
    public:
        static OperationRegistry& instance();

        void registerOperation(std::string id, OperationFactory factory);
        void unregisterOperation(const std::string& id);
        [[nodiscard]] OperationPtr create(const std::string& id) const;
        [[nodiscard]] bool hasOperation(const std::string& id) const;
        [[nodiscard]] std::vector<std::string> getAllIds() const;

    private:
        OperationRegistry() = default;
        std::unordered_map<std::string, OperationFactory> factories_;
    };

    inline OperationRegistry& operations() {
        return OperationRegistry::instance();
    }

    template <typename Op>
    struct OperationRegistrar {
        explicit OperationRegistrar(std::string id) {
            operations().registerOperation(std::move(id), [] { return std::make_unique<Op>(); });
        }
    };

#define REGISTER_OPERATION(OpClass) \
    static ::lfs::vis::op::OperationRegistrar<OpClass> _reg_##OpClass(OpClass{}.id())

} // namespace lfs::vis::op
