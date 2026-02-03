# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Layout tree renderer.

The LayoutRenderer walks the layout tree and invokes the appropriate
drawing functions. It bridges the declarative layout nodes to the
imperative ImGui-style drawing API.

Example:
    renderer = LayoutRenderer()
    renderer.render(layout_tree, imgui_context)
"""

from __future__ import annotations

from typing import Any, Callable, Dict

from .discovery import ComponentRegistry
from .layout import (
    Collapsible,
    Conditional,
    Group,
    LayoutNode,
    Panel,
    Spacer,
    Stack,
    Tabs,
)
from .protocols import Drawable, Pollable


class LayoutRenderer:
    """Renders a layout tree using the ImGui-style drawing API.

    The renderer maintains state for tabs (selected indices) and collapsibles
    (open/closed state) across frames.
    """

    def __init__(self, registry: ComponentRegistry | None = None) -> None:
        self._registry = registry or ComponentRegistry.instance()
        self._tab_states: Dict[int, int] = {}
        self._collapsible_states: Dict[int, bool] = {}

    def render(self, root: LayoutNode, layout: Any, context: Any = None) -> None:
        """Render a layout tree.

        Args:
            root: Root of the layout tree.
            layout: PyUILayout object for drawing.
            context: Optional application context for poll() calls.
        """
        self._render_node(root, layout, context)

    def _render_node(self, node: LayoutNode, layout: Any, context: Any) -> None:
        """Render a single node and its children."""
        if isinstance(node, Panel):
            self._render_panel(node, layout, context)
        elif isinstance(node, Stack):
            self._render_stack(node, layout, context)
        elif isinstance(node, Tabs):
            self._render_tabs(node, layout, context)
        elif isinstance(node, Conditional):
            self._render_conditional(node, layout, context)
        elif isinstance(node, Collapsible):
            self._render_collapsible(node, layout, context)
        elif isinstance(node, Spacer):
            self._render_spacer(node, layout)
        elif isinstance(node, Group):
            self._render_group(node, layout, context)

    def _render_panel(self, node: Panel, layout: Any, context: Any) -> None:
        """Render a panel node."""
        panel_class = node.panel_class

        if isinstance(panel_class, type) and issubclass(panel_class, Pollable):
            if hasattr(panel_class, "poll") and not panel_class.poll(context):
                return

        if node.instance:
            instance = node.instance
        else:
            instance = self._registry.get_or_create(panel_class)

        instance.draw(layout)

    def _render_stack(self, node: Stack, layout: Any, context: Any) -> None:
        """Render a stack node."""
        for i, child in enumerate(node.children):
            if i > 0 and node.spacing > 0:
                layout.separator()
            self._render_node(child, layout, context)

    def _render_tabs(self, node: Tabs, layout: Any, context: Any) -> None:
        """Render a tabs node."""
        node_id = id(node)

        if node_id not in self._tab_states:
            self._tab_states[node_id] = node.default_index

        current_index = self._tab_states[node_id]

        if hasattr(layout, "begin_tab_bar"):
            if layout.begin_tab_bar(f"##tabs_{node_id}"):
                for i, (child, label) in enumerate(zip(node.children, node.labels)):
                    if layout.begin_tab_item(label):
                        self._tab_states[node_id] = i
                        self._render_node(child, layout, context)
                        layout.end_tab_item()
                layout.end_tab_bar()
        else:
            if 0 <= current_index < len(node.children):
                self._render_node(node.children[current_index], layout, context)

    def _render_conditional(self, node: Conditional, layout: Any, context: Any) -> None:
        """Render a conditional node."""
        for child in node.get_children():
            self._render_node(child, layout, context)

    def _render_collapsible(self, node: Collapsible, layout: Any, context: Any) -> None:
        """Render a collapsible node."""
        node_id = id(node)

        if node_id not in self._collapsible_states:
            self._collapsible_states[node_id] = node.default_open

        is_open = self._collapsible_states[node_id]

        if hasattr(layout, "collapsing_header"):
            new_open = layout.collapsing_header(node.label, default_open=is_open)
            if new_open:
                self._collapsible_states[node_id] = True
                self._render_node(node.child, layout, context)
            else:
                self._collapsible_states[node_id] = False

    def _render_spacer(self, node: Spacer, layout: Any) -> None:
        """Render a spacer node."""
        if node.height > 0 and hasattr(layout, "dummy"):
            layout.dummy(0, node.height)
        else:
            layout.separator()

    def _render_group(self, node: Group, layout: Any, context: Any) -> None:
        """Render a group node."""
        for child in node.children:
            self._render_node(child, layout, context)
