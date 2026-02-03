/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "scoped_handler.hpp"

namespace lfs::event {

    ScopedHandler::~ScopedHandler() {
        for (const auto& fn : cleanup_) {
            fn();
        }
    }

    ScopedHandler::ScopedHandler(ScopedHandler&& other) noexcept : cleanup_(std::move(other.cleanup_)) {
        other.cleanup_.clear();
    }

    ScopedHandler& ScopedHandler::operator=(ScopedHandler&& other) noexcept {
        if (this != &other) {
            for (const auto& fn : cleanup_) {
                fn();
            }
            cleanup_ = std::move(other.cleanup_);
            other.cleanup_.clear();
        }
        return *this;
    }

    void ScopedHandler::subscribe_hook(lfs::training::ControlHook hook,
                                       lfs::training::ControlBoundary::Callback cb) {
        auto id = lfs::training::ControlBoundary::instance().register_callback(hook, std::move(cb));
        cleanup_.push_back(
            [hook, id]() { lfs::training::ControlBoundary::instance().unregister_callback(hook, id); });
    }

} // namespace lfs::event
