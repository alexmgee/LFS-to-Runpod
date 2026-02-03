# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Component discovery and registry.

The ComponentRegistry manages panel instances, ensuring singletons and
handling lifecycle. Unlike the old registration system, discovery is
automatic from the layout tree.

Example:
    registry = ComponentRegistry()
    layout = Stack([Panel(TrainingPanel), Panel(RenderingPanel)])

    # Discover all panels from layout
    registry.discover_from_layout(layout)

    # Get singleton instance
    panel = registry.get_or_create(TrainingPanel)
"""

from __future__ import annotations

from typing import Any, Dict, Type, TypeVar
from weakref import WeakValueDictionary

from .layout import LayoutNode, Panel, find_panels
from .protocols import Drawable

T = TypeVar("T", bound=Drawable)


class ComponentRegistry:
    """Registry for UI component instances.

    Manages singleton instances of panel classes. Each panel class has at most
    one instance, created on first access.

    The registry can be populated automatically from a layout tree, or
    instances can be registered manually.
    """

    _instance: ComponentRegistry | None = None

    def __init__(self) -> None:
        self._instances: Dict[Type[Drawable], Drawable] = {}
        self._class_ids: Dict[str, Type[Drawable]] = {}

    @classmethod
    def instance(cls) -> ComponentRegistry:
        """Get the global registry instance."""
        if cls._instance is None:
            cls._instance = ComponentRegistry()
        return cls._instance

    def get_or_create(self, panel_class: Type[T]) -> T:
        """Get or create a singleton instance of a panel class.

        Args:
            panel_class: The panel class to instantiate.

        Returns:
            The singleton instance.
        """
        if panel_class not in self._instances:
            self._instances[panel_class] = panel_class()
            class_id = self._get_class_id(panel_class)
            self._class_ids[class_id] = panel_class
        return self._instances[panel_class]  # type: ignore

    def get(self, panel_class: Type[T]) -> T | None:
        """Get an existing instance without creating.

        Args:
            panel_class: The panel class to look up.

        Returns:
            The instance if it exists, None otherwise.
        """
        return self._instances.get(panel_class)  # type: ignore

    def get_by_id(self, class_id: str) -> Drawable | None:
        """Get a panel instance by its class ID string.

        Args:
            class_id: The class ID (module.qualname format).

        Returns:
            The instance if registered, None otherwise.
        """
        panel_class = self._class_ids.get(class_id)
        if panel_class:
            return self._instances.get(panel_class)
        return None

    def register(self, panel_class: Type[Drawable], instance: Drawable | None = None) -> None:
        """Explicitly register a panel class.

        Args:
            panel_class: The panel class to register.
            instance: Optional pre-created instance.
        """
        if instance is not None:
            self._instances[panel_class] = instance
        class_id = self._get_class_id(panel_class)
        self._class_ids[class_id] = panel_class

    def unregister(self, panel_class: Type[Drawable]) -> None:
        """Unregister a panel class.

        Args:
            panel_class: The panel class to unregister.
        """
        self._instances.pop(panel_class, None)
        class_id = self._get_class_id(panel_class)
        self._class_ids.pop(class_id, None)

    def discover_from_layout(self, root: LayoutNode) -> list[Type[Drawable]]:
        """Discover and register all panel classes from a layout tree.

        Args:
            root: Root of the layout tree to scan.

        Returns:
            List of discovered panel classes.
        """
        panels = find_panels(root)
        discovered = []

        for panel_node in panels:
            panel_class = panel_node.panel_class
            if panel_node.instance:
                self._instances[panel_class] = panel_node.instance
            class_id = self._get_class_id(panel_class)
            self._class_ids[class_id] = panel_class
            discovered.append(panel_class)

        return discovered

    def clear(self) -> None:
        """Clear all registered instances."""
        self._instances.clear()
        self._class_ids.clear()

    def all_classes(self) -> list[Type[Drawable]]:
        """Get all registered panel classes."""
        return list(self._class_ids.values())

    def all_instances(self) -> list[Drawable]:
        """Get all created instances."""
        return list(self._instances.values())

    @staticmethod
    def _get_class_id(cls: Type) -> str:
        """Get the unique ID for a class."""
        return f"{cls.__module__}.{cls.__qualname__}"
