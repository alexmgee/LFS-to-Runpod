# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Modern UI framework with reactive signals and compositional layouts.

This module provides:

- Reactive signals for state management
- Compositional layout trees
- Protocol-based panels

Usage:
    from lfs_plugins.ui import Signal, AppState, Stack, Panel, Tabs

    # Read reactive state
    if AppState.is_training.value:
        print(f"Iteration: {AppState.iteration.value}")

    # Define layouts compositionally
    layout = Stack([
        Tabs(
            children=[Panel(TrainingPanel), Panel(RenderingPanel)],
            labels=["Training", "Rendering"],
        ),
    ])
"""

from .signals import Signal, ComputedSignal, ThrottledSignal, Batch
from .subscription_registry import SubscriptionRegistry
from .state import AppState
from .layout import (
    LayoutNode,
    Stack,
    Tabs,
    Panel,
    Conditional,
    Collapsible,
    Spacer,
    Group,
)
from .protocols import Drawable, Pollable, PanelLike
from .discovery import ComponentRegistry
from .renderer import LayoutRenderer

__all__ = [
    # Signals
    "Signal",
    "ComputedSignal",
    "ThrottledSignal",
    "Batch",
    "SubscriptionRegistry",
    # State
    "AppState",
    # Layout
    "LayoutNode",
    "Stack",
    "Tabs",
    "Panel",
    "Conditional",
    "Collapsible",
    "Spacer",
    "Group",
    # Protocols
    "Drawable",
    "Pollable",
    "PanelLike",
    # Discovery
    "ComponentRegistry",
    # Rendering
    "LayoutRenderer",
]
