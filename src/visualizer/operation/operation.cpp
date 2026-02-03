/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "operation.hpp"
#include "core/logger.hpp"

namespace lfs::vis::op {

    OperationRegistry& OperationRegistry::instance() {
        static OperationRegistry instance;
        return instance;
    }

    void OperationRegistry::registerOperation(std::string id, OperationFactory factory) {
        factories_[std::move(id)] = std::move(factory);
    }

    void OperationRegistry::unregisterOperation(const std::string& id) {
        factories_.erase(id);
    }

    OperationPtr OperationRegistry::create(const std::string& id) const {
        auto it = factories_.find(id);
        if (it == factories_.end()) {
            LOG_ERROR("Operation not found: {}", id);
            return nullptr;
        }
        return it->second();
    }

    bool OperationRegistry::hasOperation(const std::string& id) const {
        return factories_.contains(id);
    }

    std::vector<std::string> OperationRegistry::getAllIds() const {
        std::vector<std::string> ids;
        ids.reserve(factories_.size());
        for (const auto& [id, _] : factories_) {
            ids.push_back(id);
        }
        return ids;
    }

} // namespace lfs::vis::op
