# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""View menu implementation."""

import lichtfeld as lf
from .layouts.menus import register_menu


def _draw_plugins_submenu(layout):
    """Draw the plugins submenu content."""
    try:
        from .manager import PluginManager
        from .plugin import PluginState

        mgr = PluginManager.instance()
        plugins = mgr.discover()

        if not plugins:
            layout.text_colored("No plugins installed", (0.6, 0.6, 0.6, 1.0))
        else:
            for plugin in plugins:
                if layout.begin_menu(plugin.name):
                    layout.text_colored(f"v{plugin.version}", (0.6, 0.6, 0.6, 1.0))
                    if plugin.description:
                        layout.text_wrapped(plugin.description)

                    layout.separator()

                    state = mgr.get_state(plugin.name)
                    is_loaded = state == PluginState.ACTIVE

                    if is_loaded:
                        layout.text_colored("Active", (0.3, 0.9, 0.3, 1.0))
                        if layout.menu_item("Reload"):
                            try:
                                mgr.unload(plugin.name)
                                mgr.load(plugin.name)
                            except Exception:
                                pass
                        if layout.menu_item("Unload"):
                            try:
                                mgr.unload(plugin.name)
                            except Exception:
                                pass
                    else:
                        if layout.menu_item("Load"):
                            try:
                                mgr.load(plugin.name)
                            except Exception:
                                pass

                    layout.separator()
                    if layout.menu_item("Uninstall"):
                        try:
                            mgr.uninstall(plugin.name)
                        except Exception:
                            pass

                    layout.end_menu()

        layout.separator()
        if layout.menu_item("Install from GitHub..."):
            pass
    except ImportError:
        layout.text_colored("Plugin system unavailable", (0.6, 0.6, 0.6, 1.0))


@register_menu
class ViewMenu:
    """View menu for the menu bar."""

    label = "menu.view"
    location = "MENU_BAR"
    order = 30

    def draw(self, layout):
        if layout.begin_menu(lf.ui.tr("menu.view.theme")):
            is_dark = lf.ui.get_theme() == "Dark"
            if layout.menu_item_toggle(lf.ui.tr("menu.view.theme.dark"), "", is_dark):
                lf.ui.set_theme("dark")
            if layout.menu_item_toggle(lf.ui.tr("menu.view.theme.light"), "", not is_dark):
                lf.ui.set_theme("light")
            layout.end_menu()

        layout.separator()

        if layout.menu_item_shortcut("Python Console", "Ctrl+`"):
            lf.ui.show_python_console()

        if layout.begin_menu("Plugins"):
            _draw_plugins_submenu(layout)
            layout.end_menu()



def register():
    pass


def unregister():
    pass
