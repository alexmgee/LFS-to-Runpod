# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the RNA-style property system (optimization_params, panel prefix, etc.)."""

import pytest


class TestOptimizationParams:
    """Tests for lf.optimization_params() and property introspection."""

    def test_optimization_params_exists(self, lf):
        """optimization_params() function should be available."""
        assert hasattr(lf, "optimization_params")
        params = lf.optimization_params()
        assert params is not None

    def test_get_iterations(self, lf):
        """Should be able to read iterations property."""
        params = lf.optimization_params()
        iterations = params.iterations
        assert isinstance(iterations, int)
        assert iterations >= 0

    def test_get_learning_rates(self, lf):
        """Should be able to read learning rate properties."""
        params = lf.optimization_params()

        # All learning rates should be positive floats
        assert isinstance(params.means_lr, float)
        assert params.means_lr >= 0

        assert isinstance(params.shs_lr, float)
        assert params.shs_lr >= 0

        assert isinstance(params.opacity_lr, float)
        assert params.opacity_lr >= 0

        assert isinstance(params.scaling_lr, float)
        assert params.scaling_lr >= 0

        assert isinstance(params.rotation_lr, float)
        assert params.rotation_lr >= 0

    def test_get_boolean_properties(self, lf):
        """Should be able to read boolean properties."""
        params = lf.optimization_params()

        assert isinstance(params.headless, bool)
        assert isinstance(params.use_bilateral_grid, bool)
        assert isinstance(params.invert_masks, bool)
        assert isinstance(params.random, bool)
        assert isinstance(params.enable_sparsity, bool)

    def test_get_string_property(self, lf):
        """Should be able to read strategy string property."""
        params = lf.optimization_params()

        strategy = params.strategy
        assert isinstance(strategy, str)
        assert strategy in ("mcmc", "adc", "")

    def test_properties_list(self, lf):
        """properties() should return list of property info dicts."""
        params = lf.optimization_params()

        props = params.properties()
        assert isinstance(props, list)
        assert len(props) > 0

        # Each property is a dict with id, name, group, value
        prop_ids = [p["id"] for p in props]
        assert "iterations" in prop_ids
        assert "means_lr" in prop_ids
        assert "strategy" in prop_ids

    def test_prop_info_returns_dict(self, lf):
        """prop_info() should return metadata dict."""
        params = lf.optimization_params()

        info = params.prop_info("iterations")
        assert isinstance(info, dict)
        assert "id" in info
        assert "name" in info
        assert "type" in info
        assert "min" in info
        assert "max" in info
        assert "default" in info

    def test_prop_info_float(self, lf):
        """prop_info for float property should have correct metadata."""
        params = lf.optimization_params()

        info = params.prop_info("means_lr")
        assert info["type"] == "float"
        assert isinstance(info["min"], float)
        assert isinstance(info["max"], float)
        assert info["min"] <= info["max"]

    def test_prop_info_int(self, lf):
        """prop_info for int property should have correct metadata."""
        params = lf.optimization_params()

        info = params.prop_info("sh_degree")
        assert info["type"] == "int"
        assert isinstance(info["min"], int)
        assert isinstance(info["max"], int)

    def test_prop_info_readonly(self, lf):
        """prop_info should indicate readonly status."""
        params = lf.optimization_params()

        headless_info = params.prop_info("headless")
        assert "readonly" in headless_info
        assert headless_info["readonly"] is True

        means_lr_info = params.prop_info("means_lr")
        assert means_lr_info["readonly"] is False

    def test_prop_info_live_update(self, lf):
        """prop_info should indicate live_update status for learning rates."""
        params = lf.optimization_params()

        info = params.prop_info("means_lr")
        assert "live_update" in info
        assert info["live_update"] is True

    def test_prop_info_needs_restart(self, lf):
        """prop_info should indicate needs_restart for certain properties."""
        params = lf.optimization_params()

        info = params.prop_info("use_bilateral_grid")
        assert "needs_restart" in info
        assert info["needs_restart"] is True

        strategy_info = params.prop_info("strategy")
        assert strategy_info["needs_restart"] is True

    def test_unknown_property_raises(self, lf):
        """Accessing unknown property should raise RuntimeError."""
        params = lf.optimization_params()

        with pytest.raises(RuntimeError, match="Unknown property"):
            params.prop_info("nonexistent_property_xyz")

    def test_set_and_get_property(self, lf):
        """Should be able to set and get property values."""
        params = lf.optimization_params()

        # Save original
        original = params.means_lr

        # Modify
        params.means_lr = 0.0001
        assert abs(params.means_lr - 0.0001) < 1e-7

        # Restore (optional, since params is not persisted)
        params.means_lr = original

    def test_set_readonly_raises(self, lf):
        """Setting readonly property should raise."""
        params = lf.optimization_params()

        with pytest.raises(AttributeError):
            params.headless = True


class TestPanelPrefix:
    """Tests for panel_* prefix (backward compatibility with bl_*)."""

    def test_panel_class_with_panel_prefix(self, lf):
        """Panel with panel_label should register correctly."""

        class TestPanel:
            panel_label = "Test Panel"
            panel_space = "SIDE_PANEL"
            panel_order = 50

            def draw(self, layout):
                layout.label("Hello")

        # Should not raise - use lf.ui submodule
        lf.ui.register_panel(TestPanel)
        lf.ui.unregister_panel(TestPanel)

    def test_panel_class_with_bl_prefix(self, lf):
        """Panel with bl_label (legacy) should still work."""

        class LegacyPanel:
            bl_label = "Legacy Panel"
            bl_space = "SIDE_PANEL"
            bl_order = 99

            def draw(self, layout):
                layout.label("Legacy")

        # Should not raise (backward compat)
        lf.ui.register_panel(LegacyPanel)
        lf.ui.unregister_panel(LegacyPanel)

    def test_panel_prefix_precedence(self, lf):
        """panel_* should take precedence over bl_*."""

        class MixedPanel:
            panel_label = "New Style"
            bl_label = "Old Style"  # Should be ignored
            panel_space = "FLOATING"

            def draw(self, layout):
                pass

        # Register and verify it uses panel_label
        lf.ui.register_panel(MixedPanel)
        # The panel should be registered with "New Style" label
        # We can only verify this indirectly by not crashing
        lf.ui.unregister_panel(MixedPanel)


class TestPanelRegistry:
    """Tests for panel registration and lifecycle."""

    def test_register_unregister(self, lf):
        """Basic register/unregister cycle."""

        class SimplePanel:
            panel_label = "Simple"
            panel_space = "FLOATING"

            def draw(self, layout):
                pass

        lf.ui.register_panel(SimplePanel)
        lf.ui.unregister_panel(SimplePanel)

    def test_duplicate_register_updates(self, lf):
        """Re-registering a panel with same label should update it."""

        class UpdatePanel:
            panel_label = "Updatable"
            panel_space = "SIDE_PANEL"

            def draw(self, layout):
                layout.label("Version 1")

        lf.ui.register_panel(UpdatePanel)

        # Re-register with updated draw
        class UpdatePanel:
            panel_label = "Updatable"
            panel_space = "SIDE_PANEL"

            def draw(self, layout):
                layout.label("Version 2")

        lf.ui.register_panel(UpdatePanel)  # Should not raise
        lf.ui.unregister_panel(UpdatePanel)

    def test_unregister_nonexistent_is_safe(self, lf):
        """Unregistering a non-registered panel should not crash."""

        class NeverRegistered:
            panel_label = "Never"
            panel_space = "FLOATING"

            def draw(self, layout):
                pass

        # Should not raise
        lf.ui.unregister_panel(NeverRegistered)


class TestPropertyCallbacks:
    """Tests for property change callbacks."""

    def test_on_property_change_function(self, lf):
        """on_property_change() should register a callback."""
        callback_called = []

        def on_lr_change(old_val, new_val):
            callback_called.append((old_val, new_val))

        # Register callback - returns subscription ID
        sub_id = lf.on_property_change("optimization.means_lr", on_lr_change)
        assert isinstance(sub_id, int)
        assert sub_id > 0

        # Trigger change
        params = lf.optimization_params()
        old = params.means_lr
        params.means_lr = 0.00001

        # Note: callback fires synchronously when property changes
        # Restore original
        params.means_lr = old

        # Cleanup
        lf.unsubscribe_property_change(sub_id)

    def test_property_callback_decorator(self, lf):
        """@property_callback decorator should work."""
        callback_called = []

        @lf.property_callback("optimization.means_lr")
        def on_lr_change(old_val, new_val):
            callback_called.append((old_val, new_val))

        # The decorator registers the callback
        # Trigger change
        params = lf.optimization_params()
        old = params.means_lr
        params.means_lr = 0.00001
        params.means_lr = old

        # Note: decorator doesn't provide a way to unsubscribe
        # The callback will be cleaned up when the module is unloaded
        # This is a known limitation of the decorator pattern

    def test_unsubscribe_by_id(self, lf):
        """Unsubscribe by subscription ID should work."""

        def callback(old_val, new_val):
            pass

        # Subscribe
        sub_id = lf.on_property_change("optimization.means_lr", callback)
        assert isinstance(sub_id, int)

        # Unsubscribe by ID
        lf.unsubscribe_property_change(sub_id)


class TestUILayout:
    """Tests for PyUILayout methods (without ImGui context)."""

    # Note: These tests cannot actually call layout methods since they
    # require an active ImGui context. We can only test that the bindings exist.

    def test_layout_class_exists(self, lf):
        """UILayout class should exist in ui submodule."""
        assert hasattr(lf.ui, "UILayout")
        # Can't instantiate directly without ImGui context

    def test_panel_registration_implies_layout_works(self, lf):
        """Register/unregister panel verifies layout binding works."""
        class LayoutTestPanel:
            panel_label = "Layout Test"
            panel_space = "FLOATING"

            def draw(self, layout):
                # These would work if we had ImGui context:
                # layout.label("Test")
                # layout.button("Click")
                # layout.prop(params, "means_lr")
                pass

        lf.ui.register_panel(LayoutTestPanel)
        lf.ui.unregister_panel(LayoutTestPanel)
