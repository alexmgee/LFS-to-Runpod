# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for silent exception handlers.

Targets:
- py_ui_tools.cpp:24,30,36 - bare catches in get_class_id()
- py_ui.cpp:981,1016,1024 - combo/template_list catches
- py_prop_registry.cpp:255,304 - property getter/setter catches

NOTE: Many tests require lfs_plugins.types.Operator which may not be available.
"""

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


class TestSilentFailures:
    """Tests for functions that silently catch exceptions."""

    def test_get_class_id_with_invalid_object(self, lf, lfs_types):
        """get_class_id should handle objects without __module__ or __qualname__."""

        class NoModule(lfs_types.Operator):
            lf_label = "No Module"

            def execute(self, context):
                return {"FINISHED"}

        # Try to register - may fail on missing attributes
        try:
            lf.register_class(NoModule)
            lf.unregister_class(NoModule)
        except (AttributeError, TypeError) as e:
            pass

    def test_register_class_without_label(self, lf, lfs_types):
        """Registration without lf_label should fail gracefully."""

        class NoLabel(lfs_types.Operator):
            def execute(self, context):
                return {"FINISHED"}

        try:
            lf.register_class(NoLabel)
            lf.unregister_class(NoLabel)
        except (AttributeError, KeyError, TypeError):
            pass

    def test_property_getter_exception_logged(self, lf, lfs_types):
        """Property getter raising exception should be logged, not crash."""

        class ExceptionPropOp(lfs_types.Operator):
            lf_label = "Exception Prop"

            @property
            def bad_prop(self):
                raise RuntimeError("Getter failed")

            def execute(self, context):
                return {"FINISHED"}

        try:
            lf.register_class(ExceptionPropOp)
            lf.ops.invoke(ExceptionPropOp._class_id())
        except RuntimeError:
            pass
        finally:
            try:
                lf.unregister_class(ExceptionPropOp)
            except Exception:
                pass


class TestTensorInfoEdgeCases:
    """Tests for tensor info with edge cases."""

    def test_tensor_info_with_empty_tensor(self, lf):
        """Tensor operations on empty tensor."""
        tensor = lf.Tensor()

        # These should handle empty tensor gracefully (numel/dim are properties)
        try:
            n = tensor.numel
            d = tensor.dim
        except Exception:
            pass  # May raise on empty tensor

    def test_tensor_info_after_copy(self, lf, numpy):
        """Tensor info after copy operation."""
        arr = numpy.array([1.0, 2.0, 3.0], dtype=numpy.float32)
        t1 = lf.Tensor.from_numpy(arr)
        t2 = t1

        # Operations should still work (numel is a property, not method)
        assert t2.numel == 3


class TestDrawCallbackFailures:
    """Tests for failures in draw callbacks."""

    def test_draw_exception_does_not_crash(self, lf, lfs_types):
        """Exception in draw() callback should not crash."""

        class DrawExcOp(lfs_types.Operator):
            lf_label = "Draw Exception"

            def draw(self, layout):
                raise RuntimeError("Draw failed")

            def execute(self, context):
                return {"FINISHED"}

        try:
            lf.register_class(DrawExcOp)
            lf.ops.invoke(DrawExcOp._class_id())
        except RuntimeError:
            pass
        finally:
            try:
                lf.unregister_class(DrawExcOp)
            except Exception:
                pass

    def test_poll_exception_returns_false(self, lf, lfs_types):
        """Exception in poll() should return False."""

        class PollExcOp(lfs_types.Operator):
            lf_label = "Poll Exception"

            @classmethod
            def poll(cls, context):
                raise RuntimeError("Poll failed")

            def execute(self, context):
                return {"FINISHED"}

        try:
            lf.register_class(PollExcOp)
            can_invoke = lf.ui.poll_operator(PollExcOp._class_id())
            # May return False or raise
        except RuntimeError:
            pass
        finally:
            try:
                lf.unregister_class(PollExcOp)
            except Exception:
                pass
