/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "gui/panels/windows_console_utils.hpp"
#include "core/event_bridge/localization_manager.hpp"
#include "gui/string_keys.hpp"
#include <imgui.h>
#ifdef WIN32
#include <windows.h>
#endif

namespace lfs::vis::gui::panels {

    using namespace lichtfeld::Strings;

    void DrawSystemConsoleButton(const UIContext& ctx) {

#ifdef WIN32
        // On non-Windows platforms, dont show the console toggle button

        if (!ctx.window_states->at("system_console")) {
            if (ImGui::Button(LOC(MainPanel::SHOW_CONSOLE), ImVec2(-1, 0))) {
                HWND hwnd = GetConsoleWindow();
                Sleep(1);
                HWND owner = GetWindow(hwnd, GW_OWNER);

                if (owner == NULL) {
                    ShowWindow(hwnd, SW_SHOW); // Windows 10
                } else {
                    ShowWindow(owner, SW_SHOW); // Windows 11
                }
                ctx.window_states->at("system_console") = true;
            }
        } else {
            if (ImGui::Button(LOC(MainPanel::HIDE_CONSOLE), ImVec2(-1, 0))) {
                HWND hwnd = GetConsoleWindow();
                Sleep(1);
                HWND owner = GetWindow(hwnd, GW_OWNER);

                if (owner == NULL) {
                    ShowWindow(hwnd, SW_HIDE); // Windows 10
                } else {
                    ShowWindow(owner, SW_HIDE); // Windows 11
                }
                ctx.window_states->at("system_console") = false;
            }
        }
#endif // Win32
    }

} // namespace lfs::vis::gui::panels
