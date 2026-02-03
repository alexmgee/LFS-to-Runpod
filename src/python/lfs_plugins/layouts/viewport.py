# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Viewport overlay layout definitions.

Viewport overlays include toolbars and progress indicators that appear
over the 3D viewport.
"""

from __future__ import annotations

from ..ui.layout import LayoutNode, Stack, Panel, Conditional, Group
from ..ui.state import AppState


def get_viewport_layout() -> LayoutNode:
    """Get the layout tree for viewport overlays.

    The viewport has multiple overlay areas:
    - Left: Gizmo toolbar
    - Right: Utility toolbar
    - Center: Progress overlays (import, export, video)
    - Full: Empty state, startup, drag-drop

    Note: Actual positioning is handled by the renderer based on the
    overlay type, not by the layout tree structure.
    """
    from ..toolbar import GizmoToolbar, UtilityToolbar
    from ..overlays.empty_state import EmptyStateOverlay
    from ..overlays.startup import StartupOverlay
    from ..overlays.export_progress import ExportProgressOverlay
    from ..overlays.video_progress import VideoProgressOverlay
    from ..overlays.import_progress import ImportProgressOverlay
    from ..overlays.drag_drop import DragDropOverlay

    return Group(
        [
            Panel(GizmoToolbar),
            Panel(UtilityToolbar),
            Panel(EmptyStateOverlay),
            Panel(StartupOverlay),
            Panel(ExportProgressOverlay),
            Panel(VideoProgressOverlay),
            Panel(ImportProgressOverlay),
            Panel(DragDropOverlay),
        ],
        name="viewport_overlays",
    )


def get_toolbar_layout() -> LayoutNode:
    """Get just the toolbar overlays."""
    from ..toolbar import GizmoToolbar, UtilityToolbar

    return Group(
        [
            Panel(GizmoToolbar),
            Panel(UtilityToolbar),
        ],
        name="toolbars",
    )


def get_progress_overlays_layout() -> LayoutNode:
    """Get just the progress overlays."""
    from ..overlays.export_progress import ExportProgressOverlay
    from ..overlays.video_progress import VideoProgressOverlay
    from ..overlays.import_progress import ImportProgressOverlay

    return Group(
        [
            Panel(ExportProgressOverlay),
            Panel(VideoProgressOverlay),
            Panel(ImportProgressOverlay),
        ],
        name="progress_overlays",
    )
