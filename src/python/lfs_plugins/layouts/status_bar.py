# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Status bar layout definition.

The status bar appears at the bottom of the window and shows
application status information.
"""

from __future__ import annotations

from ..ui.layout import LayoutNode, Panel


def get_status_bar_layout() -> LayoutNode:
    """Get the layout tree for the status bar.

    Returns a single StatusBarPanel.
    """
    from ..status_bar_panel import StatusBarPanel

    return Panel(StatusBarPanel)
