"""Gaussian Analyzer - Full plugin example combining panels, operators, tools, signals, and capabilities."""

from pathlib import Path

import lichtfeld as lf
from lfs_plugins.tool_defs.definition import ToolDef
from lfs_plugins.tools import ToolRegistry
from lfs_plugins.capabilities import CapabilityRegistry, CapabilitySchema

from .panels.analyzer_panel import AnalyzerPanel
from .panels.stats_overlay import StatsOverlay
from .operators import AnalyzeOperator, FilterOperator

_PLUGIN_NAME = "gaussian_analyzer"
_PLUGIN_PATH = str(Path(__file__).parent)

_classes = [AnalyzeOperator, FilterOperator, AnalyzerPanel, StatsOverlay]
_tool_id = f"{_PLUGIN_NAME}.analyze_tool"


def _handle_analyze(args: dict, ctx) -> dict:
    """Capability handler: analyze gaussians by property."""
    prop = args.get("property", "opacity")
    threshold = args.get("threshold", 0.5)

    if ctx.scene is None:
        return {"success": False, "error": "No scene loaded"}

    scene = ctx.scene.scene
    model = scene.combined_model()
    if model is None:
        return {"success": False, "error": "No splat data"}

    if prop == "opacity":
        data = model.get_opacity().squeeze()
    elif prop == "scale":
        data = model.get_scaling().mean(dim=1)
    else:
        return {"success": False, "error": f"Unknown property: {prop}"}

    mask = data < threshold
    count = int(mask.sum().item())
    return {"success": True, "count": count, "total": model.num_points}


def on_load():
    for cls in _classes:
        lf.register_class(cls)

    tool = ToolDef(
        id=_tool_id,
        label="Analyze",
        icon="search",
        group="utility",
        order=210,
        description="Analyze gaussian properties",
        plugin_name=_PLUGIN_NAME,
        plugin_path=_PLUGIN_PATH,
    )
    ToolRegistry.register_tool(tool)

    CapabilityRegistry.instance().register(
        name=f"{_PLUGIN_NAME}.analyze",
        handler=_handle_analyze,
        description="Analyze gaussians by property threshold",
        schema=CapabilitySchema(
            properties={
                "property": {"type": "string", "enum": ["opacity", "scale"]},
                "threshold": {"type": "number", "default": 0.5},
            },
            required=["property"],
        ),
        plugin_name=_PLUGIN_NAME,
        requires_gui=False,
    )

    lf.log.info(f"{_PLUGIN_NAME} loaded")


def on_unload():
    CapabilityRegistry.instance().unregister_all_for_plugin(_PLUGIN_NAME)
    ToolRegistry.unregister_tool(_tool_id)

    for cls in reversed(_classes):
        lf.unregister_class(cls)

    lf.log.info(f"{_PLUGIN_NAME} unloaded")
