/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace lfs::mcp {

    struct CapabilityCallResult {
        bool success = false;
        std::string result_json;
        std::string error;
    };

    class SelectionClient {
    public:
        static constexpr const char* SOCKET_PATH = "/tmp/lichtfeld-selection.sock";

        explicit SelectionClient(const std::string& socket_path = SOCKET_PATH);

        [[nodiscard]] std::expected<void, std::string> select_rect(float x0, float y0, float x1, float y1,
                                                                   const std::string& mode = "replace",
                                                                   int camera_index = 0);

        [[nodiscard]] std::expected<void, std::string> apply_mask(const std::vector<uint8_t>& mask);

        [[nodiscard]] std::expected<void, std::string> deselect_all();

        [[nodiscard]] std::expected<CapabilityCallResult, std::string> invoke_capability(const std::string& name,
                                                                                         const std::string& args_json);

        [[nodiscard]] bool is_gui_running() const;

    private:
        static constexpr size_t RECV_BUFFER_SIZE = 65536;

        [[nodiscard]] std::expected<std::string, std::string> send_command(const std::string& json_command);

        std::string socket_path_;
    };

} // namespace lfs::mcp
