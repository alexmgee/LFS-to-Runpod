/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "operator_context.hpp"
#include "operator_flags.hpp"
#include "operator_id.hpp"
#include "operator_properties.hpp"
#include "operator_result.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lfs::vis::op {

    enum class OperatorSource : uint8_t { CPP,
                                          PYTHON };

    enum class PollDependency : uint8_t {
        NONE = 0,
        SELECTION = 1 << 0,
        TRAINING = 1 << 1,
        SCENE = 1 << 2,
        ALL = SELECTION | TRAINING | SCENE
    };

    inline PollDependency operator|(PollDependency a, PollDependency b) {
        return static_cast<PollDependency>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline PollDependency operator&(PollDependency a, PollDependency b) {
        return static_cast<PollDependency>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    struct OperatorDescriptor {
        std::optional<BuiltinOp> builtin_id;
        std::string python_class_id;
        std::string label;
        std::string description;
        std::string icon;
        std::string shortcut;
        OperatorFlags flags = OperatorFlags::NONE;
        OperatorSource source = OperatorSource::CPP;
        PollDependency poll_deps = PollDependency::ALL;

        [[nodiscard]] std::string id() const {
            if (builtin_id.has_value()) {
                return to_string(*builtin_id);
            }
            return python_class_id;
        }
    };

    class Operator {
    public:
        virtual ~Operator() = default;

        [[nodiscard]] virtual const OperatorDescriptor& descriptor() const = 0;
        [[nodiscard]] virtual bool poll(const OperatorContext& /*ctx*/) const { return true; }
        virtual OperatorResult invoke(OperatorContext& ctx, OperatorProperties& props) = 0;
        virtual OperatorResult modal(OperatorContext& /*ctx*/, OperatorProperties& /*props*/) {
            return OperatorResult::FINISHED;
        }
        virtual void cancel(OperatorContext& /*ctx*/) {}
    };

    using OperatorPtr = std::unique_ptr<Operator>;
    using OperatorFactory = std::function<OperatorPtr()>;

} // namespace lfs::vis::op
