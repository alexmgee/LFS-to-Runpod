/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/export.hpp"
#include "core/modal_event.hpp"
#include <functional>

namespace lfs::core {

    // Callback types for Python operator system
    // Defined here to avoid CUDA dependency issues in python_runtime
    using CancelOperatorCallback = std::function<void()>;
    using InvokeOperatorCallback = std::function<bool(const char*)>;
    using ModalEventCallback = std::function<bool(const ModalEvent&)>;

    // Interface for callback storage (implemented by EditorContext)
    class LFS_CORE_API IOperatorCallbacks {
    public:
        virtual ~IOperatorCallbacks();

        virtual void setCancelOperatorCallback(CancelOperatorCallback cb) = 0;
        virtual void setInvokeOperatorCallback(InvokeOperatorCallback cb) = 0;
        virtual void setModalEventCallback(ModalEventCallback cb) = 0;

        [[nodiscard]] virtual bool hasCancelOperatorCallback() const = 0;
        [[nodiscard]] virtual bool hasInvokeOperatorCallback() const = 0;
        [[nodiscard]] virtual bool hasModalEventCallback() const = 0;

        virtual void cancelOperator() const = 0;
        virtual bool invokeOperator(const char* id) const = 0;
        virtual bool dispatchModalEvent(const ModalEvent& evt) const = 0;
    };

} // namespace lfs::core
