# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Menu bar structure and registry.

Menus are defined declaratively and registered automatically without
needing lf.register_class() calls.
"""

from __future__ import annotations

from typing import Any

# Global list of menu classes - populated at import time
_MENU_CLASSES: list[type] = []


def register_menu(menu_class: type) -> type:
    """Decorator to register a menu class.

    This replaces the need to call lf.register_class(MenuClass).
    Menus are registered in the order they are defined.

    Usage:
        @register_menu
        class FileMenu:
            label = "menu.file"
            location = "MENU_BAR"
            order = 10

            def draw(self, layout):
                ...
    """
    _MENU_CLASSES.append(menu_class)
    return menu_class


def get_menu_classes() -> list[type]:
    """Get all registered menu classes in order.

    Returns:
        List of menu classes sorted by their 'order' attribute.
    """
    # Import menu modules to trigger @register_menu decorators
    from .. import file_menu, edit_menu, view_menu, help_menu  # noqa: F401

    return sorted(_MENU_CLASSES, key=lambda m: getattr(m, "order", 100))


def get_menu_bar_entries() -> list[tuple[str, str, int, type]]:
    """Get menu bar entries for C++ rendering.

    Returns:
        List of (idname, label, order, menu_class) tuples.
    """
    result = []
    for cls in get_menu_classes():
        location = getattr(cls, "location", "")
        if location == "MENU_BAR":
            idname = f"{cls.__module__}.{cls.__qualname__}"
            label = getattr(cls, "label", "")
            order = getattr(cls, "order", 100)
            result.append((idname, label, order, cls))
    return result


def _clear_menus():
    """Clear all registered menus (for testing/reload)."""
    _MENU_CLASSES.clear()
