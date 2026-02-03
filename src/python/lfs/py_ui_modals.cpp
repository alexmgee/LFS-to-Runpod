/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "core/logger.hpp"
#include "py_ui.hpp"
#include "theme/theme.hpp"

#include <cstring>
#include <imgui.h>

namespace lfs::python {

    PyModalRegistry& PyModalRegistry::instance() {
        static PyModalRegistry registry;
        return registry;
    }

    void PyModalRegistry::show_confirm(const std::string& title, const std::string& message,
                                       const std::vector<std::string>& buttons, nb::object callback) {
        std::lock_guard lock(mutex_);
        PyModalDialog modal;
        modal.id = "modal_" + std::to_string(next_id_++);
        modal.title = title;
        modal.message = message;
        modal.buttons = buttons.empty() ? std::vector<std::string>{"OK", "Cancel"} : buttons;
        modal.callback = callback;
        modal.type = ModalDialogType::Confirm;
        modal.is_open = true;
        modals_.push_back(std::move(modal));
    }

    void PyModalRegistry::show_input(const std::string& title, const std::string& message,
                                     const std::string& default_value, nb::object callback) {
        std::lock_guard lock(mutex_);
        PyModalDialog modal;
        modal.id = "modal_" + std::to_string(next_id_++);
        modal.title = title;
        modal.message = message;
        modal.buttons = {"OK", "Cancel"};
        modal.callback = callback;
        modal.type = ModalDialogType::Input;
        modal.input_value = default_value;
        modal.is_open = true;
        modals_.push_back(std::move(modal));
    }

    void PyModalRegistry::show_message(const std::string& title, const std::string& message,
                                       MessageStyle style, nb::object callback) {
        std::lock_guard lock(mutex_);
        PyModalDialog modal;
        modal.id = "modal_" + std::to_string(next_id_++);
        modal.title = title;
        modal.message = message;
        modal.buttons = {"OK"};
        modal.callback = callback;
        modal.type = ModalDialogType::Message;
        modal.style = style;
        modal.is_open = true;
        modals_.push_back(std::move(modal));
    }

    bool PyModalRegistry::has_open_modals() const {
        std::lock_guard lock(mutex_);
        return !modals_.empty();
    }

    void PyModalRegistry::draw_confirm_dialog(PyModalDialog& modal) {
        ImGui::TextWrapped("%s", modal.message.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        std::string clicked_button;
        for (size_t i = 0; i < modal.buttons.size(); ++i) {
            if (i > 0)
                ImGui::SameLine();
            if (ImGui::Button(modal.buttons[i].c_str(), ImVec2(80, 0))) {
                clicked_button = modal.buttons[i];
                modal.is_open = false;
            }
        }

        if (!clicked_button.empty() && modal.callback.is_valid() && !modal.callback.is_none()) {
            nb::gil_scoped_acquire gil;
            try {
                modal.callback(clicked_button);
            } catch (const std::exception& e) {
                LOG_ERROR("Modal callback error: {}", e.what());
            }
        }
    }

    void PyModalRegistry::draw_input_dialog(PyModalDialog& modal) {
        ImGui::TextWrapped("%s", modal.message.c_str());
        ImGui::Spacing();

        static char input_buf[1024];
        strncpy(input_buf, modal.input_value.c_str(), sizeof(input_buf) - 1);
        input_buf[sizeof(input_buf) - 1] = '\0';

        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##input", input_buf, sizeof(input_buf))) {
            modal.input_value = input_buf;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool submitted = false;
        bool cancelled = false;

        if (ImGui::Button("OK", ImVec2(80, 0))) {
            submitted = true;
            modal.is_open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            cancelled = true;
            modal.is_open = false;
        }

        if (modal.callback.is_valid() && !modal.callback.is_none()) {
            nb::gil_scoped_acquire gil;
            try {
                if (submitted) {
                    modal.callback(nb::str(modal.input_value.c_str()));
                } else if (cancelled) {
                    modal.callback(nb::none());
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Modal callback error: {}", e.what());
            }
        }
    }

    void PyModalRegistry::draw_message_dialog(PyModalDialog& modal) {
        ImGui::TextWrapped("%s", modal.message.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("OK", ImVec2(80, 0))) {
            modal.is_open = false;
            if (modal.callback.is_valid() && !modal.callback.is_none()) {
                nb::gil_scoped_acquire gil;
                try {
                    modal.callback();
                } catch (const std::exception& e) {
                    LOG_ERROR("Modal callback error: {}", e.what());
                }
            }
        }
    }

    void PyModalRegistry::draw_modals() {
        std::lock_guard lock(mutex_);

        for (auto it = modals_.begin(); it != modals_.end();) {
            auto& modal = *it;

            if (!modal.is_open) {
                it = modals_.erase(it);
                continue;
            }

            // Apply style-based border color
            const auto& t = lfs::vis::theme();
            ImVec4 border_color;
            switch (modal.style) {
            case MessageStyle::Warning:
                border_color = t.palette.warning;
                break;
            case MessageStyle::Error:
                border_color = t.palette.error;
                break;
            default:
                border_color = t.palette.success;
                break;
            }

            ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                    ImVec2(0.5f, 0.5f));

            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                               ImGuiWindowFlags_NoCollapse |
                                               ImGuiWindowFlags_NoDocking;

            ImGui::PushStyleColor(ImGuiCol_Border, border_color);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

            if (ImGui::Begin(modal.title.c_str(), &modal.is_open, flags)) {
                switch (modal.type) {
                case ModalDialogType::Confirm:
                    draw_confirm_dialog(modal);
                    break;
                case ModalDialogType::Input:
                    draw_input_dialog(modal);
                    break;
                case ModalDialogType::Message:
                    draw_message_dialog(modal);
                    break;
                }
            }
            ImGui::End();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            if (!modal.is_open) {
                it = modals_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void register_ui_modals(nb::module_& m) {
        m.def(
            "confirm_dialog",
            [](const std::string& title, const std::string& message,
               const std::vector<std::string>& buttons, nb::object callback) {
                PyModalRegistry::instance().show_confirm(title, message, buttons, callback);
            },
            nb::arg("title"), nb::arg("message"),
            nb::arg("buttons") = std::vector<std::string>{"OK", "Cancel"},
            nb::arg("callback") = nb::none(),
            "Show a confirmation dialog with custom buttons");

        m.def(
            "input_dialog",
            [](const std::string& title, const std::string& message,
               const std::string& default_value, nb::object callback) {
                PyModalRegistry::instance().show_input(title, message, default_value, callback);
            },
            nb::arg("title"), nb::arg("message"),
            nb::arg("default_value") = "",
            nb::arg("callback") = nb::none(),
            "Show an input dialog");

        m.def(
            "message_dialog",
            [](const std::string& title, const std::string& message,
               const std::string& style, nb::object callback) {
                MessageStyle msg_style = MessageStyle::Info;
                if (style == "warning")
                    msg_style = MessageStyle::Warning;
                else if (style == "error")
                    msg_style = MessageStyle::Error;
                PyModalRegistry::instance().show_message(title, message, msg_style, callback);
            },
            nb::arg("title"), nb::arg("message"),
            nb::arg("style") = "info",
            nb::arg("callback") = nb::none(),
            "Show a message dialog (style: 'info', 'warning', or 'error')");
    }

} // namespace lfs::python
