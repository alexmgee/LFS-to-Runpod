/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "property_schema.hpp"

namespace lfs::vis::op {

    PropertySchemaRegistry& PropertySchemaRegistry::instance() {
        static PropertySchemaRegistry registry;
        return registry;
    }

    void PropertySchemaRegistry::registerSchema(const std::string& operator_id,
                                                std::vector<PropertySchema> properties) {
        std::lock_guard lock(mutex_);
        schemas_[operator_id] = std::move(properties);
    }

    void PropertySchemaRegistry::unregisterSchema(const std::string& operator_id) {
        std::lock_guard lock(mutex_);
        schemas_.erase(operator_id);
    }

    const std::vector<PropertySchema>* PropertySchemaRegistry::getSchema(const std::string& operator_id) const {
        std::lock_guard lock(mutex_);
        auto it = schemas_.find(operator_id);
        return it != schemas_.end() ? &it->second : nullptr;
    }

    const PropertySchema* PropertySchemaRegistry::getPropertySchema(const std::string& operator_id,
                                                                    const std::string& prop_name) const {
        std::lock_guard lock(mutex_);
        auto it = schemas_.find(operator_id);
        if (it == schemas_.end()) {
            return nullptr;
        }
        for (const auto& schema : it->second) {
            if (schema.name == prop_name) {
                return &schema;
            }
        }
        return nullptr;
    }

} // namespace lfs::vis::op
