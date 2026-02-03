/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "selection_client.hpp"

#include "core/logger.hpp"

#include <cstring>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {
    constexpr int SOCKET_TIMEOUT_SEC = 5;
}

namespace lfs::mcp {

    using json = nlohmann::json;

    namespace {

        std::expected<void, std::string> parse_response(const std::string& response) {
            try {
                const auto j = json::parse(response);
                if (!j.value("success", false)) {
                    return std::unexpected(j.value("error", "Unknown error"));
                }
                return {};
            } catch (const std::exception& e) {
                return std::unexpected(std::string("Invalid response: ") + e.what());
            }
        }

#ifdef _WIN32
        std::string socket_path_to_pipe_name(const std::string& socket_path) {
            // Convert Unix socket path to Windows named pipe
            // /tmp/lichtfeld-selection.sock -> \\.\pipe\lichtfeld-selection
            std::string name = socket_path;
            if (name.find("/tmp/") == 0) {
                name = name.substr(5);
            }
            auto dot_pos = name.rfind('.');
            if (dot_pos != std::string::npos) {
                name = name.substr(0, dot_pos);
            }
            return R"(\\.\pipe\)" + name;
        }
#endif

    } // namespace

    SelectionClient::SelectionClient(const std::string& socket_path) : socket_path_(socket_path) {}

#ifdef _WIN32

    bool SelectionClient::is_gui_running() const {
        const std::string pipe_name = socket_path_to_pipe_name(socket_path_);
        HANDLE pipe = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return false;
        CloseHandle(pipe);
        return true;
    }

    std::expected<std::string, std::string> SelectionClient::send_command(const std::string& json_command) {
        const std::string pipe_name = socket_path_to_pipe_name(socket_path_);

        if (!WaitNamedPipeA(pipe_name.c_str(), SOCKET_TIMEOUT_SEC * 1000)) {
            return std::unexpected("GUI not running (timeout waiting for pipe)");
        }

        HANDLE pipe = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return std::unexpected("GUI not running");

        OVERLAPPED overlapped = {};
        overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) {
            CloseHandle(pipe);
            return std::unexpected("Failed to create event");
        }

        DWORD bytes_written = 0;
        if (!WriteFile(pipe, json_command.c_str(), static_cast<DWORD>(json_command.size()), &bytes_written, &overlapped)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(overlapped.hEvent, SOCKET_TIMEOUT_SEC * 1000) != WAIT_OBJECT_0) {
                    CancelIo(pipe);
                    CloseHandle(overlapped.hEvent);
                    CloseHandle(pipe);
                    return std::unexpected("Write timeout");
                }
                GetOverlappedResult(pipe, &overlapped, &bytes_written, FALSE);
            } else {
                CloseHandle(overlapped.hEvent);
                CloseHandle(pipe);
                return std::unexpected("Failed to send command");
            }
        }

        ResetEvent(overlapped.hEvent);

        char buffer[RECV_BUFFER_SIZE];
        DWORD bytes_read = 0;
        if (!ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, &overlapped)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(overlapped.hEvent, SOCKET_TIMEOUT_SEC * 1000) != WAIT_OBJECT_0) {
                    CancelIo(pipe);
                    CloseHandle(overlapped.hEvent);
                    CloseHandle(pipe);
                    return std::unexpected("Read timeout");
                }
                GetOverlappedResult(pipe, &overlapped, &bytes_read, FALSE);
            } else {
                CloseHandle(overlapped.hEvent);
                CloseHandle(pipe);
                return std::unexpected("No response from GUI");
            }
        }

        CloseHandle(overlapped.hEvent);
        CloseHandle(pipe);

        if (bytes_read == 0)
            return std::unexpected("No response from GUI");

        buffer[bytes_read] = '\0';
        return std::string(buffer);
    }

#else

    bool SelectionClient::is_gui_running() const {
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        const bool connected = (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        close(fd);
        return connected;
    }

    std::expected<std::string, std::string> SelectionClient::send_command(const std::string& json_command) {
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return std::unexpected("Failed to create socket");

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            return std::unexpected("GUI not running");
        }

        struct timeval tv;
        tv.tv_sec = SOCKET_TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (write(fd, json_command.c_str(), json_command.size()) < 0) {
            close(fd);
            return std::unexpected("Failed to send command");
        }

        char buffer[RECV_BUFFER_SIZE];
        const ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);

        if (bytes_read <= 0)
            return std::unexpected("No response from GUI (timeout or error)");

        buffer[bytes_read] = '\0';
        return std::string(buffer);
    }

#endif

    std::expected<void, std::string> SelectionClient::select_rect(float x0, float y0, float x1, float y1,
                                                                  const std::string& mode, int camera_index) {
        const json command = {{"command", "select_rect"}, {"x0", x0}, {"y0", y0}, {"x1", x1}, {"y1", y1}, {"mode", mode}, {"camera_index", camera_index}};

        const auto result = send_command(command.dump());
        if (!result)
            return std::unexpected(result.error());

        return parse_response(*result);
    }

    std::expected<void, std::string> SelectionClient::apply_mask(const std::vector<uint8_t>& mask) {
        const json command = {{"command", "apply_mask"}, {"mask", mask}};

        const auto result = send_command(command.dump());
        if (!result)
            return std::unexpected(result.error());

        return parse_response(*result);
    }

    std::expected<void, std::string> SelectionClient::deselect_all() {
        const json command = {{"command", "deselect_all"}};

        const auto result = send_command(command.dump());
        if (!result)
            return std::unexpected(result.error());

        return parse_response(*result);
    }

    std::expected<CapabilityCallResult, std::string> SelectionClient::invoke_capability(const std::string& name,
                                                                                        const std::string& args_json) {
        const json command = {{"command", "invoke_capability"}, {"capability", name}, {"args", args_json}};

        const auto result = send_command(command.dump());
        if (!result)
            return std::unexpected(result.error());

        try {
            const auto j = json::parse(*result);
            CapabilityCallResult cap_result;
            cap_result.success = j.value("success", false);
            cap_result.result_json = j.contains("result") ? j["result"].dump() : "{}";
            cap_result.error = j.value("error", "");
            return cap_result;
        } catch (const std::exception& e) {
            return std::unexpected(std::string("Invalid response: ") + e.what());
        }
    }

} // namespace lfs::mcp
