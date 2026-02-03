# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Layout definitions for the LichtFeld Studio UI.

This module provides the declarative layout trees that define the UI structure.
Each layout corresponds to a different area of the application:

- side_panel: Main side panel with tabs (Training, Rendering, etc.)
- viewport: Viewport overlays (toolbars, progress indicators)
- status_bar: Status bar at the bottom
- menus: Menu bar structure

Usage:
    from lfs_plugins.layouts import get_side_panel_layout, get_viewport_layout

    # Get layout tree for rendering
    layout = get_side_panel_layout()
"""

from .side_panel import get_side_panel_layout
from .viewport import get_viewport_layout
from .status_bar import get_status_bar_layout
from .menus import get_menu_classes, get_menu_bar_entries

__all__ = [
    "get_side_panel_layout",
    "get_viewport_layout",
    "get_status_bar_layout",
    "get_menu_classes",
    "get_menu_bar_entries",
]
