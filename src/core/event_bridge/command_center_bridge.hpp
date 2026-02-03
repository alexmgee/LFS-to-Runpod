/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "event_bridge.hpp"

#include <atomic>

namespace lfs::training {
    class CommandCenter;
}

namespace lfs::event {

    class LFS_BRIDGE_API CommandCenterBridge {
    public:
        static CommandCenterBridge& instance();

        void set(lfs::training::CommandCenter* ptr);
        lfs::training::CommandCenter* get() const;

    private:
        CommandCenterBridge() = default;
        std::atomic<lfs::training::CommandCenter*> ptr_{nullptr};
    };

    inline lfs::training::CommandCenter* command_center() {
        return CommandCenterBridge::instance().get();
    }

} // namespace lfs::event
