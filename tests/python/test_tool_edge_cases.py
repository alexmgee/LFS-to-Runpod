# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tool registration edge cases.

Note: Tool API may not be available in all builds. Tests are skipped
if the required API is not present.
"""

import gc
import sys
from pathlib import Path

import pytest


@pytest.fixture
def lfs_types():
    """Import lfs_plugins.types module."""
    project_root = Path(__file__).parent.parent.parent
    build_python = project_root / "build" / "src" / "python"
    if str(build_python) not in sys.path:
        sys.path.insert(0, str(build_python))

    try:
        from lfs_plugins import types
        return types
    except ImportError as e:
        pytest.skip(f"lfs_plugins.types not available: {e}")


@pytest.fixture
def tool_api(lf):
    """Check if tool API is available."""
    if not hasattr(lf, "register_class"):
        pytest.skip("lf.register_class not available")
    return lf


@pytest.fixture
def tool_fixture(lf, lfs_types):
    """Setup and cleanup for tool tests."""
    registered = []

    yield registered, lfs_types

    for cls in registered:
        try:
            lf.unregister_class(cls)
        except Exception:
            pass


class TestToolEdgeCases:
    """Tests for tool registration edge cases."""

    def test_tool_without_required_attributes(self, lf, lfs_types):
        """Tool missing required attributes should fail gracefully."""
        if not hasattr(lfs_types, "Tool"):
            pytest.skip("lfs_types.Tool not available")

        class NoIdTool(lfs_types.Tool):
            pass

        try:
            lf.register_class(NoIdTool)
            lf.unregister_class(NoIdTool)
        except (AttributeError, KeyError, TypeError, ValueError):
            pass  # Expected

    def test_rapid_register_unregister_cycle(self, lf, tool_fixture):
        """Rapid register/unregister cycles."""
        registered, lfs_types = tool_fixture

        if not hasattr(lfs_types, "Operator"):
            pytest.skip("lfs_types.Operator not available")

        class CycleOp(lfs_types.Operator):
            lf_label = "Cycle Op"

            def execute(self, context):
                return {"FINISHED"}

        for _ in range(50):
            try:
                lf.register_class(CycleOp)
                lf.unregister_class(CycleOp)
            except Exception:
                break  # Stop on first error

        gc.collect()

    def test_operator_with_exception_in_poll(self, lf, tool_fixture):
        """Operator with exception in poll()."""
        registered, lfs_types = tool_fixture

        class PollExcOp(lfs_types.Operator):
            lf_label = "Poll Exception Op"

            @classmethod
            def poll(cls, context):
                raise RuntimeError("Poll failed")

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(PollExcOp)
        registered.append(PollExcOp)

        # Poll should return False when it raises
        try:
            can_use = lf.ui.poll_operator(PollExcOp._class_id())
            # May return False or raise
        except (RuntimeError, AttributeError):
            pass

    def test_operator_with_exception_in_invoke(self, lf, tool_fixture):
        """Operator with exception in invoke()."""
        registered, lfs_types = tool_fixture

        class InvokeExcOp(lfs_types.Operator):
            lf_label = "Invoke Exception Op"

            def invoke(self, context, event):
                raise RuntimeError("Invoke failed")

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(InvokeExcOp)
        registered.append(InvokeExcOp)

        try:
            lf.ops.invoke(InvokeExcOp._class_id())
        except RuntimeError:
            pass  # Expected

    def test_operator_double_registration(self, lf, tool_fixture):
        """Registering same operator twice."""
        registered, lfs_types = tool_fixture

        class DoubleOp(lfs_types.Operator):
            lf_label = "Double Op"

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(DoubleOp)
        registered.append(DoubleOp)

        try:
            lf.register_class(DoubleOp)
        except (ValueError, RuntimeError):
            pass  # Expected


class TestOperatorCallbacks:
    """Tests for operator callback edge cases."""

    def test_operator_modal_exception(self, lf, tool_fixture):
        """Operator modal() that raises exception."""
        registered, lfs_types = tool_fixture

        class ModalExcOp(lfs_types.Operator):
            lf_label = "Modal Exception"

            def invoke(self, context, event):
                return {"RUNNING_MODAL"}

            def modal(self, context, event):
                raise RuntimeError("Modal failed")

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(ModalExcOp)
        registered.append(ModalExcOp)

        # Invoking would start modal which will fail

    def test_operator_draw_exception(self, lf, tool_fixture):
        """Operator draw() that raises exception."""
        registered, lfs_types = tool_fixture

        class DrawExcOp(lfs_types.Operator):
            lf_label = "Draw Exception"

            def draw(self, layout):
                raise RuntimeError("Draw failed")

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(DrawExcOp)
        registered.append(DrawExcOp)

        # Drawing would fail but shouldn't crash


class TestOperatorProperties:
    """Tests for operator property edge cases."""

    def test_operator_with_property_types(self, lf, tool_fixture):
        """Operator with various property types."""
        registered, lfs_types = tool_fixture

        class PropOp(lfs_types.Operator):
            lf_label = "Property Op"

            int_prop: int = 0
            float_prop: float = 0.0
            str_prop: str = ""
            bool_prop: bool = False

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(PropOp)
        registered.append(PropOp)

        lf.ops.invoke(PropOp._class_id())

    def test_operator_property_access_during_invoke(self, lf, tool_fixture):
        """Access properties during operator invoke."""
        registered, lfs_types = tool_fixture
        accessed_props = []

        class AccessOp(lfs_types.Operator):
            lf_label = "Access Op"

            value: int = 42

            def execute(self, context):
                accessed_props.append(self.value)
                return {"FINISHED"}

        lf.register_class(AccessOp)
        registered.append(AccessOp)

        lf.ops.invoke(AccessOp._class_id())
        # Property should have been accessible
        assert 42 in accessed_props
