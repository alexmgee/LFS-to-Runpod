/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "app/application.hpp"
#include "core/argument_parser.hpp"
#include "app/converter.hpp"
#include "core/event_bridge/command_center_bridge.hpp"
#include "core/logger.hpp"
#include "git_version.h"
#include "mcp/mcp_server.hpp"
#include "mcp/mcp_training_context.hpp"
#include "python/plugin_runner.hpp"
#include "python/runner.hpp"
#include "training/control/command_api.hpp"

#include <print>

namespace {

    int runMcpServer(const lfs::core::args::McpMode& mode) {
        lfs::event::CommandCenterBridge::instance().set(&lfs::training::CommandCenter::instance());

        if (mode.scene_path) {
            LOG_INFO("MCP server starting with scene: {}", mode.scene_path->string());
        } else {
            LOG_INFO("MCP server starting (no scene loaded)");
        }

        lfs::mcp::McpServer server;
        server.run_stdio();

        lfs::mcp::TrainingContext::instance().shutdown();

        return 0;
    }

} // namespace

int main(int argc, char* argv[]) {
    auto result = lfs::core::args::parse_args(argc, argv);
    if (!result) {
        std::println(stderr, "Error: {}", result.error());
        return 1;
    }

    return std::visit([](auto&& mode) -> int {
        using T = std::decay_t<decltype(mode)>;

        if constexpr (std::is_same_v<T, lfs::core::args::HelpMode>) {
            return 0;
        } else if constexpr (std::is_same_v<T, lfs::core::args::VersionMode>) {
            std::println("LichtFeld Studio {} ({})", GIT_TAGGED_VERSION, GIT_COMMIT_HASH_SHORT);
            return 0;
        } else if constexpr (std::is_same_v<T, lfs::core::args::WarmupMode>) {
            return 0;
        } else if constexpr (std::is_same_v<T, lfs::core::args::ConvertMode>) {
            return lfs::app::run_converter(mode.params);
        } else if constexpr (std::is_same_v<T, lfs::core::args::McpMode>) {
            return runMcpServer(mode);
        } else if constexpr (std::is_same_v<T, lfs::core::args::PluginMode>) {
            return lfs::python::run_plugin_command(mode);
        } else if constexpr (std::is_same_v<T, lfs::core::args::TrainingMode>) {
            LOG_INFO("LichtFeld Studio");
            LOG_INFO("version {} | tag {}", GIT_TAGGED_VERSION, GIT_COMMIT_HASH_SHORT);

            if (mode.params->optimization.debug_python) {
                lfs::python::start_debugpy(mode.params->optimization.debug_python_port);
            }

            lfs::app::Application app;
            return app.run(std::move(mode.params));
        }
    },
                      std::move(*result));
}
