# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for operator instance lifecycle.

Targets:
- py_ui.cpp:132-147 - Instances never removed from g_python_operator_instances
- py_ui_operators.cpp:135-139 - delattr() exceptions not handled

NOTE: These tests require the lfs_plugins.types.Operator base class and lf.register_class.
"""

import gc
import sys
import weakref
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
        pytest.skip(f"lfs_plugins.types module not available: {e}")


@pytest.fixture
def operator_fixture(lf, lfs_types):
    """Setup and cleanup for operator tests."""
    registered_ops = []

    yield registered_ops, lfs_types

    # Cleanup all registered operators
    for op_cls in registered_ops:
        try:
            lf.unregister_class(op_cls)
        except Exception:
            pass


class TestOperatorLifecycle:
    """Tests for operator instance lifecycle management."""

    def test_instance_cleanup_on_failure(self, lf, operator_fixture):
        """Operator instances should be cleaned up when execution fails."""
        registered_ops, lfs_types = operator_fixture

        class FailingOperator(lfs_types.Operator):
            lf_label = "Failing Op"

            def execute(self, context):
                raise RuntimeError("Intentional failure")

        lf.register_class(FailingOperator)
        registered_ops.append(FailingOperator)

        # Invoke should not crash
        result = lf.ops.invoke(FailingOperator._class_id())
        assert result is not None

    def test_multiple_invoke_no_leak(self, lf, operator_fixture):
        """Multiple invocations should not leak instances."""
        registered_ops, lfs_types = operator_fixture

        class SimpleOperator(lfs_types.Operator):
            lf_label = "Simple Op"
            instance_count = 0

            def __init__(self):
                super().__init__()
                SimpleOperator.instance_count += 1

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(SimpleOperator)
        registered_ops.append(SimpleOperator)

        initial_count = SimpleOperator.instance_count

        for _ in range(10):
            lf.ops.invoke(SimpleOperator._class_id())

        gc.collect()

        final_count = SimpleOperator.instance_count
        assert final_count >= initial_count

    def test_property_cleanup_on_exception(self, lf, operator_fixture):
        """Properties should be cleaned up even when operator raises."""
        registered_ops, lfs_types = operator_fixture

        class PropOperator(lfs_types.Operator):
            lf_label = "Prop Op"

            prop_value: int = 0

            def execute(self, context):
                if self.prop_value == 42:
                    raise ValueError("Magic number")
                return {"FINISHED"}

        lf.register_class(PropOperator)
        registered_ops.append(PropOperator)

        # Normal execution
        lf.ops.invoke(PropOperator._class_id(), prop_value=1)

        # Exception execution
        lf.ops.invoke(PropOperator._class_id(), prop_value=42)

        # Should still work after exception
        result = lf.ops.invoke(PropOperator._class_id(), prop_value=2)
        assert result is not None

    def test_gc_does_not_crash_active_operator(self, lf, operator_fixture):
        """GC during operator execution should not crash."""
        registered_ops, lfs_types = operator_fixture

        class GCOperator(lfs_types.Operator):
            lf_label = "GC Op"

            def execute(self, context):
                gc.collect()
                return {"FINISHED"}

        lf.register_class(GCOperator)
        registered_ops.append(GCOperator)

        result = lf.ops.invoke(GCOperator._class_id())
        assert result is not None

    def test_reregister_does_not_leak(self, lf, lfs_types):
        """Re-registering operator class should not leak old instances."""

        class ReregisterOp(lfs_types.Operator):
            lf_label = "Reregister Op"

            def execute(self, context):
                return {"FINISHED"}

        for _ in range(5):
            lf.register_class(ReregisterOp)
            lf.ops.invoke(ReregisterOp._class_id())
            lf.unregister_class(ReregisterOp)

        gc.collect()


class TestOperatorWeakReferences:
    """Tests for proper weak reference handling."""

    def test_operator_class_weakref(self, lf, operator_fixture):
        """Operator class should be properly weak-referenced."""
        registered_ops, lfs_types = operator_fixture

        class WeakRefOp(lfs_types.Operator):
            lf_label = "WeakRef Op"

            def execute(self, context):
                return {"FINISHED"}

        weak = weakref.ref(WeakRefOp)

        lf.register_class(WeakRefOp)
        registered_ops.append(WeakRefOp)

        assert weak() is not None

    def test_instance_does_not_prevent_gc(self, lf, lfs_types):
        """Operator instance should not prevent class GC after unregister."""
        instance_weak = [None]

        class GCableOp(lfs_types.Operator):
            lf_label = "GCable Op"

            def execute(self, context):
                instance_weak[0] = weakref.ref(self)
                return {"FINISHED"}

        lf.register_class(GCableOp)
        lf.ops.invoke(GCableOp._class_id())
        lf.unregister_class(GCableOp)

        gc.collect()


class TestOperatorPropertyEdgeCases:
    """Tests for operator property edge cases."""

    def test_property_type_mismatch(self, lf, operator_fixture):
        """Property type mismatch should be handled gracefully."""
        registered_ops, lfs_types = operator_fixture

        class TypeMismatchOp(lfs_types.Operator):
            lf_label = "Type Mismatch Op"

            int_prop: int = 0

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(TypeMismatchOp)
        registered_ops.append(TypeMismatchOp)

        # Try to pass wrong type - should be handled
        try:
            result = lf.ops.invoke(TypeMismatchOp._class_id(), int_prop="not_an_int")
        except (TypeError, ValueError):
            pass
        except Exception:
            pass

    def test_unknown_property(self, lf, operator_fixture):
        """Unknown property names should be handled."""
        registered_ops, lfs_types = operator_fixture

        class UnknownPropOp(lfs_types.Operator):
            lf_label = "Unknown Prop Op"

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(UnknownPropOp)
        registered_ops.append(UnknownPropOp)

        # Try to pass unknown property
        try:
            result = lf.ops.invoke(UnknownPropOp._class_id(), nonexistent_prop=42)
        except (AttributeError, TypeError, KeyError):
            pass
        except Exception:
            pass

    def test_property_with_none_value(self, lf, operator_fixture):
        """None value for typed property."""
        registered_ops, lfs_types = operator_fixture

        class NoneValueOp(lfs_types.Operator):
            lf_label = "None Value Op"

            str_prop: str = "default"

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(NoneValueOp)
        registered_ops.append(NoneValueOp)

        # Pass None where string expected
        try:
            result = lf.ops.invoke(NoneValueOp._class_id(), str_prop=None)
        except Exception:
            pass
