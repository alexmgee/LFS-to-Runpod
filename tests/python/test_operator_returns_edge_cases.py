# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for operator return value edge cases."""

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
def op_fixture(lf, lfs_types):
    """Setup and cleanup for operator tests."""
    registered = []

    yield registered, lfs_types

    for op_cls in registered:
        try:
            lf.unregister_class(op_cls)
        except Exception:
            pass


class TestOperatorReturnsEdgeCases:
    """Tests for operator return value edge cases."""

    def test_return_empty_set(self, lf, op_fixture):
        """Operator returning empty set."""
        registered, lfs_types = op_fixture

        class EmptySetOp(lfs_types.Operator):
            lf_label = "Empty Set"

            def execute(self, context):
                return set()

        lf.register_class(EmptySetOp)
        registered.append(EmptySetOp)

        result = lf.ops.invoke(EmptySetOp._class_id())
        # Empty set may be interpreted as CANCELLED or error
        assert result is not None

    def test_return_invalid_status_string(self, lf, op_fixture):
        """Operator returning invalid status string."""
        registered, lfs_types = op_fixture

        class InvalidStatusOp(lfs_types.Operator):
            lf_label = "Invalid Status"

            def execute(self, context):
                return {"INVALID_STATUS"}

        lf.register_class(InvalidStatusOp)
        registered.append(InvalidStatusOp)

        result = lf.ops.invoke(InvalidStatusOp._class_id())
        assert result is not None

    def test_return_dict_without_status(self, lf, op_fixture):
        """Operator returning dict without status key."""
        registered, lfs_types = op_fixture

        class NoStatusDictOp(lfs_types.Operator):
            lf_label = "No Status Dict"

            def execute(self, context):
                return {"value": 42, "message": "no status"}

        lf.register_class(NoStatusDictOp)
        registered.append(NoStatusDictOp)

        result = lf.ops.invoke(NoStatusDictOp._class_id())
        assert result is not None

    def test_return_iterable_with_custom_iter(self, lf, op_fixture):
        """Operator returning object with custom __iter__."""
        registered, lfs_types = op_fixture

        class CustomIterable:
            def __iter__(self):
                return iter(["FINISHED"])

        class CustomIterOp(lfs_types.Operator):
            lf_label = "Custom Iter"

            def execute(self, context):
                return CustomIterable()

        lf.register_class(CustomIterOp)
        registered.append(CustomIterOp)

        result = lf.ops.invoke(CustomIterOp._class_id())
        assert result is not None

    def test_return_none(self, lf, op_fixture):
        """Operator returning None."""
        registered, lfs_types = op_fixture

        class NoneReturnOp(lfs_types.Operator):
            lf_label = "None Return"

            def execute(self, context):
                return None

        lf.register_class(NoneReturnOp)
        registered.append(NoneReturnOp)

        result = lf.ops.invoke(NoneReturnOp._class_id())
        assert result is not None

    def test_return_with_exception_in_conversion(self, lf, op_fixture):
        """Operator returning object that fails during conversion."""
        registered, lfs_types = op_fixture

        class BadConvert:
            def __iter__(self):
                raise RuntimeError("Conversion failed")

        class BadConvertOp(lfs_types.Operator):
            lf_label = "Bad Convert"

            def execute(self, context):
                return BadConvert()

        lf.register_class(BadConvertOp)
        registered.append(BadConvertOp)

        result = lf.ops.invoke(BadConvertOp._class_id())
        assert result is not None

    def test_return_mixed_set(self, lf, op_fixture):
        """Operator returning set with mixed types."""
        registered, lfs_types = op_fixture

        class MixedSetOp(lfs_types.Operator):
            lf_label = "Mixed Set"

            def execute(self, context):
                return {"FINISHED", 42, None}

        lf.register_class(MixedSetOp)
        registered.append(MixedSetOp)

        result = lf.ops.invoke(MixedSetOp._class_id())
        assert result is not None


class TestOperatorModalReturns:
    """Tests for modal operator return values."""

    def test_modal_return_running(self, lf, op_fixture):
        """Modal operator returning RUNNING_MODAL."""
        registered, lfs_types = op_fixture

        class RunningModalOp(lfs_types.Operator):
            lf_label = "Running Modal"

            def invoke(self, context, event):
                return {"RUNNING_MODAL"}

            def modal(self, context, event):
                return {"FINISHED"}

        lf.register_class(RunningModalOp)
        registered.append(RunningModalOp)

    def test_modal_return_pass_through(self, lf, op_fixture):
        """Modal operator returning PASS_THROUGH."""
        registered, lfs_types = op_fixture

        class PassThroughOp(lfs_types.Operator):
            lf_label = "Pass Through"

            def invoke(self, context, event):
                return {"RUNNING_MODAL"}

            def modal(self, context, event):
                return {"PASS_THROUGH"}

        lf.register_class(PassThroughOp)
        registered.append(PassThroughOp)


class TestOperatorPropertyReturns:
    """Tests for operators returning property values."""

    def test_return_with_property_access(self, lf, op_fixture):
        """Operator that accesses properties in return."""
        registered, lfs_types = op_fixture

        class PropAccessOp(lfs_types.Operator):
            lf_label = "Prop Access"

            result_value: str = "default"

            def execute(self, context):
                self.result_value = "modified"
                return {"FINISHED"}

        lf.register_class(PropAccessOp)
        registered.append(PropAccessOp)

        result = lf.ops.invoke(PropAccessOp._class_id())
        assert result is not None

    def test_return_tuple(self, lf, op_fixture):
        """Operator returning tuple instead of set."""
        registered, lfs_types = op_fixture

        class TupleReturnOp(lfs_types.Operator):
            lf_label = "Tuple Return"

            def execute(self, context):
                return ("FINISHED",)

        lf.register_class(TupleReturnOp)
        registered.append(TupleReturnOp)

        result = lf.ops.invoke(TupleReturnOp._class_id())
        assert result is not None

    def test_return_list(self, lf, op_fixture):
        """Operator returning list instead of set."""
        registered, lfs_types = op_fixture

        class ListReturnOp(lfs_types.Operator):
            lf_label = "List Return"

            def execute(self, context):
                return ["FINISHED"]

        lf.register_class(ListReturnOp)
        registered.append(ListReturnOp)

        result = lf.ops.invoke(ListReturnOp._class_id())
        assert result is not None
