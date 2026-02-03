# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Protocol definitions for UI components.

Protocols define the interfaces that components must implement. Using protocols
instead of base classes enables duck typing - any class that has the right
methods works, no inheritance required.

Example:
    class MyPanel:
        label = "My Panel"
        def draw(self, ui): ...

    # Works with Panel() even though it doesn't inherit from anything
    layout = Panel(MyPanel)
"""

from __future__ import annotations

from typing import Any, Protocol, runtime_checkable


@runtime_checkable
class Drawable(Protocol):
    """Protocol for objects that can draw UI content.

    Any class with a draw(layout) method satisfies this protocol.
    """

    def draw(self, layout: Any) -> None:
        """Draw the component's content.

        Args:
            layout: PyUILayout object for adding UI elements.
        """
        ...


@runtime_checkable
class Pollable(Protocol):
    """Protocol for objects with visibility polling.

    Objects implementing this can control when they're shown.
    """

    @classmethod
    def poll(cls, context: Any) -> bool:
        """Check if the component should be visible.

        Args:
            context: Application context object.

        Returns:
            True if component should be drawn, False otherwise.
        """
        ...


@runtime_checkable
class PanelLike(Protocol):
    """Protocol for panel-like objects.

    Combines Drawable with optional metadata attributes.
    """

    label: str

    def draw(self, layout: Any) -> None:
        """Draw the panel content."""
        ...
