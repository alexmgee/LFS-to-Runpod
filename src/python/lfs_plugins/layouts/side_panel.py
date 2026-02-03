# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Side panel layout definition.

The side panel contains the main tabbed interface with Training, Rendering,
and Plugins tabs.
"""

from __future__ import annotations

from ..ui.layout import LayoutNode, Stack, Tabs, Panel, Conditional


def get_side_panel_layout() -> LayoutNode:
    """Get the layout tree for the main side panel.

    Returns a Tabs node containing the main panel tabs ordered by priority:
    1. Rendering (general rendering settings)
    2. Training (training controls and parameters)
    3. Plugins (plugin manager)
    """
    from ..rendering_panel import RenderingPanel
    from ..training_panel import TrainingPanel
    from ..panels import PluginManagerPanel

    return Tabs(
        children=[
            Panel(RenderingPanel),
            Panel(TrainingPanel),
            Panel(PluginManagerPanel),
        ],
        labels=["Rendering", "Training", "Plugins"],
        default_index=0,
    )


def get_scene_header_layout() -> LayoutNode:
    """Get the layout tree for the scene header.

    The scene header shows scene information and navigation controls.
    """
    from ..scene_panel import ScenePanel

    return Panel(ScenePanel)
