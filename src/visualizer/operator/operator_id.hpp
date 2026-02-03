/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace lfs::vis::op {

    enum class BuiltinOp : uint16_t {
        SelectionStroke,
        BrushStroke,

        TransformSet,
        TransformTranslate,
        TransformRotate,
        TransformScale,
        TransformApplyBatch,

        AlignPickPoint,

        Undo,
        Redo,
        Delete,

        _Count
    };

    enum class BuiltinTool : uint16_t {
        Select,
        Translate,
        Rotate,
        Scale,
        Mirror,
        Brush,
        Align,

        _Count
    };

    const char* to_string(BuiltinOp op);
    std::optional<BuiltinOp> builtin_op_from_string(std::string_view s);
    const char* builtin_op_label(BuiltinOp op);

    const char* to_string(BuiltinTool tool);
    std::optional<BuiltinTool> builtin_tool_from_string(std::string_view s);
    const char* builtin_tool_label(BuiltinTool tool);

} // namespace lfs::vis::op
