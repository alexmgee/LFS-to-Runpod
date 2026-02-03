/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "command_center_bridge.hpp"

namespace lfs::event {

    CommandCenterBridge& CommandCenterBridge::instance() {
        static CommandCenterBridge bridge;
        return bridge;
    }

    void CommandCenterBridge::set(lfs::training::CommandCenter* ptr) {
        ptr_.store(ptr, std::memory_order_release);
    }

    lfs::training::CommandCenter* CommandCenterBridge::get() const {
        return ptr_.load(std::memory_order_acquire);
    }

} // namespace lfs::event
