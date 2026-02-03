# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the plugin system."""

import pytest
import tempfile
import sys
from pathlib import Path


@pytest.fixture
def temp_plugins_dir(monkeypatch):
    """Create temporary plugins directory."""
    with tempfile.TemporaryDirectory() as tmpdir:
        plugins_dir = Path(tmpdir) / "plugins"
        plugins_dir.mkdir()

        # Ensure lfs_plugins is importable
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.manager import PluginManager

        original_instance = PluginManager._instance
        PluginManager._instance = None

        mgr = PluginManager.instance()
        mgr._plugins_dir = plugins_dir

        yield plugins_dir

        PluginManager._instance = original_instance


@pytest.fixture
def sample_plugin(temp_plugins_dir):
    """Create a sample plugin."""
    plugin_dir = temp_plugins_dir / "sample_plugin"
    plugin_dir.mkdir()

    (plugin_dir / "plugin.toml").write_text(
        """
[plugin]
name = "sample_plugin"
version = "1.0.0"
description = "A sample plugin"

[dependencies]
packages = []

[lifecycle]
auto_start = true
hot_reload = true
"""
    )

    (plugin_dir / "__init__.py").write_text(
        """
LOADED = False

def on_load():
    global LOADED
    LOADED = True

def on_unload():
    global LOADED
    LOADED = False
"""
    )

    return plugin_dir


class TestPluginDiscovery:
    """Tests for plugin discovery."""

    def test_discover_empty(self, temp_plugins_dir):
        """Should return empty list when no plugins exist."""
        from lfs_plugins import PluginManager

        mgr = PluginManager.instance()
        plugins = mgr.discover()
        assert plugins == []

    def test_discover_plugin(self, sample_plugin):
        """Should discover valid plugins."""
        from lfs_plugins import PluginManager

        mgr = PluginManager.instance()
        plugins = mgr.discover()

        assert len(plugins) == 1
        assert plugins[0].name == "sample_plugin"
        assert plugins[0].version == "1.0.0"
        assert plugins[0].description == "A sample plugin"

    def test_discover_ignores_invalid(self, temp_plugins_dir):
        """Should skip directories without plugin.toml."""
        from lfs_plugins import PluginManager

        # Create directory without plugin.toml
        invalid_dir = temp_plugins_dir / "not_a_plugin"
        invalid_dir.mkdir()
        (invalid_dir / "__init__.py").write_text("# not a plugin")

        mgr = PluginManager.instance()
        plugins = mgr.discover()
        assert plugins == []


class TestPluginLoading:
    """Tests for plugin loading."""

    def test_load_plugin(self, sample_plugin):
        """Should load a valid plugin."""
        from lfs_plugins import PluginManager, PluginState

        mgr = PluginManager.instance()

        result = mgr.load("sample_plugin")
        assert result is True
        assert mgr.get_state("sample_plugin") == PluginState.ACTIVE

        mgr.unload("sample_plugin")

    def test_load_calls_on_load(self, sample_plugin):
        """Should call on_load() when loading."""
        from lfs_plugins import PluginManager

        mgr = PluginManager.instance()
        mgr.load("sample_plugin")

        plugin_module = sys.modules.get("lfs_plugins.sample_plugin")
        assert plugin_module is not None
        assert plugin_module.LOADED is True

        mgr.unload("sample_plugin")

    def test_unload_calls_on_unload(self, sample_plugin):
        """Should call on_unload() when unloading."""
        from lfs_plugins import PluginManager, PluginState

        mgr = PluginManager.instance()
        mgr.load("sample_plugin")
        mgr.unload("sample_plugin")

        assert mgr.get_state("sample_plugin") == PluginState.UNLOADED

    def test_load_nonexistent_raises(self, temp_plugins_dir):
        """Should raise PluginError for unknown plugin."""
        from lfs_plugins import PluginManager, PluginError

        mgr = PluginManager.instance()
        with pytest.raises(PluginError, match="not found"):
            mgr.load("nonexistent_plugin")


class TestPluginReload:
    """Tests for hot reload."""

    def test_reload_plugin(self, sample_plugin):
        """Should reload plugin with updated code."""
        from lfs_plugins import PluginManager, PluginState

        mgr = PluginManager.instance()
        mgr.load("sample_plugin")

        # Modify plugin
        (sample_plugin / "__init__.py").write_text(
            """
LOADED = False
RELOADED = True

def on_load():
    global LOADED
    LOADED = True

def on_unload():
    global LOADED
    LOADED = False
"""
        )

        result = mgr.reload("sample_plugin")
        assert result is True

        plugin_module = sys.modules.get("lfs_plugins.sample_plugin")
        assert plugin_module.RELOADED is True

        mgr.unload("sample_plugin")


class TestPluginInfo:
    """Tests for PluginInfo parsing."""

    def test_parse_manifest_defaults(self, temp_plugins_dir):
        """Should use defaults for missing fields."""
        from lfs_plugins import PluginManager

        plugin_dir = temp_plugins_dir / "minimal_plugin"
        plugin_dir.mkdir()
        (plugin_dir / "plugin.toml").write_text(
            """
[plugin]
name = "minimal"
version = "0.1.0"
"""
        )
        (plugin_dir / "__init__.py").write_text("def on_load(): pass")

        mgr = PluginManager.instance()
        plugins = mgr.discover()

        assert len(plugins) == 1
        assert plugins[0].name == "minimal"
        assert plugins[0].auto_start is True
        assert plugins[0].hot_reload is True
        assert plugins[0].dependencies == []


class TestPluginState:
    """Tests for plugin state tracking."""

    def test_get_state_unloaded(self, sample_plugin):
        """Should return None for unknown plugin."""
        from lfs_plugins import PluginManager

        mgr = PluginManager.instance()
        # Plugin discovered but not loaded yet
        assert mgr.get_state("sample_plugin") is None

    def test_get_error_after_failure(self, temp_plugins_dir):
        """Should store error message on failure."""
        from lfs_plugins import PluginManager, PluginState

        # Create a plugin that will fail to load
        plugin_dir = temp_plugins_dir / "broken_plugin"
        plugin_dir.mkdir()
        (plugin_dir / "plugin.toml").write_text(
            """
[plugin]
name = "broken_plugin"
version = "1.0.0"
"""
        )
        (plugin_dir / "__init__.py").write_text("raise RuntimeError('intentional')")

        mgr = PluginManager.instance()
        result = mgr.load("broken_plugin")

        assert result is False
        assert mgr.get_state("broken_plugin") == PluginState.ERROR
        assert mgr.get_error("broken_plugin") is not None


class TestPluginLifecycle:
    """Tests for plugin lifecycle management."""

    def test_list_loaded(self, sample_plugin):
        """Should list loaded plugins."""
        from lfs_plugins import PluginManager

        mgr = PluginManager.instance()
        assert mgr.list_loaded() == []

        mgr.load("sample_plugin")
        assert "sample_plugin" in mgr.list_loaded()

        mgr.unload("sample_plugin")
        assert mgr.list_loaded() == []

    def test_load_all(self, temp_plugins_dir):
        """Should load all plugins with auto_start=True."""
        from lfs_plugins import PluginManager

        # Create two plugins
        for name in ["plugin_a", "plugin_b"]:
            plugin_dir = temp_plugins_dir / name
            plugin_dir.mkdir()
            (plugin_dir / "plugin.toml").write_text(
                f"""
[plugin]
name = "{name}"
version = "1.0.0"

[lifecycle]
auto_start = true
"""
            )
            (plugin_dir / "__init__.py").write_text("def on_load(): pass")

        mgr = PluginManager.instance()
        results = mgr.load_all()

        assert len(results) == 2
        assert results["plugin_a"] is True
        assert results["plugin_b"] is True

        # Cleanup
        mgr.unload("plugin_a")
        mgr.unload("plugin_b")


class TestVersionEnforcement:
    """Tests for plugin version enforcement."""

    def test_version_check_passes(self, temp_plugins_dir):
        """Should load plugin with compatible version requirement."""
        from lfs_plugins import PluginManager, PluginState

        plugin_dir = temp_plugins_dir / "compatible_plugin"
        plugin_dir.mkdir()
        (plugin_dir / "plugin.toml").write_text(
            """
[plugin]
name = "compatible_plugin"
version = "1.0.0"
min_lichtfeld_version = "0.5.0"
"""
        )
        (plugin_dir / "__init__.py").write_text("def on_load(): pass")

        mgr = PluginManager.instance()
        result = mgr.load("compatible_plugin")

        assert result is True
        assert mgr.get_state("compatible_plugin") == PluginState.ACTIVE
        mgr.unload("compatible_plugin")

    def test_version_check_fails(self, temp_plugins_dir):
        """Should fail to load plugin requiring newer LichtFeld version."""
        from lfs_plugins import PluginManager, PluginVersionError

        plugin_dir = temp_plugins_dir / "future_plugin"
        plugin_dir.mkdir()
        (plugin_dir / "plugin.toml").write_text(
            """
[plugin]
name = "future_plugin"
version = "1.0.0"
min_lichtfeld_version = "99.0.0"
"""
        )
        (plugin_dir / "__init__.py").write_text("def on_load(): pass")

        mgr = PluginManager.instance()

        with pytest.raises(PluginVersionError, match="requires LichtFeld >= 99.0.0"):
            mgr.load("future_plugin")


class TestModuleNamespacing:
    """Tests for plugin module namespacing."""

    def test_plugin_module_namespaced(self, sample_plugin):
        """Plugin module should be registered under lfs_plugins namespace."""
        from lfs_plugins import PluginManager

        mgr = PluginManager.instance()
        mgr.load("sample_plugin")

        assert "lfs_plugins.sample_plugin" in sys.modules
        assert "sample_plugin" not in sys.modules

        mgr.unload("sample_plugin")

        assert "lfs_plugins.sample_plugin" not in sys.modules

    def test_namespace_prevents_collision(self, temp_plugins_dir):
        """Should not conflict with pip packages of same name."""
        from lfs_plugins import PluginManager

        plugin_dir = temp_plugins_dir / "json"
        plugin_dir.mkdir()
        (plugin_dir / "plugin.toml").write_text(
            """
[plugin]
name = "json"
version = "1.0.0"
"""
        )
        (plugin_dir / "__init__.py").write_text("PLUGIN_LOADED = True\ndef on_load(): pass")

        mgr = PluginManager.instance()
        mgr.load("json")

        import json as stdlib_json
        plugin_json = sys.modules.get("lfs_plugins.json")

        assert hasattr(stdlib_json, "dumps")
        assert hasattr(plugin_json, "PLUGIN_LOADED")
        assert not hasattr(stdlib_json, "PLUGIN_LOADED")

        mgr.unload("json")


class TestGitHubUrlParsing:
    """Tests for GitHub URL parsing."""

    def test_parse_full_url(self):
        """Should parse standard GitHub URL."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url("https://github.com/owner/repo")
        assert owner == "owner"
        assert repo == "repo"
        assert branch is None

    def test_parse_url_with_git_suffix(self):
        """Should strip .git suffix."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url("https://github.com/owner/repo.git")
        assert repo == "repo"

    def test_parse_url_with_branch(self):
        """Should extract branch from /tree/branch pattern."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url(
            "https://github.com/owner/repo/tree/develop"
        )
        assert branch == "develop"

    def test_parse_github_shorthand(self):
        """Should parse github:owner/repo shorthand."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url("github:owner/repo")
        assert owner == "owner"
        assert repo == "repo"
        assert branch is None

    def test_parse_github_shorthand_with_branch(self):
        """Should parse github:owner/repo@branch shorthand."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url("github:owner/repo@feature")
        assert branch == "feature"

    def test_parse_owner_repo_shorthand(self):
        """Should parse owner/repo shorthand."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url("MrNeRF/LichtFeld-Comap-Plugin")
        assert owner == "MrNeRF"
        assert repo == "LichtFeld-Comap-Plugin"

    def test_parse_url_without_scheme(self):
        """Should handle github.com/owner/repo without https://."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url("github.com/MrNeRF/LichtFeld-Comap-Plugin")
        assert owner == "MrNeRF"
        assert repo == "LichtFeld-Comap-Plugin"
        assert branch is None

    def test_parse_www_url_without_scheme(self):
        """Should handle www.github.com/owner/repo without https://."""
        from lfs_plugins.installer import parse_github_url

        owner, repo, branch = parse_github_url("www.github.com/owner/repo")
        assert owner == "owner"
        assert repo == "repo"

    def test_parse_invalid_url_raises(self):
        """Should raise for non-GitHub URLs."""
        from lfs_plugins.installer import parse_github_url
        from lfs_plugins.errors import PluginError

        with pytest.raises(PluginError, match="Not a GitHub URL"):
            parse_github_url("https://gitlab.com/owner/repo")
