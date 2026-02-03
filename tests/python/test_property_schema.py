# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for PropertySchema extraction from operator properties."""

import sys
from pathlib import Path

import pytest


@pytest.fixture
def lf():
    """Import lichtfeld module."""
    project_root = Path(__file__).parent.parent.parent
    build_python = project_root / "build" / "src" / "python"
    if str(build_python) not in sys.path:
        sys.path.insert(0, str(build_python))

    try:
        import lichtfeld

        return lichtfeld
    except ImportError as e:
        pytest.skip(f"lichtfeld module not available: {e}")


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


class TestPropertySchemaExtraction:
    """Tests for property schema extraction from Python operators."""

    def test_float_property_basic(self, lf, lfs_types):
        """FloatProperty should be recognized."""
        received = {}

        class FloatOp(lfs_types.Operator):
            lf_label = "Float Op"
            amount: float = 1.0

            def execute(self, context):
                received["amount"] = self.amount
                return {"FINISHED"}

        lf.register_class(FloatOp)
        try:
            lf.ops.invoke(FloatOp._class_id())
            assert received["amount"] == 1.0
        finally:
            lf.unregister_class(FloatOp)

    def test_int_property_basic(self, lf, lfs_types):
        """IntProperty should be recognized."""
        received = {}

        class IntOp(lfs_types.Operator):
            lf_label = "Int Op"
            count: int = 10

            def execute(self, context):
                received["count"] = self.count
                return {"FINISHED"}

        lf.register_class(IntOp)
        try:
            lf.ops.invoke(IntOp._class_id())
            assert received["count"] == 10
        finally:
            lf.unregister_class(IntOp)

    def test_string_property_basic(self, lf, lfs_types):
        """StringProperty should be recognized."""
        received = {}

        class StringOp(lfs_types.Operator):
            lf_label = "String Op"
            name: str = "default"

            def execute(self, context):
                received["name"] = self.name
                return {"FINISHED"}

        lf.register_class(StringOp)
        try:
            lf.ops.invoke(StringOp._class_id())
            assert received["name"] == "default"
        finally:
            lf.unregister_class(StringOp)

    def test_bool_property_basic(self, lf, lfs_types):
        """BoolProperty should be recognized."""
        received = {}

        class BoolOp(lfs_types.Operator):
            lf_label = "Bool Op"
            enabled: bool = False

            def execute(self, context):
                received["enabled"] = self.enabled
                return {"FINISHED"}

        lf.register_class(BoolOp)
        try:
            lf.ops.invoke(BoolOp._class_id())
            assert received["enabled"] is False
        finally:
            lf.unregister_class(BoolOp)


class TestPropertySchemaWithConstraints:
    """Tests for property schemas with constraints (min/max/step)."""

    def test_override_float_with_value(self, lf, lfs_types):
        """Float properties should accept override values."""
        received = {}

        class ConstrainedFloat(lfs_types.Operator):
            lf_label = "Constrained Float"
            value: float = 0.5

            def execute(self, context):
                received["value"] = self.value
                return {"FINISHED"}

        lf.register_class(ConstrainedFloat)
        try:
            # Override with explicit value
            lf.ops.invoke(ConstrainedFloat._class_id(), value=0.75)
            assert abs(received["value"] - 0.75) < 0.001
        finally:
            lf.unregister_class(ConstrainedFloat)

    def test_override_int_with_value(self, lf, lfs_types):
        """Int properties should accept override values."""
        received = {}

        class ConstrainedInt(lfs_types.Operator):
            lf_label = "Constrained Int"
            count: int = 5

            def execute(self, context):
                received["count"] = self.count
                return {"FINISHED"}

        lf.register_class(ConstrainedInt)
        try:
            lf.ops.invoke(ConstrainedInt._class_id(), count=42)
            assert received["count"] == 42
        finally:
            lf.unregister_class(ConstrainedInt)


class TestPropertySchemaComplexTypes:
    """Tests for complex property types (vectors, enums)."""

    def test_list_property_passthrough(self, lf, lfs_types):
        """Lists should pass through as properties."""
        received = {}

        class ListOp(lfs_types.Operator):
            lf_label = "List Op"

            def execute(self, context):
                values = getattr(self, "values", None)
                if values is not None:
                    received["sum"] = sum(values)
                return {"FINISHED"}

        lf.register_class(ListOp)
        try:
            lf.ops.invoke(ListOp._class_id(), values=[1, 2, 3, 4])
            assert received["sum"] == 10
        finally:
            lf.unregister_class(ListOp)

    def test_dict_property_passthrough(self, lf, lfs_types):
        """Dicts should pass through as properties."""
        received = {}

        class DictOp(lfs_types.Operator):
            lf_label = "Dict Op"

            def execute(self, context):
                config = getattr(self, "config", None)
                if config is not None:
                    received["key_count"] = len(config)
                return {"FINISHED"}

        lf.register_class(DictOp)
        try:
            lf.ops.invoke(DictOp._class_id(), config={"a": 1, "b": 2})
            assert received["key_count"] == 2
        finally:
            lf.unregister_class(DictOp)


class TestPropertySchemaEdgeCases:
    """Tests for edge cases in property schema handling."""

    def test_none_property_value(self, lf, lfs_types):
        """None values should be handled correctly."""
        received = {}

        class NoneOp(lfs_types.Operator):
            lf_label = "None Op"

            def execute(self, context):
                val = getattr(self, "optional", "not_set")
                received["value"] = val
                return {"FINISHED"}

        lf.register_class(NoneOp)
        try:
            # Without the property
            lf.ops.invoke(NoneOp._class_id())
            assert received["value"] == "not_set"
        finally:
            lf.unregister_class(NoneOp)

    def test_empty_string_property(self, lf, lfs_types):
        """Empty strings should be handled correctly."""
        received = {}

        class EmptyStringOp(lfs_types.Operator):
            lf_label = "Empty String"
            text: str = "default"

            def execute(self, context):
                received["text"] = self.text
                received["empty"] = self.text == ""
                return {"FINISHED"}

        lf.register_class(EmptyStringOp)
        try:
            lf.ops.invoke(EmptyStringOp._class_id(), text="")
            assert received["empty"]
        finally:
            lf.unregister_class(EmptyStringOp)

    def test_special_characters_in_property(self, lf, lfs_types):
        """Special characters in string properties should work."""
        received = {}

        class SpecialCharsOp(lfs_types.Operator):
            lf_label = "Special Chars"
            text: str = ""

            def execute(self, context):
                received["text"] = self.text
                return {"FINISHED"}

        lf.register_class(SpecialCharsOp)
        try:
            special = "Hello\nWorld\t\"'\\"
            lf.ops.invoke(SpecialCharsOp._class_id(), text=special)
            assert received["text"] == special
        finally:
            lf.unregister_class(SpecialCharsOp)

    def test_zero_values(self, lf, lfs_types):
        """Zero values should be handled correctly (not treated as missing)."""
        received = {}

        class ZeroOp(lfs_types.Operator):
            lf_label = "Zero"
            int_val: int = 100
            float_val: float = 1.0

            def execute(self, context):
                received["int"] = self.int_val
                received["float"] = self.float_val
                return {"FINISHED"}

        lf.register_class(ZeroOp)
        try:
            lf.ops.invoke(ZeroOp._class_id(), int_val=0, float_val=0.0)
            assert received["int"] == 0
            assert received["float"] == 0.0
        finally:
            lf.unregister_class(ZeroOp)
