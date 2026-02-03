/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "selection_server.hpp"
#include "core/events.hpp"
#include "core/logger.hpp"

#include <cstring>
#include <nlohmann/json.hpp>

#ifdef _WIN32
// Windows headers already included via selection_server.hpp
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace lfs::vis {

    using json = nlohmann::json;
    using namespace lfs::core::events;

#ifdef _WIN32
    namespace {
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
    } // namespace
#endif

    SelectionServer::SelectionServer() = default;

    SelectionServer::~SelectionServer() {
        stop();
    }

#ifdef _WIN32

    bool SelectionServer::start(const std::string& socket_path) {
        if (running_)
            return true;

        socket_path_ = socket_path;
        const std::string pipe_name = socket_path_to_pipe_name(socket_path_);

        pipe_handle_ = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            static_cast<DWORD>(RECV_BUFFER_SIZE),
            static_cast<DWORD>(RECV_BUFFER_SIZE),
            0,
            nullptr);

        if (pipe_handle_ == INVALID_HANDLE_VALUE) {
            LOG_ERROR("SelectionServer: CreateNamedPipe() failed: {}", GetLastError());
            return false;
        }

        running_ = true;
        server_thread_ = std::thread(&SelectionServer::server_loop, this);
        LOG_INFO("SelectionServer started: {}", pipe_name);
        return true;
    }

    void SelectionServer::stop() {
        if (!running_)
            return;

        running_ = false;

        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            // Cancel any pending ConnectNamedPipe
            CancelIoEx(pipe_handle_, nullptr);
            CloseHandle(pipe_handle_);
            pipe_handle_ = INVALID_HANDLE_VALUE;
        }

        if (server_thread_.joinable())
            server_thread_.join();
    }

    void SelectionServer::server_loop() {
        const std::string pipe_name = socket_path_to_pipe_name(socket_path_);

        while (running_) {
            // Wait for a client to connect
            if (ConnectNamedPipe(pipe_handle_, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
                handle_client(pipe_handle_);
                DisconnectNamedPipe(pipe_handle_);
            } else {
                DWORD error = GetLastError();
                if (error != ERROR_OPERATION_ABORTED && running_) {
                    LOG_ERROR("SelectionServer: ConnectNamedPipe() failed: {}", error);
                }
            }
        }
    }

    void SelectionServer::send_response(HANDLE client_pipe, bool success, const char* error) {
        const json response = error ? json{{"success", success}, {"error", error}} : json{{"success", success}};
        const std::string str = response.dump();
        DWORD bytes_written = 0;
        WriteFile(client_pipe, str.c_str(), static_cast<DWORD>(str.size()), &bytes_written, nullptr);
    }

    void SelectionServer::handle_client(HANDLE client_pipe) {
        char buffer[RECV_BUFFER_SIZE];
        DWORD bytes_read = 0;

        if (!ReadFile(client_pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) || bytes_read == 0)
            return;

        buffer[bytes_read] = '\0';

        try {
            const auto request = json::parse(buffer);
            const auto command = request.value("command", "");

            if (command == "select_rect") {
                queue_command(SelectRectCmd{.x0 = request.value("x0", 0.0f),
                                            .y0 = request.value("y0", 0.0f),
                                            .x1 = request.value("x1", 0.0f),
                                            .y1 = request.value("y1", 0.0f),
                                            .camera_index = request.value("camera_index", 0),
                                            .mode = request.value("mode", "replace")});
                send_response(client_pipe, true);

            } else if (command == "apply_mask") {
                std::vector<uint8_t> mask;
                if (request.contains("mask")) {
                    mask = request["mask"].get<std::vector<uint8_t>>();
                }
                queue_command(ApplyMaskCmd{.mask = std::move(mask)});
                send_response(client_pipe, true);

            } else if (command == "deselect_all") {
                queue_command(DeselectAllCmd{});
                send_response(client_pipe, true);

            } else if (command == "invoke_capability") {
                const auto capability = request.value("capability", "");
                const auto args = request.value("args", "{}");
                if (capability.empty()) {
                    send_response(client_pipe, false, "Missing capability name");
                } else if (!invoke_capability_callback_) {
                    LOG_ERROR("SelectionServer: capability callback not set");
                    send_response(client_pipe, false, "Capability invocation not available");
                } else {
                    const auto result = invoke_capability_callback_(capability, args);
                    json response;
                    response["success"] = result.success;
                    if (!result.result_json.empty()) {
                        try {
                            response["result"] = json::parse(result.result_json);
                        } catch (...) {
                            response["result"] = result.result_json;
                        }
                    }
                    if (!result.error.empty())
                        response["error"] = result.error;
                    const std::string str = response.dump();
                    DWORD bytes_written = 0;
                    WriteFile(client_pipe, str.c_str(), static_cast<DWORD>(str.size()), &bytes_written, nullptr);
                }

            } else {
                send_response(client_pipe, false, "Unknown command");
            }

        } catch (const std::exception& e) {
            LOG_ERROR("SelectionServer: {}", e.what());
            send_response(client_pipe, false, e.what());
        }
    }

#else // Unix implementation

    bool SelectionServer::start(const std::string& socket_path) {
        if (running_)
            return true;

        socket_path_ = socket_path;
        unlink(socket_path_.c_str());

        server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            LOG_ERROR("SelectionServer: socket() failed: {}", strerror(errno));
            return false;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            LOG_ERROR("SelectionServer: bind() failed: {}", strerror(errno));
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        if (listen(server_fd_, LISTEN_BACKLOG) < 0) {
            LOG_ERROR("SelectionServer: listen() failed: {}", strerror(errno));
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        running_ = true;
        server_thread_ = std::thread(&SelectionServer::server_loop, this);
        LOG_INFO("SelectionServer started: {}", socket_path_);
        return true;
    }

    void SelectionServer::stop() {
        if (!running_)
            return;

        running_ = false;

        if (server_fd_ >= 0) {
            shutdown(server_fd_, SHUT_RDWR);
            close(server_fd_);
            server_fd_ = -1;
        }

        if (server_thread_.joinable())
            server_thread_.join();

        unlink(socket_path_.c_str());
    }

    void SelectionServer::server_loop() {
        while (running_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_fd_, &read_fds);

            timeval timeout{.tv_sec = 0, .tv_usec = SELECT_TIMEOUT_US};
            const int result = select(server_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);

            if (result < 0) {
                if (running_)
                    LOG_ERROR("SelectionServer: select() failed");
                break;
            }

            if (result == 0)
                continue;

            const int client_fd = accept(server_fd_, nullptr, nullptr);
            if (client_fd < 0)
                continue;

            handle_client(client_fd);
            close(client_fd);
        }
    }

    void SelectionServer::send_response(int client_fd, bool success, const char* error) {
        const json response = error ? json{{"success", success}, {"error", error}} : json{{"success", success}};
        const std::string str = response.dump();
        write(client_fd, str.c_str(), str.size());
    }

    void SelectionServer::handle_client(const int client_fd) {
        char buffer[RECV_BUFFER_SIZE];
        const ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0)
            return;

        buffer[bytes_read] = '\0';

        try {
            const auto request = json::parse(buffer);
            const auto command = request.value("command", "");

            if (command == "select_rect") {
                queue_command(SelectRectCmd{.x0 = request.value("x0", 0.0f),
                                            .y0 = request.value("y0", 0.0f),
                                            .x1 = request.value("x1", 0.0f),
                                            .y1 = request.value("y1", 0.0f),
                                            .camera_index = request.value("camera_index", 0),
                                            .mode = request.value("mode", "replace")});
                send_response(client_fd, true);

            } else if (command == "apply_mask") {
                std::vector<uint8_t> mask;
                if (request.contains("mask")) {
                    mask = request["mask"].get<std::vector<uint8_t>>();
                }
                queue_command(ApplyMaskCmd{.mask = std::move(mask)});
                send_response(client_fd, true);

            } else if (command == "deselect_all") {
                queue_command(DeselectAllCmd{});
                send_response(client_fd, true);

            } else if (command == "invoke_capability") {
                const auto capability = request.value("capability", "");
                const auto args = request.value("args", "{}");
                if (capability.empty()) {
                    send_response(client_fd, false, "Missing capability name");
                } else if (!invoke_capability_callback_) {
                    LOG_ERROR("SelectionServer: capability callback not set");
                    send_response(client_fd, false, "Capability invocation not available");
                } else {
                    const auto result = invoke_capability_callback_(capability, args);
                    json response;
                    response["success"] = result.success;
                    if (!result.result_json.empty()) {
                        try {
                            response["result"] = json::parse(result.result_json);
                        } catch (...) {
                            response["result"] = result.result_json;
                        }
                    }
                    if (!result.error.empty())
                        response["error"] = result.error;
                    const std::string str = response.dump();
                    write(client_fd, str.c_str(), str.size());
                }

            } else {
                send_response(client_fd, false, "Unknown command");
            }

        } catch (const std::exception& e) {
            LOG_ERROR("SelectionServer: {}", e.what());
            send_response(client_fd, false, e.what());
        }
    }

#endif // _WIN32

    // Shared implementations (platform-independent)

    void SelectionServer::queue_command(SelectionCommand cmd) {
        std::lock_guard lock(command_queue_mutex_);
        command_queue_.push(std::move(cmd));
    }

    void SelectionServer::process_pending_commands() {
        std::queue<SelectionCommand> commands;
        {
            std::lock_guard lock(command_queue_mutex_);
            std::swap(commands, command_queue_);
        }

        while (!commands.empty()) {
            auto& cmd = commands.front();

            std::visit(
                [](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, SelectRectCmd>) {
                        cmd::SelectRect{.x0 = arg.x0,
                                        .y0 = arg.y0,
                                        .x1 = arg.x1,
                                        .y1 = arg.y1,
                                        .camera_index = arg.camera_index,
                                        .mode = std::move(arg.mode)}
                            .emit();
                    } else if constexpr (std::is_same_v<T, ApplyMaskCmd>) {
                        cmd::ApplySelectionMask{.mask = std::move(arg.mask)}.emit();
                    } else if constexpr (std::is_same_v<T, DeselectAllCmd>) {
                        cmd::DeselectAll{}.emit();
                    }
                },
                cmd);

            commands.pop();
        }
    }

} // namespace lfs::vis
