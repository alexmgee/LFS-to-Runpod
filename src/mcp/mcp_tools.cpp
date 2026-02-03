/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "mcp_tools.hpp"
#include "mcp_training_context.hpp"

#include "core/event_bridge/command_center_bridge.hpp"
#include "core/logger.hpp"

#include <cassert>

namespace lfs::mcp {

    namespace {

        std::string target_to_string(training::CommandTarget target) {
            switch (target) {
            case training::CommandTarget::Model:
                return "model";
            case training::CommandTarget::Optimizer:
                return "optimizer";
            case training::CommandTarget::Session:
                return "session";
            }
            return "unknown";
        }

        training::CommandTarget string_to_target(const std::string& s) {
            if (s == "model")
                return training::CommandTarget::Model;
            if (s == "optimizer")
                return training::CommandTarget::Optimizer;
            if (s == "session")
                return training::CommandTarget::Session;
            return training::CommandTarget::Session;
        }

    } // namespace

    ToolRegistry& ToolRegistry::instance() {
        static ToolRegistry inst;
        return inst;
    }

    void ToolRegistry::register_tool(McpTool tool, ToolHandler handler) {
        std::lock_guard lock(mutex_);
        std::string name = tool.name;
        tools_[name] = RegisteredTool{std::move(tool), std::move(handler)};
    }

    void ToolRegistry::unregister_tool(const std::string& name) {
        std::lock_guard lock(mutex_);
        tools_.erase(name);
    }

    std::vector<McpTool> ToolRegistry::list_tools() const {
        std::lock_guard lock(mutex_);
        std::vector<McpTool> result;
        result.reserve(tools_.size());
        for (const auto& [name, reg] : tools_) {
            result.push_back(reg.tool);
        }
        return result;
    }

    json ToolRegistry::call_tool(const std::string& name, const json& arguments) {
        std::lock_guard lock(mutex_);

        auto it = tools_.find(name);
        if (it == tools_.end()) {
            return json{{"error", "Tool not found: " + name}};
        }

        return it->second.handler(arguments);
    }

    json ToolRegistry::arg_type_to_json_schema(training::ArgType type) const {
        json schema;
        switch (type) {
        case training::ArgType::Int:
            schema["type"] = "integer";
            break;
        case training::ArgType::Float:
            schema["type"] = "number";
            break;
        case training::ArgType::Bool:
            schema["type"] = "boolean";
            break;
        case training::ArgType::String:
            schema["type"] = "string";
            break;
        case training::ArgType::IntList:
            schema["type"] = "array";
            schema["items"] = json{{"type", "integer"}};
            break;
        case training::ArgType::FloatList:
            schema["type"] = "array";
            schema["items"] = json{{"type", "number"}};
            break;
        }
        return schema;
    }

    McpTool ToolRegistry::operation_to_tool(const training::OperationInfo& op) const {
        McpTool tool;

        std::string target_str = target_to_string(op.target);
        tool.name = target_str + "." + op.name;
        tool.description = op.description;

        json properties;
        std::vector<std::string> required;

        for (const auto& arg : op.args) {
            json prop = arg_type_to_json_schema(arg.type);
            if (arg.description) {
                prop["description"] = *arg.description;
            }
            properties[arg.name] = prop;

            if (arg.required) {
                required.push_back(arg.name);
            }
        }

        bool has_selection = !op.selectors.empty() &&
                             op.selectors.size() > 1; // More than just "All"
        if (has_selection) {
            json selection_prop;
            selection_prop["type"] = "object";
            selection_prop["description"] = "Selection of gaussians to operate on";
            selection_prop["properties"] = json{
                {"kind", json{{"type", "string"}, {"enum", json::array({"all", "range", "indices"})}}},
                {"start", json{{"type", "integer"}, {"description", "Start index for range selection"}}},
                {"end", json{{"type", "integer"}, {"description", "End index (exclusive) for range selection"}}},
                {"indices", json{{"type", "array"}, {"items", json{{"type", "integer"}}}, {"description", "Specific indices to select"}}}};
            properties["selection"] = selection_prop;
        }

        tool.input_schema.properties = properties;
        tool.input_schema.required = required;

        return tool;
    }

    training::Command ToolRegistry::json_to_command(
        const training::OperationInfo& op,
        const json& arguments) const {

        training::Command cmd;
        cmd.target = op.target;
        cmd.op = op.name;

        if (arguments.contains("selection")) {
            const auto& sel = arguments["selection"];
            std::string kind = sel.value("kind", "all");

            if (kind == "range") {
                cmd.selection.kind = training::SelectionKind::Range;
                cmd.selection.start = sel.value("start", int64_t(0));
                cmd.selection.end = sel.value("end", int64_t(0));
            } else if (kind == "indices") {
                cmd.selection.kind = training::SelectionKind::Indices;
                if (sel.contains("indices")) {
                    cmd.selection.indices = sel["indices"].get<std::vector<int64_t>>();
                }
            } else {
                cmd.selection.kind = training::SelectionKind::All;
            }
        }

        for (const auto& arg_spec : op.args) {
            if (!arguments.contains(arg_spec.name)) {
                continue;
            }

            const auto& val = arguments[arg_spec.name];
            training::ArgValue arg_val;

            switch (arg_spec.type) {
            case training::ArgType::Int:
                arg_val = val.get<int64_t>();
                break;
            case training::ArgType::Float:
                arg_val = val.get<double>();
                break;
            case training::ArgType::Bool:
                arg_val = val.get<bool>();
                break;
            case training::ArgType::String:
                arg_val = val.get<std::string>();
                break;
            case training::ArgType::IntList:
                arg_val = val.get<std::vector<int64_t>>();
                break;
            case training::ArgType::FloatList:
                arg_val = val.get<std::vector<double>>();
                break;
            }

            cmd.args[arg_spec.name] = arg_val;
        }

        return cmd;
    }

    ToolRegistry::ToolHandler ToolRegistry::create_command_handler(
        const training::OperationInfo& op) const {

        return [this, op](const json& arguments) -> json {
            auto* cc = event::command_center();
            if (!cc) {
                return json{{"error", "Training system not initialized"}};
            }

            training::Command cmd = json_to_command(op, arguments);

            auto result = cc->execute(cmd);
            if (!result) {
                return json{{"error", result.error()}};
            }

            json response;
            response["success"] = true;
            response["operation"] = target_to_string(op.target) + "." + op.name;

            auto snapshot = cc->snapshot();
            response["state"] = json{
                {"iteration", snapshot.iteration},
                {"num_gaussians", snapshot.num_gaussians},
                {"loss", snapshot.loss},
                {"is_running", snapshot.is_running},
                {"is_paused", snapshot.is_paused}};

            return response;
        };
    }

    void ToolRegistry::generate_from_command_center() {
        auto* cc = event::command_center();
        if (!cc) {
            LOG_WARN("Cannot generate MCP tools: CommandCenter not available");
            return;
        }

        auto ops = cc->operations();

        for (const auto& op : ops) {
            McpTool tool = operation_to_tool(op);
            ToolHandler handler = create_command_handler(op);
            register_tool(std::move(tool), std::move(handler));
        }

        LOG_INFO("Generated {} MCP tools from CommandCenter", ops.size());
    }

    void register_builtin_tools() {
        auto& registry = ToolRegistry::instance();

        registry.register_tool(
            McpTool{
                .name = "training.get_state",
                .description = "Get current training state snapshot",
                .input_schema = {.type = "object", .properties = json::object(), .required = {}}},
            [](const json&) -> json {
                auto* cc = event::command_center();
                if (!cc) {
                    return json{{"error", "Training system not initialized"}};
                }

                auto snapshot = cc->snapshot();
                return json{
                    {"iteration", snapshot.iteration},
                    {"max_iterations", snapshot.max_iterations},
                    {"num_gaussians", snapshot.num_gaussians},
                    {"loss", snapshot.loss},
                    {"is_running", snapshot.is_running},
                    {"is_paused", snapshot.is_paused},
                    {"is_refining", snapshot.is_refining}};
            });

        registry.register_tool(
            McpTool{
                .name = "training.list_operations",
                .description = "List all available CommandCenter operations",
                .input_schema = {.type = "object", .properties = json::object(), .required = {}}},
            [](const json&) -> json {
                auto* cc = event::command_center();
                if (!cc) {
                    return json{{"error", "Training system not initialized"}};
                }

                auto ops = cc->operations();
                json result = json::array();

                for (const auto& op : ops) {
                    json op_json;
                    op_json["name"] = target_to_string(op.target) + "." + op.name;
                    op_json["description"] = op.description;

                    json args = json::array();
                    for (const auto& arg : op.args) {
                        json arg_json;
                        arg_json["name"] = arg.name;
                        arg_json["required"] = arg.required;
                        if (arg.description) {
                            arg_json["description"] = *arg.description;
                        }
                        args.push_back(arg_json);
                    }
                    op_json["args"] = args;
                    result.push_back(op_json);
                }

                return result;
            });

        registry.register_tool(
            McpTool{
                .name = "training.get_loss_history",
                .description = "Get training loss history",
                .input_schema = {
                    .type = "object",
                    .properties = json{
                        {"last_n", json{{"type", "integer"}, {"description", "Return only last N points (default: all)"}}}},
                    .required = {}}},
            [](const json& args) -> json {
                auto* cc = event::command_center();
                if (!cc) {
                    return json{{"error", "Training system not initialized"}};
                }

                auto history = cc->loss_history();
                int last_n = args.contains("last_n") ? args["last_n"].get<int>() : 0;

                json points = json::array();
                size_t start = 0;
                if (last_n > 0 && static_cast<size_t>(last_n) < history.size()) {
                    start = history.size() - last_n;
                }

                for (size_t i = start; i < history.size(); ++i) {
                    points.push_back(json{
                        {"iteration", history[i].iteration},
                        {"loss", history[i].loss}});
                }

                return json{{"count", points.size()}, {"points", points}};
            });

        registry.generate_from_command_center();
        register_scene_tools();
    }

} // namespace lfs::mcp
