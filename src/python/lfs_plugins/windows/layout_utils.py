# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Layout utilities for window content centering."""


def center_content(layout, content_width: float) -> float:
    """Center content horizontally. Call before drawing. Returns indent applied."""
    avail_w, _ = layout.get_content_region_avail()
    if content_width < avail_w:
        indent = (avail_w - content_width) / 2
        layout.set_cursor_pos_x(layout.get_cursor_pos()[0] + indent)
        return indent
    return 0.0


def calc_row_width(item_count: int, item_width: float, spacing: float) -> float:
    """Calculate total width of a row with items and spacing."""
    return item_count * item_width + (item_count - 1) * spacing


def calc_window_width(item_count: int, item_width: float, spacing: float, padding: float) -> float:
    """Calculate window width for a row of items with equal padding on both sides."""
    content_width = calc_row_width(item_count, item_width, spacing)
    return content_width + 2 * padding
