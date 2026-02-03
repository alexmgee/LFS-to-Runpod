# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compositional layout system for UI structure.

Layout nodes define the UI structure declaratively, providing explicit
structure visible in one place.

Example:
    layout = Stack([
        Tabs(
            children=[Panel(TrainingPanel), Panel(RenderingPanel)],
            labels=["Training", "Rendering"],
        ),
        Conditional(
            condition=lambda: AppState.has_selection.value,
            child=Collapsible("Selection", Panel(SelectionPanel)),
        ),
    ])
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, Callable, Sequence, Type

from .protocols import Drawable


class LayoutNode(ABC):
    """Base class for layout tree nodes.

    Layout nodes form a tree that describes the UI structure. The renderer
    walks this tree to draw the actual UI.
    """

    @abstractmethod
    def get_children(self) -> Sequence[LayoutNode]:
        """Return child nodes for tree traversal."""
        ...


@dataclass(frozen=True)
class Panel(LayoutNode):
    """A leaf node wrapping a drawable panel class.

    Panel nodes wrap panel classes and handle instantiation. The panel's
    draw() method is called when rendering.

    Args:
        panel_class: The panel class to instantiate and draw.
        instance: Optional pre-created instance (for singleton panels).

    Example:
        Panel(TrainingPanel)
        Panel(RenderingPanel)
    """

    panel_class: Type[Drawable]
    instance: Drawable | None = None

    def get_children(self) -> Sequence[LayoutNode]:
        return ()

    @property
    def label(self) -> str:
        if hasattr(self.panel_class, "label"):
            return self.panel_class.label
        return self.panel_class.__name__


@dataclass(frozen=True)
class Stack(LayoutNode):
    """A vertical stack of child nodes.

    Children are drawn in order, top to bottom.

    Args:
        children: Child layout nodes.
        spacing: Space between children in pixels.

    Example:
        Stack([
            Panel(HeaderPanel),
            Panel(ContentPanel),
            Panel(FooterPanel),
        ])
    """

    children: tuple[LayoutNode, ...] = ()
    spacing: int = 0

    def __init__(self, children: Sequence[LayoutNode], spacing: int = 0):
        object.__setattr__(self, "children", tuple(children))
        object.__setattr__(self, "spacing", spacing)

    def get_children(self) -> Sequence[LayoutNode]:
        return self.children


@dataclass(frozen=True)
class Tabs(LayoutNode):
    """A tabbed container where only one child is visible at a time.

    Args:
        children: Child layout nodes, one per tab.
        labels: Tab labels, must match number of children.
        default_index: Initially selected tab index.

    Example:
        Tabs(
            children=[Panel(TrainingPanel), Panel(RenderingPanel)],
            labels=["Training", "Rendering"],
            default_index=0,
        )
    """

    children: tuple[LayoutNode, ...] = ()
    labels: tuple[str, ...] = ()
    default_index: int = 0

    def __init__(
        self,
        children: Sequence[LayoutNode],
        labels: Sequence[str] | None = None,
        default_index: int = 0,
    ):
        children_tuple = tuple(children)
        object.__setattr__(self, "children", children_tuple)

        if labels is None:
            auto_labels = []
            for child in children_tuple:
                if isinstance(child, Panel):
                    auto_labels.append(child.label)
                else:
                    auto_labels.append("")
            object.__setattr__(self, "labels", tuple(auto_labels))
        else:
            assert len(labels) == len(children_tuple), "Labels must match children count"
            object.__setattr__(self, "labels", tuple(labels))

        object.__setattr__(self, "default_index", default_index)

    def get_children(self) -> Sequence[LayoutNode]:
        return self.children


@dataclass(frozen=True)
class Conditional(LayoutNode):
    """A node that conditionally shows its child based on a condition.

    The condition is a callable that returns bool. When false, an optional
    fallback is shown instead.

    Args:
        condition: Callable returning bool. Called every render.
        child: Node to show when condition is true.
        fallback: Optional node to show when condition is false.

    Example:
        Conditional(
            condition=lambda: AppState.has_selection.value,
            child=Panel(SelectionPanel),
            fallback=Panel(NoSelectionPanel),
        )
    """

    condition: Callable[[], bool]
    child: LayoutNode
    fallback: LayoutNode | None = None

    def get_children(self) -> Sequence[LayoutNode]:
        if self.condition():
            return (self.child,)
        elif self.fallback:
            return (self.fallback,)
        return ()


@dataclass(frozen=True)
class Collapsible(LayoutNode):
    """A collapsible section with a header.

    Args:
        label: Header text shown even when collapsed.
        child: Content shown when expanded.
        default_open: Whether initially expanded.

    Example:
        Collapsible(
            "Advanced Settings",
            Panel(AdvancedSettingsPanel),
            default_open=False,
        )
    """

    label: str
    child: LayoutNode
    default_open: bool = True

    def get_children(self) -> Sequence[LayoutNode]:
        return (self.child,)


@dataclass(frozen=True)
class Spacer(LayoutNode):
    """An empty space for layout purposes.

    Args:
        height: Height in pixels. If 0, uses default spacing.
    """

    height: int = 0

    def get_children(self) -> Sequence[LayoutNode]:
        return ()


@dataclass(frozen=True)
class Group(LayoutNode):
    """A logical grouping of nodes without visual container.

    Useful for organizing related nodes that shouldn't have a visible wrapper.

    Args:
        children: Child nodes.
        name: Optional name for debugging.
    """

    children: tuple[LayoutNode, ...] = ()
    name: str = ""

    def __init__(self, children: Sequence[LayoutNode], name: str = ""):
        object.__setattr__(self, "children", tuple(children))
        object.__setattr__(self, "name", name)

    def get_children(self) -> Sequence[LayoutNode]:
        return self.children


def walk_layout(node: LayoutNode) -> list[LayoutNode]:
    """Walk the layout tree depth-first, returning all nodes."""
    result = [node]
    for child in node.get_children():
        result.extend(walk_layout(child))
    return result


def find_panels(node: LayoutNode) -> list[Panel]:
    """Find all Panel nodes in a layout tree."""
    return [n for n in walk_layout(node) if isinstance(n, Panel)]
