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

    (plugin_dir / "pyproject.toml").write_text(
        """
[project]
name = "sample_plugin"
version = "1.0.0"
description = "A sample plugin"
dependencies = []

[tool.lichtfeld]
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
        """Should skip directories without pyproject.toml."""
        from lfs_plugins import PluginManager

        # Create directory without pyproject.toml
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

    def test_load_venv_creation_fallback_sequence(self, temp_plugins_dir):
        """manager.load() should trigger venv creation fallback from sys.executable to uv-managed Python."""
        from unittest.mock import patch
        import subprocess as sp
        from lfs_plugins import PluginManager, PluginState

        plugin_name = "venv_fallback_plugin"
        plugin_dir = temp_plugins_dir / plugin_name
        plugin_dir.mkdir()
        (plugin_dir / "pyproject.toml").write_text(
            f"""
[project]
name = "{plugin_name}"
version = "1.0.0"
description = "Plugin for venv fallback integration test"
dependencies = []

[tool.lichtfeld]
auto_start = true
hot_reload = false
"""
        )
        (plugin_dir / "__init__.py").write_text("def on_load():\n    pass\n")

        mock_uv = plugin_dir / "uv"
        mock_uv.touch()

        fail = sp.CompletedProcess(
            args=[],
            returncode=2,
            stdout="",
            stderr="Could not find a suitable Python executable",
        )
        ok = sp.CompletedProcess(args=[], returncode=0, stdout="ok", stderr="")
        version_spec = f"{sys.version_info.major}.{sys.version_info.minor}"

        mgr = PluginManager.instance()
        with patch("lfs_plugins.installer.PluginInstaller._find_uv", return_value=mock_uv), \
             patch("lfs_plugins.installer.PluginInstaller._get_embedded_python", return_value=None), \
             patch("lfs_plugins.installer.PluginInstaller.install_dependencies", return_value=True), \
             patch("lfs_plugins.installer.subprocess.run", side_effect=[fail, ok]) as mock_run:
            assert mgr.load(plugin_name) is True
            assert mgr.get_state(plugin_name) == PluginState.ACTIVE
            assert mock_run.call_count == 2

            expected_prefix = [str(mock_uv), "venv", str(plugin_dir / ".venv"), "--python"]
            assert mock_run.call_args_list[0][0][0] == expected_prefix + [str(Path(sys.executable).resolve())]
            assert mock_run.call_args_list[1][0][0] == expected_prefix + [version_spec]

            first_env = mock_run.call_args_list[0][1]["env"]
            second_env = mock_run.call_args_list[1][1]["env"]
            assert first_env.get("PYTHONHOME") == sys.prefix
            assert second_env.get("PYTHONHOME") is None

        mgr.unload(plugin_name)


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

    def test_parse_minimal_manifest(self, temp_plugins_dir):
        """Should parse a manifest with all required fields and no extras."""
        from lfs_plugins import PluginManager

        plugin_dir = temp_plugins_dir / "minimal_plugin"
        plugin_dir.mkdir()
        (plugin_dir / "pyproject.toml").write_text(
            """
[project]
name = "minimal"
version = "0.1.0"
description = "Minimal plugin"

[tool.lichtfeld]
auto_start = false
hot_reload = false
"""
        )
        (plugin_dir / "__init__.py").write_text("def on_load(): pass")

        mgr = PluginManager.instance()
        plugins = mgr.discover()

        assert len(plugins) == 1
        assert plugins[0].name == "minimal"
        assert plugins[0].description == "Minimal plugin"
        assert plugins[0].auto_start is False
        assert plugins[0].hot_reload is False
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
        (plugin_dir / "pyproject.toml").write_text(
            """
[project]
name = "broken_plugin"
version = "1.0.0"
description = "Plugin that fails to load"

[tool.lichtfeld]
auto_start = true
hot_reload = false
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
            (plugin_dir / "pyproject.toml").write_text(
                f"""
[project]
name = "{name}"
version = "1.0.0"
description = "Auto-start plugin {name}"

[tool.lichtfeld]
auto_start = true
hot_reload = false
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
        (plugin_dir / "pyproject.toml").write_text(
            """
[project]
name = "compatible_plugin"
version = "1.0.0"
description = "Plugin with compatible version"

[tool.lichtfeld]
auto_start = true
hot_reload = false
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
        (plugin_dir / "pyproject.toml").write_text(
            """
[project]
name = "future_plugin"
version = "1.0.0"
description = "Plugin requiring future version"

[tool.lichtfeld]
auto_start = true
hot_reload = false
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
        (plugin_dir / "pyproject.toml").write_text(
            """
[project]
name = "json"
version = "1.0.0"
description = "Plugin named json for collision test"

[tool.lichtfeld]
auto_start = true
hot_reload = false
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


class TestInstallerSyncDetection:
    """Tests for uv sync vs uv pip install detection."""

    @pytest.fixture
    def installer_plugin(self, tmp_path):
        """Create a plugin directory with installer for testing."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        plugin_dir = tmp_path / "test_plugin"
        plugin_dir.mkdir()
        (plugin_dir / "pyproject.toml").write_text(
            """
[project]
name = "test_plugin"
version = "1.0.0"
description = "Plugin for installer tests"
dependencies = ["requests>=2.0"]

[tool.lichtfeld]
auto_start = true
hot_reload = false
"""
        )
        venv_dir = plugin_dir / ".venv"
        venv_dir.mkdir()
        (venv_dir / "bin").mkdir(parents=True)
        (venv_dir / "bin" / "python").touch()
        return plugin_dir

    def _make_installer(self, plugin_dir):
        from lfs_plugins.plugin import PluginInfo, PluginInstance
        from lfs_plugins.installer import PluginInstaller

        info = PluginInfo(
            name="test_plugin",
            version="1.0.0",
            description="",
            author="",
            path=plugin_dir,
            dependencies=["requests>=2.0"],
            auto_start=True,
            hot_reload=True,
        )
        instance = PluginInstance(info=info)
        instance.venv_path = plugin_dir / ".venv"
        return PluginInstaller(instance)

    @staticmethod
    def _mock_popen():
        from unittest.mock import MagicMock
        import io

        mock_proc = MagicMock()
        mock_proc.stdout = io.StringIO("")
        mock_proc.returncode = 0
        mock_proc.__enter__ = MagicMock(return_value=mock_proc)
        mock_proc.__exit__ = MagicMock(return_value=False)
        return mock_proc

    def test_pyproject_triggers_sync(self, installer_plugin):
        """pyproject.toml present should use uv sync."""
        from unittest.mock import patch

        (installer_plugin / "pyproject.toml").write_text(
            """
[project]
name = "test_plugin"
version = "0.1.0"
description = "Plugin for sync test"
dependencies = ["requests>=2.0"]

[tool.lichtfeld]
auto_start = true
hot_reload = false
"""
        )
        installer = self._make_installer(installer_plugin)
        mock_uv = installer_plugin / "uv"
        mock_uv.touch()
        mock_proc = self._mock_popen()

        with patch.object(installer, "_find_uv", return_value=mock_uv), \
             patch("subprocess.Popen", return_value=mock_proc) as mock_popen:
            installer.install_dependencies()
            cmd = mock_popen.call_args[0][0]
            assert cmd[1] == "sync"

    def test_stamp_invalidation_includes_pyproject(self, installer_plugin):
        """Stamp should be invalidated when pyproject.toml changes."""
        import os

        installer = self._make_installer(installer_plugin)
        stamp = installer._deps_stamp_path()
        stamp.touch()

        assert installer._deps_already_installed() is True

        pyproject = installer_plugin / "pyproject.toml"
        pyproject.write_text('[project]\nname = "test"\nversion = "0.1.0"\n')
        future = stamp.stat().st_mtime + 1.0
        os.utime(pyproject, (future, future))

        assert installer._deps_already_installed() is False

    def test_uv_env_sets_pythonhome_for_embedded(self, installer_plugin):
        """_uv_env() should set PYTHONHOME when using embedded Python."""
        installer = self._make_installer(installer_plugin)
        env = installer._uv_env(set_pythonhome=True)
        assert env["PYTHONHOME"] == sys.prefix

    def test_uv_env_strips_pythonhome_for_uv_managed(self, installer_plugin, monkeypatch):
        """_uv_env() should strip PYTHONHOME for uv-managed interpreters."""
        installer = self._make_installer(installer_plugin)
        monkeypatch.setenv("PYTHONHOME", "/tmp/python-home")
        env = installer._uv_env(set_pythonhome=False)
        assert env.get("PYTHONHOME") is None

    def test_install_strips_pythonhome_for_uv_sync(self, installer_plugin):
        """install_dependencies() should not pass PYTHONHOME to uv sync."""
        from unittest.mock import patch

        installer = self._make_installer(installer_plugin)
        mock_uv = installer_plugin / "uv"
        mock_uv.touch()
        mock_proc = self._mock_popen()

        with patch.object(installer, "_find_uv", return_value=mock_uv), \
             patch("subprocess.Popen", return_value=mock_proc) as mock_popen:
            installer.install_dependencies()
            env = mock_popen.call_args[1].get("env", {})
            assert env.get("PYTHONHOME") is None

    def test_ensure_venv_falls_back_to_uv_managed_python(self, installer_plugin):
        """ensure_venv() should retry with uv-managed Python when sys interpreter is unsuitable."""
        from unittest.mock import patch
        import subprocess as sp

        installer = self._make_installer(installer_plugin)
        venv_python = installer._get_venv_python()
        if venv_python.exists():
            venv_python.unlink()

        mock_uv = installer_plugin / "uv"
        mock_uv.touch()

        fail = sp.CompletedProcess(
            args=[],
            returncode=2,
            stdout="",
            stderr="Could not find a suitable Python executable",
        )
        ok = sp.CompletedProcess(args=[], returncode=0, stdout="ok", stderr="")
        version_spec = f"{sys.version_info.major}.{sys.version_info.minor}"

        with patch.object(installer, "_find_uv", return_value=mock_uv), \
             patch.object(installer, "_get_embedded_python", return_value=None), \
             patch("lfs_plugins.installer.subprocess.run", side_effect=[fail, ok]) as mock_run:
            assert installer.ensure_venv() is True
            assert mock_run.call_count == 2

            first_cmd = mock_run.call_args_list[0][0][0]
            second_cmd = mock_run.call_args_list[1][0][0]
            assert first_cmd[-2:] == ["--python", str(Path(sys.executable).resolve())]
            assert second_cmd[-2:] == ["--python", version_spec]

            first_env = mock_run.call_args_list[0][1]["env"]
            second_env = mock_run.call_args_list[1][1]["env"]
            assert first_env.get("PYTHONHOME") == sys.prefix
            assert second_env.get("PYTHONHOME") is None

    def test_find_uv_portable_does_not_fallback_to_system(self, installer_plugin):
        """Portable runtime must not fall back to system uv."""
        from unittest.mock import patch

        installer = self._make_installer(installer_plugin)

        with patch.object(installer, "_is_portable_bundle", return_value=True), \
             patch.object(installer, "_bundled_uv_candidates", return_value=[]), \
             patch("shutil.which") as mock_which:
            assert installer._find_uv() is None
            mock_which.assert_not_called()

    def test_ensure_venv_portable_uses_embedded_only(self, installer_plugin):
        """Portable runtime should use embedded interpreter only for uv venv."""
        from unittest.mock import patch
        import subprocess as sp
        from lfs_plugins.errors import PluginDependencyError

        installer = self._make_installer(installer_plugin)
        venv_python = installer._get_venv_python()
        if venv_python.exists():
            venv_python.unlink()

        embedded_dir = installer_plugin / "embedded"
        embedded_dir.mkdir()
        embedded_python = embedded_dir / "python.exe"
        embedded_python.touch()

        mock_uv = installer_plugin / "uv"
        mock_uv.touch()

        fail = sp.CompletedProcess(
            args=[],
            returncode=2,
            stdout="",
            stderr="Could not find a suitable Python executable",
        )

        with patch.object(installer, "_find_uv", return_value=mock_uv), \
             patch.object(installer, "_is_portable_bundle", return_value=True), \
             patch.object(installer, "_get_embedded_python", return_value=embedded_python), \
             patch("lfs_plugins.installer.subprocess.run", return_value=fail) as mock_run:
            with pytest.raises(PluginDependencyError):
                installer.ensure_venv()

            assert mock_run.call_count == 1
            cmd = mock_run.call_args_list[0][0][0]
            assert cmd[-2:] == ["--python", str(embedded_python)]
