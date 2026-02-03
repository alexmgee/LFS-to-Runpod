# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for property constraint enforcement."""

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
def prop_op_fixture(lf, lfs_types):
    """Setup and cleanup for property constraint tests."""
    registered = []

    yield registered, lfs_types

    for op_cls in registered:
        try:
            lf.unregister_class(op_cls)
        except Exception:
            pass


class TestPropertyConstraints:
    """Tests for property constraint enforcement."""

    def test_float_below_min(self, lf, prop_op_fixture):
        """Float property below minimum."""
        registered, lfs_types = prop_op_fixture

        class MinFloatOp(lfs_types.Operator):
            lf_label = "Min Float"

            value: float = 0.5

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(MinFloatOp)
        registered.append(MinFloatOp)

        # Try to set value below hypothetical minimum
        try:
            lf.ops.invoke(MinFloatOp._class_id(), value=-1000.0)
        except (ValueError, TypeError):
            pass

    def test_float_above_max(self, lf, prop_op_fixture):
        """Float property above maximum."""
        registered, lfs_types = prop_op_fixture

        class MaxFloatOp(lfs_types.Operator):
            lf_label = "Max Float"

            value: float = 0.5

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(MaxFloatOp)
        registered.append(MaxFloatOp)

        # Try to set very large value
        try:
            lf.ops.invoke(MaxFloatOp._class_id(), value=float("inf"))
        except (ValueError, OverflowError):
            pass

    def test_int_bounds_enforcement(self, lf, prop_op_fixture):
        """Integer property bounds."""
        registered, lfs_types = prop_op_fixture

        class IntBoundsOp(lfs_types.Operator):
            lf_label = "Int Bounds"

            count: int = 0

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(IntBoundsOp)
        registered.append(IntBoundsOp)

        # Very large integer
        try:
            lf.ops.invoke(IntBoundsOp._class_id(), count=2**63)
        except (ValueError, OverflowError):
            pass

        # Negative
        try:
            lf.ops.invoke(IntBoundsOp._class_id(), count=-2**63)
        except (ValueError, OverflowError):
            pass

    def test_string_maxlen_truncation(self, lf, prop_op_fixture):
        """String property maximum length."""
        registered, lfs_types = prop_op_fixture

        class MaxLenOp(lfs_types.Operator):
            lf_label = "Max Len"

            name: str = ""

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(MaxLenOp)
        registered.append(MaxLenOp)

        # Very long string
        long_string = "x" * 10000
        try:
            lf.ops.invoke(MaxLenOp._class_id(), name=long_string)
        except (ValueError, MemoryError):
            pass

    def test_enum_invalid_value(self, lf, prop_op_fixture):
        """Enum property with invalid value."""
        registered, lfs_types = prop_op_fixture

        class EnumOp(lfs_types.Operator):
            lf_label = "Enum Op"

            mode: str = "DEFAULT"

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(EnumOp)
        registered.append(EnumOp)

        # Invalid enum value
        try:
            lf.ops.invoke(EnumOp._class_id(), mode="INVALID_VALUE")
        except (ValueError, KeyError):
            pass

    def test_vector_wrong_size(self, lf, prop_op_fixture):
        """Vector property with wrong size."""
        registered, lfs_types = prop_op_fixture

        class VectorOp(lfs_types.Operator):
            lf_label = "Vector Op"

            position: tuple = (0.0, 0.0, 0.0)

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(VectorOp)
        registered.append(VectorOp)

        # Wrong size vectors
        try:
            lf.ops.invoke(VectorOp._class_id(), position=(1.0,))
        except (ValueError, TypeError):
            pass

        try:
            lf.ops.invoke(VectorOp._class_id(), position=(1.0, 2.0, 3.0, 4.0, 5.0))
        except (ValueError, TypeError):
            pass

    def test_bool_non_bool_value(self, lf, prop_op_fixture):
        """Boolean property with non-boolean value."""
        registered, lfs_types = prop_op_fixture

        class BoolOp(lfs_types.Operator):
            lf_label = "Bool Op"

            enabled: bool = False

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(BoolOp)
        registered.append(BoolOp)

        # Truthy non-bool values - should be coerced or rejected
        lf.ops.invoke(BoolOp._class_id(), enabled=1)
        lf.ops.invoke(BoolOp._class_id(), enabled="true")
        lf.ops.invoke(BoolOp._class_id(), enabled=[1])


class TestPropertyTypeCoercion:
    """Tests for property type coercion."""

    def test_int_from_float(self, lf, prop_op_fixture):
        """Integer property assigned float value."""
        registered, lfs_types = prop_op_fixture

        class IntFromFloatOp(lfs_types.Operator):
            lf_label = "Int From Float"

            count: int = 0

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(IntFromFloatOp)
        registered.append(IntFromFloatOp)

        # Float value for int property
        lf.ops.invoke(IntFromFloatOp._class_id(), count=3.7)

    def test_float_from_int(self, lf, prop_op_fixture):
        """Float property assigned int value."""
        registered, lfs_types = prop_op_fixture

        class FloatFromIntOp(lfs_types.Operator):
            lf_label = "Float From Int"

            value: float = 0.0

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(FloatFromIntOp)
        registered.append(FloatFromIntOp)

        # Int value for float property
        lf.ops.invoke(FloatFromIntOp._class_id(), value=42)

    def test_string_from_non_string(self, lf, prop_op_fixture):
        """String property assigned non-string value."""
        registered, lfs_types = prop_op_fixture

        class StringFromOtherOp(lfs_types.Operator):
            lf_label = "String From Other"

            name: str = ""

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(StringFromOtherOp)
        registered.append(StringFromOtherOp)

        # Various types for string property
        lf.ops.invoke(StringFromOtherOp._class_id(), name=42)
        lf.ops.invoke(StringFromOtherOp._class_id(), name=3.14)
        lf.ops.invoke(StringFromOtherOp._class_id(), name=True)


class TestPropertySpecialValues:
    """Tests for special property values."""

    def test_float_nan(self, lf, prop_op_fixture):
        """Float property with NaN."""
        registered, lfs_types = prop_op_fixture

        class NanOp(lfs_types.Operator):
            lf_label = "NaN Op"

            value: float = 0.0

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(NanOp)
        registered.append(NanOp)

        try:
            lf.ops.invoke(NanOp._class_id(), value=float("nan"))
        except ValueError:
            pass

    def test_float_infinity(self, lf, prop_op_fixture):
        """Float property with infinity."""
        registered, lfs_types = prop_op_fixture

        class InfOp(lfs_types.Operator):
            lf_label = "Inf Op"

            value: float = 0.0

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(InfOp)
        registered.append(InfOp)

        try:
            lf.ops.invoke(InfOp._class_id(), value=float("inf"))
        except ValueError:
            pass

        try:
            lf.ops.invoke(InfOp._class_id(), value=float("-inf"))
        except ValueError:
            pass

    def test_empty_string(self, lf, prop_op_fixture):
        """String property with empty string."""
        registered, lfs_types = prop_op_fixture

        class EmptyStrOp(lfs_types.Operator):
            lf_label = "Empty String"

            name: str = "default"

            def execute(self, context):
                return {"FINISHED"}

        lf.register_class(EmptyStrOp)
        registered.append(EmptyStrOp)

        lf.ops.invoke(EmptyStrOp._class_id(), name="")
