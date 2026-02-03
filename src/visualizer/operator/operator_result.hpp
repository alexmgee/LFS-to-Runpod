/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace lfs::vis::op {

    enum class OperatorResult : uint8_t {
        FINISHED,      // Success - push undo if UNDO flag set
        CANCELLED,     // User cancelled - no undo push
        RUNNING_MODAL, // Continue modal operation
        PASS_THROUGH,  // Let event propagate to next handler
    };

    struct OperatorReturnValue {
        OperatorResult status = OperatorResult::CANCELLED;
        std::unordered_map<std::string, std::any> data;

        static OperatorReturnValue finished() { return {OperatorResult::FINISHED, {}}; }
        static OperatorReturnValue cancelled() { return {OperatorResult::CANCELLED, {}}; }
        static OperatorReturnValue running_modal() { return {OperatorResult::RUNNING_MODAL, {}}; }
        static OperatorReturnValue pass_through() { return {OperatorResult::PASS_THROUGH, {}}; }

        static OperatorReturnValue finished_with(std::unordered_map<std::string, std::any> d) {
            return {OperatorResult::FINISHED, std::move(d)};
        }

        [[nodiscard]] bool is_finished() const { return status == OperatorResult::FINISHED; }
        [[nodiscard]] bool is_cancelled() const { return status == OperatorResult::CANCELLED; }
        [[nodiscard]] bool is_running_modal() const { return status == OperatorResult::RUNNING_MODAL; }
        [[nodiscard]] bool is_pass_through() const { return status == OperatorResult::PASS_THROUGH; }
    };

} // namespace lfs::vis::op
