/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "mcp_server.hpp"
#include "mcp_training_context.hpp"

#include "core/event_bridge/command_center_bridge.hpp"
#include "core/logger.hpp"

#include <cassert>
#include <iostream>

namespace lfs::mcp {

    McpServer::McpServer() {
        capabilities_.tools = true;
        capabilities_.resources = true;
        capabilities_.prompts = false;
        capabilities_.logging = true;
    }

    McpServer::~McpServer() {
        stop();
    }

    void McpServer::stop() {
        running_.store(false);
    }

    std::string McpServer::read_line() {
        std::string line;
        std::getline(std::cin, line);
        return line;
    }

    void McpServer::write_response(const std::string& response) {
        std::lock_guard lock(io_mutex_);
        std::cout << response << std::endl;
        std::cout.flush();
    }

    void McpServer::run_stdio() {
        running_.store(true);

        register_builtin_tools();

        LOG_INFO("MCP server started (stdio mode)");

        while (running_.load() && !std::cin.eof()) {
            std::string line = read_line();

            if (line.empty()) {
                continue;
            }

            try {
                JsonRpcRequest req = parse_request(line);
                JsonRpcResponse resp = handle_request(req);
                std::string output = serialize_response(resp);
                write_response(output);
            } catch (const json::parse_error& e) {
                auto resp = make_error_response(
                    int64_t(0),
                    JsonRpcError::PARSE_ERROR,
                    std::string("Parse error: ") + e.what());
                write_response(serialize_response(resp));
            } catch (const std::exception& e) {
                auto resp = make_error_response(
                    int64_t(0),
                    JsonRpcError::INTERNAL_ERROR,
                    std::string("Internal error: ") + e.what());
                write_response(serialize_response(resp));
            }
        }

        LOG_INFO("MCP server stopped");
    }

    JsonRpcResponse McpServer::handle_request(const JsonRpcRequest& req) {
        if (req.method == "initialize") {
            return handle_initialize(req);
        }
        if (req.method == "notifications/initialized" || req.method == "initialized") {
            return handle_initialized(req);
        }
        if (req.method == "ping") {
            return handle_ping(req);
        }

        if (!initialized_ && req.method != "initialize") {
            return make_error_response(
                req.id,
                JsonRpcError::INVALID_REQUEST,
                "Server not initialized. Call 'initialize' first.");
        }

        if (req.method == "tools/list") {
            return handle_tools_list(req);
        }
        if (req.method == "tools/call") {
            return handle_tools_call(req);
        }
        if (req.method == "resources/list") {
            return handle_resources_list(req);
        }
        if (req.method == "resources/read") {
            return handle_resources_read(req);
        }

        return make_error_response(
            req.id,
            JsonRpcError::METHOD_NOT_FOUND,
            "Method not found: " + req.method);
    }

    JsonRpcResponse McpServer::handle_initialize(const JsonRpcRequest& req) {
        McpInitializeResult result;
        result.capabilities = capabilities_;
        result.server_info.name = "lichtfeld-mcp";
        result.server_info.version = "1.0.0";

        initialized_ = true;

        return make_success_response(req.id, initialize_result_to_json(result));
    }

    JsonRpcResponse McpServer::handle_initialized(const JsonRpcRequest& req) {
        return make_success_response(req.id, json::object());
    }

    JsonRpcResponse McpServer::handle_ping(const JsonRpcRequest& req) {
        return make_success_response(req.id, json::object());
    }

    JsonRpcResponse McpServer::handle_tools_list(const JsonRpcRequest& req) {
        auto tools = ToolRegistry::instance().list_tools();

        json tools_array = json::array();
        for (const auto& tool : tools) {
            tools_array.push_back(tool_to_json(tool));
        }

        return make_success_response(req.id, json{{"tools", tools_array}});
    }

    JsonRpcResponse McpServer::handle_tools_call(const JsonRpcRequest& req) {
        if (!req.params) {
            return make_error_response(
                req.id,
                JsonRpcError::INVALID_PARAMS,
                "Missing params for tools/call");
        }

        const auto& params = *req.params;

        if (!params.contains("name")) {
            return make_error_response(
                req.id,
                JsonRpcError::INVALID_PARAMS,
                "Missing 'name' parameter");
        }

        std::string tool_name = params["name"].get<std::string>();
        json arguments = params.value("arguments", json::object());

        json result = ToolRegistry::instance().call_tool(tool_name, arguments);

        json content = json::array();
        content.push_back(json{
            {"type", "text"},
            {"text", result.dump(2)}});

        return make_success_response(req.id, json{{"content", content}});
    }

    JsonRpcResponse McpServer::handle_resources_list(const JsonRpcRequest& req) {
        json resources = json::array();

        resources.push_back(resource_to_json(McpResource{
            .uri = "lichtfeld://scene/state",
            .name = "Training State",
            .description = "Current training state snapshot (iteration, loss, gaussians)",
            .mime_type = "application/json"}));

        resources.push_back(resource_to_json(McpResource{
            .uri = "lichtfeld://render/current",
            .name = "Current Render",
            .description = "Base64-encoded PNG render from camera 0",
            .mime_type = "image/png"}));

        resources.push_back(resource_to_json(McpResource{
            .uri = "lichtfeld://training/loss_curve",
            .name = "Loss Curve",
            .description = "Training loss history",
            .mime_type = "application/json"}));

        resources.push_back(resource_to_json(McpResource{
            .uri = "lichtfeld://gaussians/stats",
            .name = "Gaussian Statistics",
            .description = "Statistics about the Gaussian model",
            .mime_type = "application/json"}));

        return make_success_response(req.id, json{{"resources", resources}});
    }

    JsonRpcResponse McpServer::handle_resources_read(const JsonRpcRequest& req) {
        if (!req.params || !req.params->contains("uri")) {
            return make_error_response(
                req.id,
                JsonRpcError::INVALID_PARAMS,
                "Missing 'uri' parameter");
        }

        std::string uri = (*req.params)["uri"].get<std::string>();

        json content;

        if (uri == "lichtfeld://scene/state") {
            auto* cc = event::command_center();
            if (cc) {
                auto snapshot = cc->snapshot();
                content["iteration"] = snapshot.iteration;
                content["max_iterations"] = snapshot.max_iterations;
                content["num_gaussians"] = snapshot.num_gaussians;
                content["loss"] = snapshot.loss;
                content["is_running"] = snapshot.is_running;
                content["is_paused"] = snapshot.is_paused;
                content["is_refining"] = snapshot.is_refining;
            } else {
                content["error"] = "Training system not initialized";
            }
        } else if (uri == "lichtfeld://render/current" || uri.starts_with("lichtfeld://render/camera/")) {
            int camera_index = 0;
            if (uri.starts_with("lichtfeld://render/camera/")) {
                auto idx_str = uri.substr(strlen("lichtfeld://render/camera/"));
                try {
                    camera_index = std::stoi(idx_str);
                } catch (...) {
                    camera_index = 0;
                }
            }

            auto result = TrainingContext::instance().render_to_base64(camera_index);
            if (result) {
                json render_result;
                render_result["contents"] = json::array();
                render_result["contents"].push_back(json{
                    {"uri", uri},
                    {"mimeType", "image/png"},
                    {"blob", *result}});
                return make_success_response(req.id, render_result);
            } else {
                content["error"] = result.error();
            }
        } else if (uri == "lichtfeld://training/loss_curve") {
            auto* cc = event::command_center();
            if (cc) {
                auto history = cc->loss_history();
                json points = json::array();
                for (const auto& p : history) {
                    points.push_back(json{{"iteration", p.iteration}, {"loss", p.loss}});
                }
                content["points"] = points;
                content["count"] = history.size();
            } else {
                content["error"] = "Training system not initialized";
            }
        } else if (uri == "lichtfeld://gaussians/stats") {
            auto* cc = event::command_center();
            if (cc) {
                auto snapshot = cc->snapshot();
                content["count"] = snapshot.num_gaussians;
                content["is_refining"] = snapshot.is_refining;
            } else {
                content["error"] = "Training system not initialized";
            }
        } else {
            return make_error_response(
                req.id,
                JsonRpcError::INVALID_PARAMS,
                "Unknown resource URI: " + uri);
        }

        json result;
        result["contents"] = json::array();
        result["contents"].push_back(json{
            {"uri", uri},
            {"mimeType", "application/json"},
            {"text", content.dump(2)}});

        return make_success_response(req.id, result);
    }

    int run_mcp_server_main(int argc, char* argv[]) {
        (void)argc;
        (void)argv;

        McpServer server;
        server.run_stdio();

        return 0;
    }

} // namespace lfs::mcp
