"""Operators for the gaussian analyzer plugin."""

import lichtfeld as lf
from lfs_plugins.types import Operator
from lfs_plugins.props import FloatProperty, EnumProperty


class AnalyzeOperator(Operator):
    label = "Analyze Gaussians"
    description = "Analyze gaussian property distribution"

    property_name: str = EnumProperty(
        items=[
            ("opacity", "Opacity", "Analyze opacity distribution"),
            ("scale", "Scale", "Analyze scale distribution"),
        ],
    )

    @classmethod
    def poll(cls, context) -> bool:
        return lf.has_scene()

    def execute(self, context) -> set:
        scene = lf.get_scene()
        model = scene.combined_model()
        if model is None:
            lf.log.warn("No splat data")
            return {"CANCELLED"}

        if self.property_name == "opacity":
            data = model.get_opacity().squeeze()
            label = "Opacity"
        else:
            data = model.get_scaling().mean(dim=1)
            label = "Scale"

        mean_val = data.mean().item()
        min_val = data.min().item()
        max_val = data.max().item()

        lf.log.info(f"{label} stats: mean={mean_val:.4f}, min={min_val:.4f}, max={max_val:.4f}")
        return {"FINISHED"}


class FilterOperator(Operator):
    label = "Filter Gaussians"
    description = "Remove gaussians below opacity threshold"
    options = {"UNDO"}

    threshold: float = FloatProperty(default=0.01, min=0.0, max=1.0)

    @classmethod
    def poll(cls, context) -> bool:
        return lf.has_scene()

    def execute(self, context) -> set:
        scene = lf.get_scene()
        model = scene.combined_model()
        if model is None:
            return {"CANCELLED"}

        opacity = model.get_opacity().squeeze()
        mask = opacity < self.threshold
        count = int(mask.sum().item())

        if count == 0:
            lf.log.info("No gaussians below threshold")
            return {"FINISHED"}

        model.soft_delete(mask)
        removed = model.apply_deleted()
        lf.log.info(f"Removed {removed} gaussians with opacity < {self.threshold}")

        return {"FINISHED"}
