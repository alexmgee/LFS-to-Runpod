/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "control_boundary.hpp"
#include "event_bridge.hpp"

#include <functional>
#include <vector>

namespace lfs::event {

    class LFS_BRIDGE_API ScopedHandler {
    public:
        ScopedHandler() = default;
        ~ScopedHandler();

        template <typename E>
        void subscribe(std::function<void(const E&)> handler) {
            auto id = when<E>(std::move(handler));
            cleanup_.push_back([id]() { EventBridge::instance().unsubscribe(typeid(E), id); });
        }

        void subscribe_hook(lfs::training::ControlHook hook,
                            lfs::training::ControlBoundary::Callback cb);

        ScopedHandler(const ScopedHandler&) = delete;
        ScopedHandler& operator=(const ScopedHandler&) = delete;
        ScopedHandler(ScopedHandler&& other) noexcept;
        ScopedHandler& operator=(ScopedHandler&& other) noexcept;

    private:
        std::vector<std::function<void()>> cleanup_;
    };

} // namespace lfs::event
