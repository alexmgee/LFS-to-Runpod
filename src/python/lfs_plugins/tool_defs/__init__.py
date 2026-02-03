# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Modern declarative tool system.

This module provides a dataclass-based tool definition system that replaces
the class-based inheritance pattern.

Usage:
    from lfs_plugins.tool_defs import ToolDef, SubmodeDef, BUILTIN_TOOLS

    # Access builtin tools
    translate_tool = next(t for t in BUILTIN_TOOLS if t.id == "builtin.translate")

    # Define custom tools
    my_tool = ToolDef(
        id="myplugin.mytool",
        label="My Tool",
        icon="custom-icon",
        group="custom",
    )
"""

from .definition import ToolDef, SubmodeDef, PivotModeDef
from .builtin import BUILTIN_TOOLS, get_tool_by_id

__all__ = [
    "ToolDef",
    "SubmodeDef",
    "PivotModeDef",
    "BUILTIN_TOOLS",
    "get_tool_by_id",
]
