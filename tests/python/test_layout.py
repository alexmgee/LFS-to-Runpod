# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the layout system."""

import sys

import pytest

sys.path.insert(0, "build/python")

from lfs_plugins.ui.layout import (
    LayoutNode,
    Stack,
    Tabs,
    Panel,
    Conditional,
    Collapsible,
    Spacer,
    Group,
    walk_layout,
    find_panels,
)
from lfs_plugins.ui.discovery import ComponentRegistry
from lfs_plugins.ui.protocols import Drawable


class MockPanel:
    """Mock panel for testing."""

    label = "Mock Panel"

    def draw(self, layout):
        pass


class MockPanel2:
    """Second mock panel."""

    label = "Mock Panel 2"

    def draw(self, layout):
        pass


class FallbackPanel:
    """Fallback panel for conditional tests."""

    label = "Fallback"

    def draw(self, layout):
        pass


class TestPanel:
    """Tests for Panel node."""

    def test_panel_wraps_class(self):
        """Panel should wrap a panel class."""
        p = Panel(MockPanel)
        assert p.panel_class is MockPanel

    def test_panel_label_from_attribute(self):
        """Panel should get label from class."""
        p = Panel(MockPanel)
        assert p.label == "Mock Panel"

    def test_panel_no_children(self):
        """Panel is a leaf node."""
        p = Panel(MockPanel)
        assert p.get_children() == ()

    def test_panel_with_instance(self):
        """Panel can hold a pre-created instance."""
        instance = MockPanel()
        p = Panel(MockPanel, instance=instance)
        assert p.instance is instance


class TestStack:
    """Tests for Stack node."""

    def test_stack_children(self):
        """Stack should hold children."""
        layout = Stack([Panel(MockPanel), Panel(MockPanel2)])
        assert len(layout.children) == 2

    def test_stack_get_children(self):
        """get_children should return children tuple."""
        p1 = Panel(MockPanel)
        p2 = Panel(MockPanel2)
        layout = Stack([p1, p2])
        assert layout.get_children() == (p1, p2)

    def test_stack_spacing(self):
        """Stack can have spacing."""
        layout = Stack([Panel(MockPanel)], spacing=10)
        assert layout.spacing == 10

    def test_stack_empty(self):
        """Empty stack should work."""
        layout = Stack([])
        assert layout.children == ()


class TestTabs:
    """Tests for Tabs node."""

    def test_tabs_children_and_labels(self):
        """Tabs should hold children and labels."""
        layout = Tabs(
            children=[Panel(MockPanel), Panel(MockPanel2)],
            labels=["Tab 1", "Tab 2"],
        )
        assert len(layout.children) == 2
        assert layout.labels == ("Tab 1", "Tab 2")

    def test_tabs_auto_labels(self):
        """Tabs should auto-generate labels from panel classes."""
        layout = Tabs(children=[Panel(MockPanel), Panel(MockPanel2)])
        assert layout.labels == ("Mock Panel", "Mock Panel 2")

    def test_tabs_default_index(self):
        """Tabs should have default_index."""
        layout = Tabs(
            children=[Panel(MockPanel), Panel(MockPanel2)],
            default_index=1,
        )
        assert layout.default_index == 1

    def test_tabs_get_children(self):
        """get_children should return all tabs."""
        p1 = Panel(MockPanel)
        p2 = Panel(MockPanel2)
        layout = Tabs(children=[p1, p2])
        assert layout.get_children() == (p1, p2)


class TestConditional:
    """Tests for Conditional node."""

    def test_conditional_true_shows_child(self):
        """Conditional should show child when true."""
        layout = Conditional(
            condition=lambda: True,
            child=Panel(MockPanel),
        )
        children = layout.get_children()
        assert len(children) == 1
        assert isinstance(children[0], Panel)

    def test_conditional_false_hides_child(self):
        """Conditional should hide child when false."""
        layout = Conditional(
            condition=lambda: False,
            child=Panel(MockPanel),
        )
        children = layout.get_children()
        assert len(children) == 0

    def test_conditional_fallback(self):
        """Conditional should show fallback when false."""
        layout = Conditional(
            condition=lambda: False,
            child=Panel(MockPanel),
            fallback=Panel(FallbackPanel),
        )
        children = layout.get_children()
        assert len(children) == 1
        assert children[0].panel_class is FallbackPanel


class TestCollapsible:
    """Tests for Collapsible node."""

    def test_collapsible_label(self):
        """Collapsible should have label."""
        layout = Collapsible("Section", Panel(MockPanel))
        assert layout.label == "Section"

    def test_collapsible_child(self):
        """Collapsible should have child."""
        p = Panel(MockPanel)
        layout = Collapsible("Section", p)
        assert layout.get_children() == (p,)

    def test_collapsible_default_open(self):
        """Collapsible can be default open or closed."""
        layout = Collapsible("Section", Panel(MockPanel), default_open=False)
        assert layout.default_open is False


class TestSpacer:
    """Tests for Spacer node."""

    def test_spacer_height(self):
        """Spacer should have height."""
        s = Spacer(height=20)
        assert s.height == 20

    def test_spacer_no_children(self):
        """Spacer is a leaf node."""
        s = Spacer()
        assert s.get_children() == ()


class TestGroup:
    """Tests for Group node."""

    def test_group_children(self):
        """Group should hold children."""
        p1 = Panel(MockPanel)
        p2 = Panel(MockPanel2)
        g = Group([p1, p2])
        assert g.get_children() == (p1, p2)

    def test_group_name(self):
        """Group can have a name."""
        g = Group([Panel(MockPanel)], name="my_group")
        assert g.name == "my_group"


class TestWalkLayout:
    """Tests for walk_layout function."""

    def test_walk_single_node(self):
        """walk_layout should return single node."""
        p = Panel(MockPanel)
        result = walk_layout(p)
        assert result == [p]

    def test_walk_nested(self):
        """walk_layout should traverse nested nodes."""
        p1 = Panel(MockPanel)
        p2 = Panel(MockPanel2)
        stack = Stack([p1, p2])
        result = walk_layout(stack)
        assert stack in result
        assert p1 in result
        assert p2 in result

    def test_walk_deep_nesting(self):
        """walk_layout should handle deep nesting."""
        p = Panel(MockPanel)
        layout = Stack([
            Tabs(children=[
                Collapsible("Section", p),
            ]),
        ])
        result = walk_layout(layout)
        assert p in result


class TestFindPanels:
    """Tests for find_panels function."""

    def test_find_panels_in_stack(self):
        """find_panels should find all panels."""
        p1 = Panel(MockPanel)
        p2 = Panel(MockPanel2)
        layout = Stack([p1, p2])
        result = find_panels(layout)
        assert p1 in result
        assert p2 in result

    def test_find_panels_nested(self):
        """find_panels should find nested panels."""
        p = Panel(MockPanel)
        layout = Stack([Tabs(children=[Collapsible("Sec", p)])])
        result = find_panels(layout)
        assert p in result


class TestComponentRegistry:
    """Tests for ComponentRegistry."""

    def test_get_or_create(self):
        """get_or_create should create singleton."""
        registry = ComponentRegistry()
        a = registry.get_or_create(MockPanel)
        b = registry.get_or_create(MockPanel)
        assert a is b

    def test_get_returns_none_if_not_created(self):
        """get should return None if not created."""
        registry = ComponentRegistry()
        result = registry.get(MockPanel)
        assert result is None

    def test_discover_from_layout(self):
        """discover_from_layout should register panels."""
        registry = ComponentRegistry()
        layout = Stack([Panel(MockPanel), Panel(MockPanel2)])
        discovered = registry.discover_from_layout(layout)
        assert MockPanel in discovered
        assert MockPanel2 in discovered

    def test_register_explicit(self):
        """register should add panel class."""
        registry = ComponentRegistry()
        registry.register(MockPanel)
        assert MockPanel in registry.all_classes()

    def test_unregister(self):
        """unregister should remove panel class."""
        registry = ComponentRegistry()
        registry.register(MockPanel)
        registry.unregister(MockPanel)
        assert MockPanel not in registry.all_classes()

    def test_clear(self):
        """clear should remove all registrations."""
        registry = ComponentRegistry()
        registry.register(MockPanel)
        registry.register(MockPanel2)
        registry.clear()
        assert len(registry.all_classes()) == 0


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
